/****************************************************************************
 *   Copyright  2026  Dirk Sarodnick
 ****************************************************************************/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "config.h"
#include "weather.h"
#include "weather_ble.h"
#include "weather_fetch.h"
#include "weather_forecast.h"
#include "images/resolve_owm_icon.h"
#include "hardware/ble/gadgetbridge.h"
#include "utils/bluejsonrequest.h"

#ifdef NATIVE_64BIT
    #include <stdio.h>
    #include <string.h>
    #include <time.h>
    #include "utils/logging.h"
#endif

/**
 * @brief max size of the decoded weather block, 38 base + 5 + 25 * 6 hourly + 1 + 7 * 6 daily
 */
#define WEATHER_BLE_MAX_DATA        256
/**
 * @brief hours between two forecast slots, the same grid openweathermap delivers
 */
#define WEATHER_BLE_SLOT_HOURS      3
/**
 * @brief how far a hourly entry may be away from a slot to be used
 */
#define WEATHER_BLE_SLOT_TOLERANCE  3600

static bool weather_ble_connected = false;

static bool weather_ble_gadgetbridge_event_cb( EventBits_t event, void *arg );
static void weather_ble_decode_v1( BluetoothJsonRequest &request );
static void weather_ble_decode_v2( BluetoothJsonRequest &request );

void weather_ble_setup( void ) {
    weather_ble_connected = false;
    gadgetbridge_register_cb( GADGETBRIDGE_CONNECT | GADGETBRIDGE_DISCONNECT | GADGETBRIDGE_JSON_MSG, weather_ble_gadgetbridge_event_cb, "weather ble" );
}

void weather_ble_request( void ) {
    if ( !weather_ble_connected )
        return;

    if ( gadgetbridge_send_msg( "\r\n{\"t\":\"weather\",\"v\":2,\"f\":true}\r\n" ) ) {
        log_d("weather over ble requested");
    }
}

/**
 * @brief decode base64
 *
 * @return number of decoded bytes
 */
static size_t weather_ble_base64_decode( const char *in, uint8_t *out, size_t out_size ) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t buffer = 0;
    int bits = 0;
    size_t len = 0;

    while ( *in ) {
        char c = *in++;

        if ( c == '=' )
            break;

        const char *pos = strchr( alphabet, c );
        if ( !pos )
            continue;

        buffer = ( buffer << 6 ) | ( uint32_t )( pos - alphabet );
        bits += 6;

        if ( bits >= 8 ) {
            bits -= 8;
            if ( len >= out_size )
                break;
            out[ len++ ] = ( buffer >> bits ) & 0xff;
        }
    }

    return( len );
}

static uint8_t rd_u8( const uint8_t *data, size_t len, size_t pos ) {
    return( pos < len ? data[ pos ] : 0 );
}

static int8_t rd_i8( const uint8_t *data, size_t len, size_t pos ) {
    return( ( int8_t )rd_u8( data, len, pos ) );
}

static uint16_t rd_u16( const uint8_t *data, size_t len, size_t pos ) {
    return( ( uint16_t )rd_u8( data, len, pos ) | ( ( uint16_t )rd_u8( data, len, pos + 1 ) << 8 ) );
}

static uint32_t rd_u32( const uint8_t *data, size_t len, size_t pos ) {
    return( ( uint32_t )rd_u8( data, len, pos ) |
            ( ( uint32_t )rd_u8( data, len, pos + 1 ) << 8 ) |
            ( ( uint32_t )rd_u8( data, len, pos + 2 ) << 16 ) |
            ( ( uint32_t )rd_u8( data, len, pos + 3 ) << 24 ) );
}

/**
 * @brief resolve the bucket index gadgetbridge sends into a real owm condition code
 */
static uint16_t weather_ble_map_code( uint8_t code ) {
    static const uint16_t codes[ 7 ][ 11 ] = {
        { 200, 201, 202, 210, 211, 212, 221, 230, 231, 232, 0 },
        { 300, 301, 302, 310, 311, 312, 313, 314, 321, 0, 0 },
        { 0 },
        { 500, 501, 502, 503, 504, 511, 520, 521, 522, 531, 0 },
        { 600, 601, 602, 611, 612, 613, 615, 616, 620, 621, 622 },
        { 701, 711, 721, 731, 741, 751, 761, 762, 771, 781, 0 },
        { 800, 801, 802, 803, 804, 0, 0, 0, 0, 0, 0 }
    };
    static const uint8_t sizes[ 7 ] = { 10, 9, 0, 10, 11, 10, 5 };

    uint8_t group = code >> 5;
    uint8_t index = code & 0x1f;

    if ( group > 6 || index >= sizes[ group ] ) {
        log_d("unknown weather code %d", code );
        return( 800 );
    }

    return( codes[ group ][ index ] );
}

/**
 * @brief day or night for a timestamp, compared by hour of day
 */
static bool weather_ble_is_night( time_t timestamp, time_t sunrise, time_t sunset ) {
    struct tm info;
    int rise = 6;
    int set = 20;

    if ( sunrise > 0 && sunset > 0 ) {
        localtime_r( &sunrise, &info );
        rise = info.tm_hour;
        localtime_r( &sunset, &info );
        set = info.tm_hour;
    }

    localtime_r( &timestamp, &info );

    return( info.tm_hour < rise || info.tm_hour >= set );
}

static void weather_ble_temp( char *dest, size_t len, float celsius, weather_config_t *weather_config ) {
    if ( weather_config->imperial )
        snprintf( dest, len, "%0.1f°F", celsius * 9.0f / 5.0f + 32.0f );
    else
        snprintf( dest, len, "%0.1f°C", celsius );
}

/**
 * @brief gadgetbridge sends km/h while openweathermap sends m/s or mph.
 *        convert so the number means the same no matter where it came from.
 */
static void weather_ble_wind( weather_forcast_t *container, float kmh, int degree, weather_config_t *weather_config ) {
    weather_wind_to_string( container, ( int )( weather_config->imperial ? kmh / 1.609344f : kmh / 3.6f ), degree );
}

static bool weather_ble_gadgetbridge_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case GADGETBRIDGE_CONNECT:      weather_ble_connected = true;
                                        weather_ble_request();
                                        break;
        case GADGETBRIDGE_DISCONNECT:   weather_ble_connected = false;
                                        break;
        case GADGETBRIDGE_JSON_MSG: {
                                        BluetoothJsonRequest &request = *(BluetoothJsonRequest*)arg;

                                        if ( !request.isEqualKeyValue( "t", "weather" ) )
                                            break;

                                        if ( request.containsKey("d") )
                                            weather_ble_decode_v2( request );
                                        else if ( request.containsKey("temp") )
                                            weather_ble_decode_v1( request );

                                        break;
                                    }
    }
    return( true );
}

/**
 * @brief older gadgetbridge and some providers send plain json with kelvin temperatures
 *        and a real owm condition code, current weather only
 */
static void weather_ble_decode_v1( BluetoothJsonRequest &request ) {
    weather_config_t *weather_config = weather_get_config();
    weather_forcast_t *today = weather_get_today();
    const char *location = request["loc"] | "";
    time_t now = time( NULL );

    today->valide = true;
    today->timestamp = now;
    weather_ble_temp( today->temp, sizeof( today->temp ), request["temp"].as<float>() - 273.15f, weather_config );
    snprintf( today->humidity, sizeof( today->humidity ), "%f%%", request["hum"] | 0.0f );
    today->pressure[ 0 ] = '\0';
    snprintf( today->name, sizeof( today->name ), "%s", location );
    resolve_owm_iconname( request["code"] | 800, weather_ble_is_night( now, 0, 0 ), today->icon, sizeof( today->icon ) );
    weather_ble_wind( today, request["wind"] | 0.0f, request["wdir"] | 0, weather_config );

    log_d("weather v1: %s, %s, %s", today->name, today->temp, today->icon );

    weather_widget_update();
}

/**
 * @brief gadgetbridge 0.86 and newer, binary block in a base64 string
 */
static void weather_ble_decode_v2( BluetoothJsonRequest &request ) {
    weather_config_t *weather_config = weather_get_config();
    weather_forcast_t *today = weather_get_today();
    weather_forcast_t *forecast = weather_forecast_get_data();
    const char *payload = request["d"] | "";
    const char *location = request["l"] | "";
    uint8_t data[ WEATHER_BLE_MAX_DATA ];
    time_t now = time( NULL );

    size_t len = weather_ble_base64_decode( payload, data, sizeof( data ) );
    if ( len < 38 ) {
        log_d("weather v2 block too short (%d of %d bytes), ignore", (int)len, (int)strlen( payload ) );
        return;
    }

    time_t sunrise = ( time_t )rd_u32( data, len, 19 );
    time_t sunset = ( time_t )rd_u32( data, len, 23 );

    today->valide = true;
    today->timestamp = now;
    weather_ble_temp( today->temp, sizeof( today->temp ), rd_i8( data, len, 0 ), weather_config );
    snprintf( today->humidity, sizeof( today->humidity ), "%f%%", ( float )rd_u8( data, len, 3 ) );
    snprintf( today->pressure, sizeof( today->pressure ), "%fpha", rd_u16( data, len, 12 ) / 10.0f );
    snprintf( today->name, sizeof( today->name ), "%s", location );
    resolve_owm_iconname( weather_ble_map_code( rd_u8( data, len, 6 ) ), weather_ble_is_night( now, sunrise, sunset ), today->icon, sizeof( today->icon ) );
    weather_ble_wind( today, rd_u16( data, len, 7 ) / 100.0f, rd_u16( data, len, 9 ), weather_config );

    log_d("weather v2: %s, %s, %s, %d bytes", today->name, today->temp, today->icon, (int)len );

    weather_widget_update();

    /**
     * the hourly block is optional, it only arrives when it was requested with "f":true
     */
    size_t hours = rd_u8( data, len, 38 );
    time_t base = ( time_t )rd_u32( data, len, 39 );
    size_t arrays = 43;
    int filled = 0;

    if ( !forecast || len < 43 || hours == 0 || len < arrays + hours * 6 ) {
        log_d("weather v2 without forecast, %d hours in %d bytes", (int)hours, (int)len );
        return;
    }

    for ( int slot = 0 ; slot < WEATHER_MAX_FORECAST ; slot++ ) {
        time_t target = now + ( slot + 1 ) * WEATHER_BLE_SLOT_HOURS * 3600;
        time_t best_diff = 0;
        int best = -1;

        for ( size_t hour = 0 ; hour < hours ; hour++ ) {
            time_t entry = base + rd_u8( data, len, arrays + hour ) * 360;
            time_t diff = entry > target ? entry - target : target - entry;

            if ( best < 0 || diff < best_diff ) {
                best_diff = diff;
                best = (int)hour;
            }
        }
        /**
         * the phone sends at most 25 hours, bigger displays have more slots than that
         */
        if ( best < 0 || best_diff > WEATHER_BLE_SLOT_TOLERANCE )
            continue;

        time_t entry_time = base + rd_u8( data, len, arrays + best ) * 360;

        forecast[ slot ].valide = true;
        forecast[ slot ].timestamp = entry_time;
        filled++;
        weather_ble_temp( forecast[ slot ].temp, sizeof( forecast[ slot ].temp ), rd_i8( data, len, arrays + hours + best ), weather_config );
        snprintf( forecast[ slot ].name, sizeof( forecast[ slot ].name ), "%s", location );
        resolve_owm_iconname( weather_ble_map_code( rd_u8( data, len, arrays + hours * 2 + best ) ), weather_ble_is_night( entry_time, sunrise, sunset ), forecast[ slot ].icon, sizeof( forecast[ slot ].icon ) );
        weather_ble_wind( &forecast[ slot ], rd_u8( data, len, arrays + hours * 3 + best ), rd_u8( data, len, arrays + hours * 4 + best ) * 2, weather_config );
    }

    log_d("weather v2 forecast: %d hours, %d slots filled", (int)hours, filled );

    weather_forecast_update();
}
