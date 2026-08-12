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
#include "calendar_ble.h"

#include "gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.h"
#include "hardware/ble/gadgetbridge.h"
#include "hardware/motor.h"
#include "hardware/powermgm.h"
#include "utils/alloc.h"
#include "utils/bluejsonrequest.h"

#ifdef NATIVE_64BIT
    #include <stdio.h>
    #include <string.h>
    #include "utils/logging.h"
    #include "utils/millis.h"
#else
    #include <Arduino.h>
#endif

/**
 * @brief how long we wait after connect before we ask, gadgetbridge sends time and
 *        notifications first and the connect event comes from the nimble host task
 */
#define CALENDAR_BLE_SYNC_DELAY         3000
/**
 * @brief silence between two sync requests
 */
#define CALENDAR_BLE_SYNC_INTERVAL      ( 30 * 60 * 1000 )
/**
 * @brief interval for the expire and reminder check
 */
#define CALENDAR_BLE_TICK               60000
/**
 * @brief how long before an event the reminder fires
 */
#define CALENDAR_BLE_REMINDER_LEAD      ( 15 * 60 )

static calendar_ble_event_t *calendar_ble_events = NULL;
static uint64_t calendar_ble_next_sync = 0;
static uint64_t calendar_ble_next_tick = 0;

static bool calendar_ble_gadgetbridge_event_cb( EventBits_t event, void *arg );
static bool calendar_ble_powermgm_loop_cb( EventBits_t event, void *arg );
static void calendar_ble_send_sync( void );
static void calendar_ble_add( BluetoothJsonRequest &request );
static void calendar_ble_remove( int64_t id );
static int calendar_ble_find( int64_t id );
static int calendar_ble_alloc( time_t start );
static bool calendar_ble_is_day( time_t start, int year, int month, int day );
static void calendar_ble_remind( calendar_ble_event_t *event );

void calendar_ble_setup( void ) {
    calendar_ble_events = ( calendar_ble_event_t* )CALLOC_ASSERT( CALENDAR_BLE_MAX_EVENTS, sizeof( calendar_ble_event_t ), "calendar ble event table calloc failed" );
    calendar_ble_next_sync = 0;
    calendar_ble_next_tick = 0;

    gadgetbridge_register_cb( GADGETBRIDGE_CONNECT | GADGETBRIDGE_JSON_MSG, calendar_ble_gadgetbridge_event_cb, "calendar ble" );
    powermgm_register_loop_cb( POWERMGM_WAKEUP | POWERMGM_SILENCE_WAKEUP | POWERMGM_STANDBY, calendar_ble_powermgm_loop_cb, "calendar ble loop" );
}

const calendar_ble_event_t *calendar_ble_get_event( int slot ) {
    if ( !calendar_ble_events || slot < 0 || slot >= CALENDAR_BLE_MAX_EVENTS )
        return( NULL );

    return( calendar_ble_events[ slot ].used ? &calendar_ble_events[ slot ] : NULL );
}

bool calendar_ble_has_day( int year, int month, int day ) {
    if ( !calendar_ble_events )
        return( false );

    for ( int slot = 0 ; slot < CALENDAR_BLE_MAX_EVENTS ; slot++ ) {
        if ( calendar_ble_events[ slot ].used && calendar_ble_is_day( calendar_ble_events[ slot ].start, year, month, day ) )
            return( true );
    }

    return( false );
}

int calendar_ble_get_day_events( int year, int month, int day, int *slots, int max ) {
    int count = 0;

    if ( !calendar_ble_events || !slots )
        return( 0 );

    for ( int slot = 0 ; slot < CALENDAR_BLE_MAX_EVENTS && count < max ; slot++ ) {
        if ( !calendar_ble_events[ slot ].used || !calendar_ble_is_day( calendar_ble_events[ slot ].start, year, month, day ) )
            continue;
        /**
         * insert sorted by start time, the slot order is the arrival order
         */
        int pos = count;
        while ( pos > 0 && calendar_ble_events[ slots[ pos - 1 ] ].start > calendar_ble_events[ slot ].start ) {
            slots[ pos ] = slots[ pos - 1 ];
            pos--;
        }
        slots[ pos ] = slot;
        count++;
    }

    return( count );
}

static bool calendar_ble_is_day( time_t start, int year, int month, int day ) {
    struct tm event_tm;

    localtime_r( &start, &event_tm );

    return( event_tm.tm_year + 1900 == year && event_tm.tm_mon + 1 == month && event_tm.tm_mday == day );
}

static int calendar_ble_find( int64_t id ) {
    for ( int slot = 0 ; slot < CALENDAR_BLE_MAX_EVENTS ; slot++ ) {
        if ( calendar_ble_events[ slot ].used && calendar_ble_events[ slot ].id == id )
            return( slot );
    }

    return( -1 );
}

/**
 * @brief get a free slot.
 * drop the most distant event if we are full.
 */
static int calendar_ble_alloc( time_t start ) {
    int latest = -1;

    for ( int slot = 0 ; slot < CALENDAR_BLE_MAX_EVENTS ; slot++ ) {
        if ( !calendar_ble_events[ slot ].used )
            return( slot );

        if ( latest < 0 || calendar_ble_events[ slot ].start > calendar_ble_events[ latest ].start )
            latest = slot;
    }

    return( calendar_ble_events[ latest ].start > start ? latest : -1 );
}

static void calendar_ble_add( BluetoothJsonRequest &request ) {
    if ( !request.containsKey("id") || !request.containsKey("timestamp") )
        return;

    int64_t id = request["id"].as<long long>();
    time_t start = ( time_t )request["timestamp"].as<long long>();
    int slot = calendar_ble_find( id );

    if ( slot < 0 )
        slot = calendar_ble_alloc( start );

    if ( slot < 0 ) {
        log_d("calendar event table full, drop event %lld", (long long)id );
        return;
    }

    calendar_ble_event_t *event = &calendar_ble_events[ slot ];

    event->used = true;
    event->id = id;
    event->start = start;
    event->duration = request["durationInSeconds"] | 0;
    event->allday = request["allDay"] | false;
    snprintf( event->title, sizeof( event->title ), "%s", request["title"] | "" );
    snprintf( event->location, sizeof( event->location ), "%s", request["location"] | "" );
    snprintf( event->calname, sizeof( event->calname ), "%s", request["calName"] | "" );
    snprintf( event->description, sizeof( event->description ), "%s", request["description"] | "" );
    /**
     * a full sync delivers running events too, they must not buzz
     */
    event->reminded = time( NULL ) >= start;

    log_i("calendar event: %s, %d", event->title, (int)start );
}

static void calendar_ble_remove( int64_t id ) {
    int slot = calendar_ble_find( id );

    if ( slot < 0 )
        return;

    calendar_ble_events[ slot ].used = false;
    log_i("calendar event removed: %lld", (long long)id );
}

/**
 * @brief ask the phone for a full sync
 * gadgetbridge answers with every event we did not name in "ids".
 */
static void calendar_ble_send_sync( void ) {
    char *ids = ( char* )MALLOC_ASSERT( 1024, "calendar ble id list malloc failed" );
    size_t pos = 0;
    int count = 0;

    ids[ 0 ] = '\0';
    for ( int slot = 0 ; slot < CALENDAR_BLE_MAX_EVENTS && pos < 1000 ; slot++ ) {
        if ( !calendar_ble_events[ slot ].used )
            continue;

        pos += snprintf( ids + pos, 1024 - pos, "%s%lld", count ? "," : "", (long long)calendar_ble_events[ slot ].id );
        count++;
    }

    if ( gadgetbridge_send_msg( "\r\n{\"t\":\"force_calendar_sync\",\"ids\":[%s]}\r\n", ids ) ) {
        log_i("calendar sync requested, %d known events", count );
    }

    free( ids );
}

static void calendar_ble_remind( calendar_ble_event_t *event ) {
    char msg[ 320 ] = "";
    char body[ 96 ] = "";
    struct tm start_tm;
    SpiRamJsonDocument doc( 512 );

    localtime_r( &event->start, &start_tm );

    if ( event->location[ 0 ] )
        snprintf( body, sizeof( body ), "%02d:%02d - %s", start_tm.tm_hour, start_tm.tm_min, event->location );
    else
        snprintf( body, sizeof( body ), "%02d:%02d", start_tm.tm_hour, start_tm.tm_min );

    doc["t"] = "notify";
    doc["id"] = (long)millis();
    doc["src"] = "Calendar";
    doc["title"] = event->title;
    doc["body"] = body;
    
    if ( serializeJson( doc, msg, sizeof( msg ) ) ) {
        motor_vibe( 250 );
        bluetooth_message_queue_msg( msg );
        log_i("calendar reminder: %s, %s", event->title, body );
    }

    doc.clear();
}

static bool calendar_ble_gadgetbridge_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case GADGETBRIDGE_CONNECT:      calendar_ble_next_sync = millis() + CALENDAR_BLE_SYNC_DELAY;
                                        break;
        case GADGETBRIDGE_JSON_MSG: {
                                        BluetoothJsonRequest &request = *(BluetoothJsonRequest*)arg;

                                        if ( request.isEqualKeyValue( "t", "calendar" ) ) {
                                            calendar_ble_add( request );
                                        }
                                        else if ( request.isEqualKeyValue( "t", "calendar-" ) && request.containsKey("id") ) {
                                            if ( request["id"].is<JsonArray>() ) {
                                                for ( JsonVariant id : request["id"].as<JsonArray>() )
                                                    calendar_ble_remove( id.as<long long>() );
                                            }
                                            else {
                                                calendar_ble_remove( request["id"].as<long long>() );
                                            }
                                        }
                                        break;
                                    }
    }
    return( true );
}

static bool calendar_ble_powermgm_loop_cb( EventBits_t event, void *arg ) {
    if ( !calendar_ble_events )
        return( true );

    if ( calendar_ble_next_sync && calendar_ble_next_sync < millis() ) {
        calendar_ble_next_sync = millis() + CALENDAR_BLE_SYNC_INTERVAL;
        calendar_ble_send_sync();
    }

    if ( calendar_ble_next_tick > millis() )
        return( true );

    calendar_ble_next_tick = millis() + CALENDAR_BLE_TICK;

    time_t now = time( NULL );
    for ( int slot = 0 ; slot < CALENDAR_BLE_MAX_EVENTS ; slot++ ) {
        calendar_ble_event_t *entry = &calendar_ble_events[ slot ];

        if ( !entry->used )
            continue;

        if ( now > entry->start + ( time_t )entry->duration ) {
            entry->used = false;
            continue;
        }

        if ( !entry->allday && !entry->reminded && now >= entry->start - CALENDAR_BLE_REMINDER_LEAD ) {
            entry->reminded = true;
            calendar_ble_remind( entry );
        }
    }

    return( true );
}
