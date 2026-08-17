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
#include "blecalendar.h"

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
#define BLECALENDAR_SYNC_DELAY          3000
/**
 * @brief silence between two sync requests
 */
#define BLECALENDAR_SYNC_INTERVAL       ( 30 * 60 * 1000 )
/**
 * @brief interval for the expire and reminder check
 */
#define BLECALENDAR_TICK                60000
/**
 * @brief how long before an event the reminder fires
 */
#define BLECALENDAR_REMINDER_LEAD       ( 15 * 60 )
/**
 * @brief a full sync arrives as a burst of single messages, collect them into one write
 */
#define BLECALENDAR_SAVE_DELAY          5000

static blecalendar_event_t *blecalendar_events = NULL;
static blecalendar_config_t blecalendar_config;
static uint32_t blecalendar_sync_since = 0;
static uint32_t blecalendar_sync_delay = 0;
static uint32_t blecalendar_tick_since = 0;
static bool blecalendar_save_pending = false;
static uint32_t blecalendar_save_since = 0;

static bool blecalendar_gadgetbridge_event_cb( EventBits_t event, void *arg );
static bool blecalendar_powermgm_loop_cb( EventBits_t event, void *arg );
static void blecalendar_save_later( void );
static void blecalendar_send_sync( void );
static void blecalendar_add( BluetoothJsonRequest &request );
static void blecalendar_remove( int64_t id );
static int blecalendar_find( int64_t id );
static int blecalendar_alloc( time_t start );
static bool blecalendar_is_day( time_t start, int year, int month, int day );
static void blecalendar_remind( blecalendar_event_t *event );

void blecalendar_setup( void ) {
    blecalendar_events = ( blecalendar_event_t* )CALLOC_ASSERT( BLECALENDAR_MAX_EVENTS, sizeof( blecalendar_event_t ), "calendar ble event table calloc failed" );
    blecalendar_sync_delay = 0;
    blecalendar_save_pending = false;
    blecalendar_tick_since = ( uint32_t )millis() - BLECALENDAR_TICK;
    /**
     * gadgetbridge only sends what it thinks we do not have yet, so we have to keep it
     */
    blecalendar_config.events = blecalendar_events;
    blecalendar_config.load( BLECALENDAR_JSON_SIZE );

    gadgetbridge_register_cb( GADGETBRIDGE_CONNECT | GADGETBRIDGE_JSON_MSG, blecalendar_gadgetbridge_event_cb, "calendar ble" );
    powermgm_register_loop_cb( POWERMGM_WAKEUP | POWERMGM_SILENCE_WAKEUP | POWERMGM_STANDBY, blecalendar_powermgm_loop_cb, "calendar ble loop" );
}

const blecalendar_event_t *blecalendar_get_event( int slot ) {
    if ( !blecalendar_events || slot < 0 || slot >= BLECALENDAR_MAX_EVENTS )
        return( NULL );

    return( blecalendar_events[ slot ].used ? &blecalendar_events[ slot ] : NULL );
}

bool blecalendar_has_day( int year, int month, int day ) {
    if ( !blecalendar_events )
        return( false );

    for ( int slot = 0 ; slot < BLECALENDAR_MAX_EVENTS ; slot++ ) {
        if ( blecalendar_events[ slot ].used && blecalendar_is_day( blecalendar_events[ slot ].start, year, month, day ) )
            return( true );
    }

    return( false );
}

int blecalendar_get_day_events( int year, int month, int day, int *slots, int max ) {
    int count = 0;

    if ( !blecalendar_events || !slots )
        return( 0 );

    for ( int slot = 0 ; slot < BLECALENDAR_MAX_EVENTS && count < max ; slot++ ) {
        if ( !blecalendar_events[ slot ].used || !blecalendar_is_day( blecalendar_events[ slot ].start, year, month, day ) )
            continue;
        /**
         * insert sorted by start time, the slot order is the arrival order
         */
        int pos = count;
        while ( pos > 0 && blecalendar_events[ slots[ pos - 1 ] ].start > blecalendar_events[ slot ].start ) {
            slots[ pos ] = slots[ pos - 1 ];
            pos--;
        }
        slots[ pos ] = slot;
        count++;
    }

    return( count );
}

static bool blecalendar_is_day( time_t start, int year, int month, int day ) {
    struct tm event_tm;

    localtime_r( &start, &event_tm );

    return( event_tm.tm_year + 1900 == year && event_tm.tm_mon + 1 == month && event_tm.tm_mday == day );
}

static int blecalendar_find( int64_t id ) {
    for ( int slot = 0 ; slot < BLECALENDAR_MAX_EVENTS ; slot++ ) {
        if ( blecalendar_events[ slot ].used && blecalendar_events[ slot ].id == id )
            return( slot );
    }

    return( -1 );
}

/**
 * @brief get a free slot.
 * drop the most distant event if we are full.
 */
static int blecalendar_alloc( time_t start ) {
    int latest = -1;

    for ( int slot = 0 ; slot < BLECALENDAR_MAX_EVENTS ; slot++ ) {
        if ( !blecalendar_events[ slot ].used )
            return( slot );

        if ( latest < 0 || blecalendar_events[ slot ].start > blecalendar_events[ latest ].start )
            latest = slot;
    }

    return( blecalendar_events[ latest ].start > start ? latest : -1 );
}

static void blecalendar_add( BluetoothJsonRequest &request ) {
    if ( !request.containsKey("id") || !request.containsKey("timestamp") )
        return;

    int64_t id = request["id"].as<long long>();
    time_t start = ( time_t )request["timestamp"].as<long long>();
    int slot = blecalendar_find( id );

    if ( slot < 0 )
        slot = blecalendar_alloc( start );

    if ( slot < 0 ) {
        log_d("calendar event table full, drop event %lld", (long long)id );
        return;
    }

    blecalendar_event_t *event = &blecalendar_events[ slot ];

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

    blecalendar_save_later();
    log_i("calendar event: %s, %d", event->title, (int)start );
}

static void blecalendar_remove( int64_t id ) {
    int slot = blecalendar_find( id );

    if ( slot < 0 )
        return;

    blecalendar_events[ slot ].used = false;
    blecalendar_save_later();
    log_i("calendar event removed: %lld", (long long)id );
}

/**
 * @brief ask the phone for a full sync
 * gadgetbridge answers with every event we did not name in "ids".
 */
static void blecalendar_send_sync( void ) {
    char *ids = ( char* )MALLOC_ASSERT( 1024, "calendar ble id list malloc failed" );
    size_t pos = 0;
    int count = 0;

    ids[ 0 ] = '\0';
    for ( int slot = 0 ; slot < BLECALENDAR_MAX_EVENTS && pos < 1000 ; slot++ ) {
        if ( !blecalendar_events[ slot ].used )
            continue;

        pos += snprintf( ids + pos, 1024 - pos, "%s%lld", count ? "," : "", (long long)blecalendar_events[ slot ].id );
        count++;
    }

    if ( gadgetbridge_send_msg( "\r\n{\"t\":\"force_calendar_sync\",\"ids\":[%s]}\r\n", ids ) ) {
        log_i("calendar sync requested, %d known events", count );
    }

    free( ids );
}

static void blecalendar_remind( blecalendar_event_t *event ) {
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

static bool blecalendar_gadgetbridge_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case GADGETBRIDGE_CONNECT:      blecalendar_sync_since = millis();
                                        blecalendar_sync_delay = BLECALENDAR_SYNC_DELAY;
                                        break;
        case GADGETBRIDGE_JSON_MSG: {
                                        BluetoothJsonRequest &request = *(BluetoothJsonRequest*)arg;

                                        if ( request.isEqualKeyValue( "t", "calendar" ) ) {
                                            blecalendar_add( request );
                                        }
                                        else if ( request.isEqualKeyValue( "t", "calendar-" ) && request.containsKey("id") ) {
                                            if ( request["id"].is<JsonArray>() ) {
                                                for ( JsonVariant id : request["id"].as<JsonArray>() )
                                                    blecalendar_remove( id.as<long long>() );
                                            }
                                            else {
                                                blecalendar_remove( request["id"].as<long long>() );
                                            }
                                        }
                                        break;
                                    }
    }
    return( true );
}

static void blecalendar_save_later( void ) {
    blecalendar_save_pending = true;
    blecalendar_save_since = millis();
}

static bool blecalendar_powermgm_loop_cb( EventBits_t event, void *arg ) {
    if ( !blecalendar_events )
        return( true );

    if ( blecalendar_sync_delay && millis() - blecalendar_sync_since >= blecalendar_sync_delay ) {
        blecalendar_sync_since = millis();
        blecalendar_sync_delay = BLECALENDAR_SYNC_INTERVAL;
        blecalendar_send_sync();
    }

    if ( blecalendar_save_pending && millis() - blecalendar_save_since >= BLECALENDAR_SAVE_DELAY ) {
        blecalendar_save_pending = false;
        blecalendar_config.save( BLECALENDAR_JSON_SIZE );
    }

    if ( millis() - blecalendar_tick_since < BLECALENDAR_TICK )
        return( true );

    blecalendar_tick_since = millis();

    time_t now = time( NULL );
    for ( int slot = 0 ; slot < BLECALENDAR_MAX_EVENTS ; slot++ ) {
        blecalendar_event_t *entry = &blecalendar_events[ slot ];

        if ( !entry->used )
            continue;

        if ( now > entry->start + ( time_t )entry->duration ) {
            entry->used = false;
            blecalendar_save_later();
            continue;
        }

        if ( !entry->allday && !entry->reminded && now >= entry->start - BLECALENDAR_REMINDER_LEAD ) {
            entry->reminded = true;
            blecalendar_save_later();
            blecalendar_remind( entry );
        }
    }

    return( true );
}

blecalendar_config_t::blecalendar_config_t() : BaseJsonConfig( BLECALENDAR_CONFIG_FILE ) {
    prettyJson = false;
}

bool blecalendar_config_t::onSave( JsonDocument& doc ) {
    if ( !events )
        return( false );

    doc["version"] = 1;

    JsonArray list = doc.createNestedArray("e");
    for ( int slot = 0 ; slot < BLECALENDAR_MAX_EVENTS ; slot++ ) {
        if ( !events[ slot ].used )
            continue;

        JsonObject entry = list.createNestedObject();
        entry["id"] = (long long)events[ slot ].id;
        entry["ts"] = (long long)events[ slot ].start;
        entry["du"] = events[ slot ].duration;
        entry["ad"] = events[ slot ].allday;
        entry["re"] = events[ slot ].reminded;
        entry["ti"] = events[ slot ].title;
        entry["lo"] = events[ slot ].location;
        entry["ca"] = events[ slot ].calname;
        entry["de"] = events[ slot ].description;
    }

    log_i("%d calendar events stored", list.size() );

    return( true );
}

bool blecalendar_config_t::onLoad( JsonDocument& doc ) {
    time_t now = time( NULL );
    int slot = 0;

    if ( !events )
        return( false );

    for ( JsonVariant entry : doc["e"].as<JsonArray>() ) {
        if ( slot >= BLECALENDAR_MAX_EVENTS )
            break;

        time_t start = ( time_t )entry["ts"].as<long long>();
        uint32_t duration = entry["du"] | 0;
        /**
         * an event we would drop in the next tick anyway, on an unsynced clock
         * now is 1970 and nothing is dropped
         */
        if ( now > start + ( time_t )duration )
            continue;

        events[ slot ].used = true;
        events[ slot ].id = entry["id"].as<long long>();
        events[ slot ].start = start;
        events[ slot ].duration = duration;
        events[ slot ].allday = entry["ad"] | false;
        events[ slot ].reminded = entry["re"] | false;
        snprintf( events[ slot ].title, sizeof( events[ slot ].title ), "%s", entry["ti"] | "" );
        snprintf( events[ slot ].location, sizeof( events[ slot ].location ), "%s", entry["lo"] | "" );
        snprintf( events[ slot ].calname, sizeof( events[ slot ].calname ), "%s", entry["ca"] | "" );
        snprintf( events[ slot ].description, sizeof( events[ slot ].description ), "%s", entry["de"] | "" );
        slot++;
    }

    log_i("%d calendar events restored", slot );

    return( true );
}

bool blecalendar_config_t::onDefault( void ) {
    return( true );
}
