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
#include "blealarm.h"

#include "hardware/ble/gadgetbridge.h"
#include "hardware/powermgm.h"
#include "hardware/rtcctl.h"
#include "utils/bluejsonrequest.h"

#ifdef NATIVE_64BIT
    #include <string.h>
    #include <time.h>
    #include "utils/logging.h"
    #include "utils/millis.h"
#else
    #include <Arduino.h>
#endif

#define BLEALARM_SAVE_DELAY             5000

static blealarm_config_t blealarm_config;
static bool blealarm_save_pending = false;
static uint32_t blealarm_save_since = 0;

static bool blealarm_gadgetbridge_event_cb( EventBits_t event, void *arg );
static bool blealarm_rtcctl_event_cb( EventBits_t event, void *arg );
static bool blealarm_powermgm_loop_cb( EventBits_t event, void *arg );
static void blealarm_receive( BluetoothJsonRequest &request );
static void blealarm_apply( void );
static void blealarm_save_later( void );

void blealarm_setup( void ) {
    blealarm_save_pending = false;
    blealarm_config.load();
    blealarm_apply();
    /**
     * the phone never asks and never answers, it only sends when the user
     * leaves the alarm list, so there is nothing to do on connect
     */
    gadgetbridge_register_cb( GADGETBRIDGE_JSON_MSG, blealarm_gadgetbridge_event_cb, "ble alarm" );
    rtcctl_register_cb( RTCCTL_ALARM_OCCURRED, blealarm_rtcctl_event_cb, "ble alarm" );
    powermgm_register_loop_cb( POWERMGM_WAKEUP | POWERMGM_SILENCE_WAKEUP | POWERMGM_STANDBY, blealarm_powermgm_loop_cb, "ble alarm loop" );
}

/**
 * @brief hand the stored terms over to rtcctl, it holds the next one of all sources
 */
static void blealarm_apply( void ) {
    rtcctl_alarm_term_t terms[ BLEALARM_MAX_ALARMS ];
    size_t count = blealarm_config.count < BLEALARM_MAX_ALARMS ? blealarm_config.count : BLEALARM_MAX_ALARMS;

    for ( size_t index = 0 ; index < count ; index++ ) {
        terms[ index ].enabled = true;
        terms[ index ].hour = blealarm_config.alarm[ index ].hour;
        terms[ index ].minute = blealarm_config.alarm[ index ].minute;

        for ( int day = 0 ; day < DAYS_IN_WEEK ; day++ )
            terms[ index ].week_days[ day ] = ( ( blealarm_config.alarm[ index ].week_days >> day ) & 1 ) != 0;
    }

    rtcctl_set_ext_alarms( terms, count );
}

/**
 * @brief take over the whole alarm list, an empty list clears it
 */
static void blealarm_receive( BluetoothJsonRequest &request ) {
    uint8_t count = 0;

    for ( JsonVariant entry : request["d"].as<JsonArray>() ) {
        if ( count >= BLEALARM_MAX_ALARMS )
            break;

        if ( !entry.containsKey("h") || !entry.containsKey("m") )
            continue;
        /**
         * a missing "on" is an enabled alarm, a disabled one has no use without ui
         */
        if ( !( entry["on"] | true ) )
            continue;

        int rep = entry["rep"] | 0;
        blealarm_alarm_t *alarm = &blealarm_config.alarm[ count ];

        alarm->hour = entry["h"].as<int>();
        alarm->minute = entry["m"].as<int>();
        alarm->week_days = 0;
        alarm->once = ( rep == 0 );
        /**
         * gadgetbridge counts the days from monday, week_days from sunday
         */
        for ( int day = 0 ; day < DAYS_IN_WEEK ; day++ ) {
            if ( rep & ( 1 << day ) )
                alarm->week_days |= 1 << ( ( day + 1 ) % DAYS_IN_WEEK );
        }

        log_i("ble alarm: %02d:%02d, days 0x%02x%s", alarm->hour, alarm->minute, alarm->week_days, alarm->once ? ", once" : "" );
        count++;
    }

    blealarm_config.count = count;
    blealarm_save_later();
    blealarm_apply();
}

static bool blealarm_gadgetbridge_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case GADGETBRIDGE_JSON_MSG: {
                                        BluetoothJsonRequest &request = *(BluetoothJsonRequest*)arg;

                                        if ( !request.isEqualKeyValue( "t", "alarm" ) || !request.containsKey("d") )
                                            break;

                                        blealarm_receive( request );
                                        break;
                                    }
    }
    return( true );
}

static bool blealarm_rtcctl_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case RTCCTL_ALARM_OCCURRED: {
                                        struct tm term_tm;
                                        uint8_t count = 0;
                                        time_t term = rtcctl_get_last_alarm_time();

                                        if ( !term )
                                            time( &term );

                                        localtime_r( &term, &term_tm );
                                        /**
                                         * drop the one shot terms of this minute, rtcctl rearms after us
                                         */
                                        for ( uint8_t index = 0 ; index < blealarm_config.count ; index++ ) {
                                            blealarm_alarm_t *alarm = &blealarm_config.alarm[ index ];

                                            if ( alarm->once && alarm->hour == term_tm.tm_hour && alarm->minute == term_tm.tm_min ) {
                                                log_i("ble alarm %02d:%02d expired", alarm->hour, alarm->minute );
                                                continue;
                                            }

                                            blealarm_config.alarm[ count ] = *alarm;
                                            count++;
                                        }

                                        if ( count == blealarm_config.count )
                                            break;

                                        blealarm_config.count = count;
                                        blealarm_save_later();
                                        blealarm_apply();
                                        break;
                                    }
    }
    return( true );
}

static void blealarm_save_later( void ) {
    blealarm_save_pending = true;
    blealarm_save_since = millis();
}

static bool blealarm_powermgm_loop_cb( EventBits_t event, void *arg ) {
    if ( blealarm_save_pending && millis() - blealarm_save_since >= BLEALARM_SAVE_DELAY ) {
        blealarm_save_pending = false;
        blealarm_config.save();
    }
    return( true );
}

blealarm_config_t::blealarm_config_t() : BaseJsonConfig( BLEALARM_CONFIG_FILE ) {
    count = 0;
    memset( alarm, 0, sizeof( alarm ) );
}

bool blealarm_config_t::onSave( JsonDocument& doc ) {
    doc["version"] = 1;

    JsonArray list = doc.createNestedArray("a");
    for ( uint8_t index = 0 ; index < count && index < BLEALARM_MAX_ALARMS ; index++ ) {
        JsonObject entry = list.createNestedObject();
        entry["h"] = alarm[ index ].hour;
        entry["m"] = alarm[ index ].minute;
        entry["w"] = alarm[ index ].week_days;
        entry["o"] = alarm[ index ].once;
    }

    return( true );
}

bool blealarm_config_t::onLoad( JsonDocument& doc ) {
    count = 0;

    for ( JsonVariant entry : doc["a"].as<JsonArray>() ) {
        if ( count >= BLEALARM_MAX_ALARMS )
            break;

        alarm[ count ].hour = entry["h"] | 0;
        alarm[ count ].minute = entry["m"] | 0;
        alarm[ count ].week_days = entry["w"] | 0;
        alarm[ count ].once = entry["o"] | false;
        count++;
    }

    log_i("%d ble alarms restored", count );

    return( true );
}

bool blealarm_config_t::onDefault( void ) {
    count = 0;

    return( true );
}
