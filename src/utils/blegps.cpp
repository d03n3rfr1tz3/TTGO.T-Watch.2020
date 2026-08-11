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
#include "blegps.h"
#include "hardware/gpsctl.h"
#include "hardware/ble/gadgetbridge.h"
#include "utils/bluejsonrequest.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#endif

/**
 * @brief distance in km before apps get a new location
 */
#define BLEGPS_APP_LOCATION_DISTANCE    1.0

static bool blegps_connected = false;
static bool blegps_gps_on = false;
static bool blegps_requested = false;
static bool blegps_have_last = false;
static double blegps_last_lat = 0;
static double blegps_last_lon = 0;

static bool blegps_gadgetbridge_event_cb( EventBits_t event, void *arg );
static bool blegps_gpsctl_event_cb( EventBits_t event, void *arg );
static void blegps_send_gps_power( bool status );
static void blegps_update( void );

void blegps_setup( void ) {
    blegps_connected = false;
    blegps_gps_on = false;
    blegps_requested = false;
    blegps_have_last = false;
    gadgetbridge_register_cb( GADGETBRIDGE_CONNECT | GADGETBRIDGE_DISCONNECT | GADGETBRIDGE_JSON_MSG, blegps_gadgetbridge_event_cb, "blegps" );
    gpsctl_register_cb( GPSCTL_ENABLE | GPSCTL_DISABLE | GPSCTL_UPDATE_CONFIG, blegps_gpsctl_event_cb, "blegps" );
}

/**
 * @brief tell the phone if we want location updates or not.
 */
static void blegps_send_gps_power( bool status ) {
    if ( gadgetbridge_send_msg( "\r\n{\"t\":\"gps_power\",\"status\":%s}\r\n", status ? "true" : "false" ) ) {
        blegps_requested = status;
        log_d("gps over ble %s", status ? "requested" : "canceled" );
    }
}

/**
 * @brief ask the phone for location updates or cancel them on state change.
 */
static void blegps_update( void ) {
    bool request = blegps_gps_on && gpsctl_get_gps_over_ble();

    if ( !blegps_connected || request == blegps_requested )
        return;

    blegps_send_gps_power( request );
}

static bool blegps_gpsctl_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case GPSCTL_ENABLE:         blegps_gps_on = true;
                                    blegps_update();
                                    break;
        case GPSCTL_DISABLE:        blegps_gps_on = false;
                                    blegps_have_last = false;
                                    blegps_update();
                                    break;
        case GPSCTL_UPDATE_CONFIG:  blegps_update();
                                    break;
    }
    return( true );
}

static bool blegps_gadgetbridge_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case GADGETBRIDGE_CONNECT:      blegps_connected = true;
                                        blegps_requested = false;
                                        blegps_update();
                                        break;
        case GADGETBRIDGE_DISCONNECT:   blegps_connected = false;
                                        blegps_requested = false;
                                        blegps_have_last = false;
                                        break;
        case GADGETBRIDGE_JSON_MSG: {
                                        BluetoothJsonRequest &request = *(BluetoothJsonRequest*)arg;

                                        /**
                                         * the phone asks if we want gps, answer even if nothing changed
                                         */
                                        if ( request.isEqualKeyValue( "t", "is_gps_active" ) ) {
                                            blegps_send_gps_power( blegps_gps_on && gpsctl_get_gps_over_ble() );
                                            break;
                                        }

                                        if ( !gpsctl_get_gps_over_ble() )
                                            break;

                                        if ( request.isEqualKeyValue( "t", "gps" ) && request.containsKey("lat") && request.containsKey("lon") ) {
                                            double lat = request["lat"];
                                            double lon = request["lon"];
                                            double altitude = request["alt"] | 0.0;
                                            double speed = request["speed"] | 0.0;
                                            /**
                                             * every app location writes the weather config to spiffs and reloads map tiles,
                                             * so only do that for the first fix and after a real move
                                             */
                                            bool app_location = !blegps_have_last || gpsctl_distance( blegps_last_lat, blegps_last_lon, lat, lon, EARTH_RADIUS_KM ) > BLEGPS_APP_LOCATION_DISTANCE;
                                            if ( app_location ) {
                                                blegps_last_lat = lat;
                                                blegps_last_lon = lon;
                                                blegps_have_last = true;
                                            }
                                            log_d("new lat/lon: %f/%f, altitude: %f, speed: %f", lat, lon, altitude, speed );
                                            gpsctl_set_location( lat, lon, altitude, speed, GPS_SOURCE_USER, app_location );
                                        }
                                        break;
                                    }
    }
    return( true );
}
