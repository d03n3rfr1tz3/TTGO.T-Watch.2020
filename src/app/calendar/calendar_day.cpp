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

#include "calendar.h"
#include "utils/blecalendar.h"
#include "calendar_db.h"
#include "calendar_day.h"
#include "calendar_detail.h"
#include "calendar_create.h"

#include "gui/mainbar/mainbar.h"
#include "gui/icon.h"
#include "gui/statusbar.h"
#include "gui/app.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

#include "utils/alloc.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
    #include "utils/millis.h"
    #include <string>
    #include <time.h>

    using namespace std;
    #define String string
#else
    #include <time.h>
    #include <Arduino.h>
#endif
/**
 * calendar tile store
 */
uint32_t calendar_day_tile_num;                 /** @brief allocated calendar overview tile number */
/**
 * calendar icon
 */
LV_FONT_DECLARE(Ubuntu_12px);                   /** @brief calendar font */
LV_FONT_DECLARE(Ubuntu_16px);                   /** @brief calendar font */
LV_FONT_DECLARE(Ubuntu_32px);                   /** @brief calendar font */

#if defined( BIG_THEME )
    lv_font_t *daylist_font = &Ubuntu_32px;
#elif defined( MID_THEME )
    lv_font_t *daylist_font = &Ubuntu_16px;
#else
    lv_font_t *daylist_font = &Ubuntu_12px;
#endif
/**
 * calendar objects
 */
lv_obj_t *calendar_day_list = NULL;
static lv_style_t calendar_day_list_style;      /** @brief calendar style object */
/**
 * internal variables
 */
static int calendar_day_year = 0;               /** @brief current year in calendar overview */
static int calendar_day_month = 0;              /** @brief current month in calendar overview */
static int calendar_day_day = 0;                /** @brief current day in calendar overview */
/**
 * @brief entries of the shown day
 */
#define CALENDAR_DAY_MAX_ENTRYS     64
typedef struct {
    int     hour;
    int     min;
    int     ble_slot;                           /** @brief ble store slot or -1 for a database entry */
    char    text[ 80 ];
} calendar_day_entry_t;

static calendar_day_entry_t *calendar_day_entry_table = NULL;
static int calendar_day_entrys = 0;
/**
 * internal function declaration
 */
static int calendar_day_overview_callback( void *data, int argc, char **argv, char **azColName );
static void calendar_day_add_entry( int hour, int min, int ble_slot, const char *text );
static void calendar_day_add_ble_entrys( void );
static void calendar_day_build_list( void );
void calendar_day_build_ui( void );
static void calendar_day_exit_event_cb( lv_obj_t * obj, lv_event_t event );
static void calendar_day_create_event_cb( lv_obj_t * obj, lv_event_t event );
static void calendar_day_edit_event_cb( lv_obj_t * obj, lv_event_t event );
static void calendar_day_ble_event_cb( lv_obj_t * obj, lv_event_t event );
void calendar_day_activate_cb( void );
void calendar_day_hibernate_cb( void );
/**
 * setup routine for application
 */
void calendar_day_setup( void ) {
    /**
     * register a tile
     */
    calendar_day_tile_num = mainbar_add_app_tile( 1, 1, "calendar day" );
    /**
     * table for the entries of the shown day
     */
    calendar_day_entry_table = ( calendar_day_entry_t* )CALLOC_ASSERT( CALENDAR_DAY_MAX_ENTRYS, sizeof( calendar_day_entry_t ), "calendar day entry table calloc failed" );
    /**
     * set activation/hibernation call back
     */
    mainbar_add_tile_activate_cb( calendar_day_tile_num, calendar_day_activate_cb );
    mainbar_add_tile_hibernate_cb( calendar_day_tile_num, calendar_day_hibernate_cb );
    /**
     * build calendar day ovweview ui
     */
    calendar_day_build_ui();
}

void calendar_day_build_ui( void ) {
    /**
     * get calendar object from tile number
     */
    lv_obj_t *calendar_day_tile = mainbar_get_tile_obj( calendar_day_tile_num );
    /**
     * set style for day date list
     */
    lv_style_init( &calendar_day_list_style );
    lv_style_set_border_width( &calendar_day_list_style , LV_OBJ_PART_MAIN, 0 );
    lv_style_set_radius( &calendar_day_list_style , LV_OBJ_PART_MAIN, 0 );
    lv_style_set_text_font( &calendar_day_list_style , LV_OBJ_PART_MAIN, daylist_font );
    lv_style_set_bg_opa( &calendar_day_list_style, LV_OBJ_PART_MAIN, LV_OPA_80 );
    /**
     * day date list
     */
    calendar_day_list = lv_list_create( calendar_day_tile, NULL );
    lv_obj_set_size( calendar_day_list, lv_disp_get_hor_res( NULL ), lv_disp_get_ver_res( NULL ) - THEME_ICON_SIZE );
    lv_obj_align( calendar_day_list, calendar_day_tile, LV_ALIGN_IN_TOP_MID, 0, 0);
    lv_obj_add_style( calendar_day_list, LV_OBJ_PART_MAIN, &calendar_day_list_style  );
    /**
     * add exit button
     */
    lv_obj_t *exit_button = wf_add_exit_button( calendar_day_tile, calendar_day_exit_event_cb );
    lv_obj_align( exit_button, calendar_day_tile, LV_ALIGN_IN_BOTTOM_LEFT, THEME_ICON_PADDING, -THEME_ICON_PADDING );
    /**
     * add exit button
     */
    lv_obj_t *create_button = wf_add_add_button( calendar_day_tile, calendar_day_create_event_cb );
    lv_obj_align( create_button, calendar_day_tile, LV_ALIGN_IN_BOTTOM_RIGHT, -THEME_ICON_PADDING, -THEME_ICON_PADDING );
}

static void calendar_day_exit_event_cb( lv_obj_t * obj, lv_event_t event ) {
    if ( event == LV_EVENT_CLICKED ) {
        mainbar_jump_back();
    }
}

static void calendar_day_create_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case LV_EVENT_CLICKED: {
            time_t now;
            struct tm time_tm;
            time( &now );
            localtime_r( &now, &time_tm );
            calendar_create_set_date( calendar_day_year, calendar_day_month, calendar_day_day );
            calendar_create_set_time( time_tm.tm_hour, time_tm.tm_min );
            calendar_create_clear_content();
            mainbar_jump_to_tilenumber( calendar_create_get_tile() , LV_ANIM_OFF );
            break;
        }
    }
}

static void calendar_day_ble_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case LV_EVENT_CLICKED:
            calendar_detail_show( ( int )( intptr_t )lv_obj_get_user_data( obj ) );
            break;
    }
}

static void calendar_day_edit_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case LV_EVENT_CLICKED: {
            char hour[ 3 ] = "";
            char min[ 3 ] = "";
            const char *time = lv_list_get_btn_text( obj );
            hour[ 0 ] = *time;
            time++;
            hour[ 1 ] = *time;
            time++;
            hour[ 2 ] = '\0';
            time++;
            min[ 0 ] = *time;
            time++;
            min[ 1 ] = *time;
            time++;
            min[ 2 ] = '\0';
            time++;
            calendar_create_set_date( calendar_day_year, calendar_day_month, calendar_day_day );
            calendar_create_set_time( atoi( hour ), atoi( min ) );
            calendar_create_set_content();
            mainbar_jump_to_tilenumber( calendar_create_get_tile() , LV_ANIM_OFF );
            CALENDAR_DAY_DEBUG_LOG("edit: %04d-%02d-%02d %02d:%02d", calendar_day_year, calendar_day_month, calendar_day_day, atoi( hour ), atoi( min ) );
            break;
        }
    }
}

void calendar_day_activate_cb( void ) {
    /**
     * open calendar date base
     */
    if ( !calendar_db_open() ) {
        log_e("open calendar date base failed");
    }
    /**
     * refresh day overview, ble events are shown even without a database
     */
    calendar_day_overview_refresh( calendar_day_year, calendar_day_month, calendar_day_day );
}

void calendar_day_hibernate_cb( void ) {
    /**
     * close calendar date base
     */
    calendar_db_close();
}

static int calendar_day_overview_callback( void *data, int argc, char **argv, char **azColName ) {
    String Result = "";
    char hour[8] = "";
    char min[8] = "";
    /**
     * count all key/values pairs
     */
    for ( int i = 0; i < argc ; i++ ) {
       /**
         * build an result string for one line presentation
         */
        Result += i != 0 ? "," : "";
        Result += azColName[i];
        Result += "=";
        Result += argv[i] ? argv[i] : "NULL";
    }
    CALENDAR_DAY_DEBUG_LOG("Result = %s", Result.c_str() );
    snprintf( hour, sizeof( hour ), "%02d", atoi( argv[ 0 ] ) );
    snprintf( min, sizeof( min ), "%02d", atoi( argv[ 1 ] ) );
    /**
     * collect the entry
     */
    String Date = (String) hour + ":" + min + " - " + argv[2];
    calendar_day_add_entry( atoi( argv[ 0 ] ), atoi( argv[ 1 ] ), -1, Date.c_str() );
    return( 0 );
}

/**
 * @brief collect one entry and keep the table sorted by time
 */
static void calendar_day_add_entry( int hour, int min, int ble_slot, const char *text ) {
    if ( calendar_day_entrys >= CALENDAR_DAY_MAX_ENTRYS ) {
        CALENDAR_DAY_DEBUG_LOG("day entry table full");
        return;
    }

    int pos = calendar_day_entrys;
    while ( pos > 0 && ( calendar_day_entry_table[ pos - 1 ].hour * 60 + calendar_day_entry_table[ pos - 1 ].min ) > ( hour * 60 + min ) ) {
        calendar_day_entry_table[ pos ] = calendar_day_entry_table[ pos - 1 ];
        pos--;
    }

    calendar_day_entry_table[ pos ].hour = hour;
    calendar_day_entry_table[ pos ].min = min;
    calendar_day_entry_table[ pos ].ble_slot = ble_slot;
    snprintf( calendar_day_entry_table[ pos ].text, sizeof( calendar_day_entry_table[ pos ].text ), "%s", text );
    calendar_day_entrys++;
}

/**
 * @brief add the gadgetbridge events of the shown day
 */
static void calendar_day_add_ble_entrys( void ) {
    int slots[ BLECALENDAR_MAX_EVENTS ];
    int count = blecalendar_get_day_events( calendar_day_year, calendar_day_month, calendar_day_day, slots, BLECALENDAR_MAX_EVENTS );

    for ( int i = 0 ; i < count ; i++ ) {
        const blecalendar_event_t *event = blecalendar_get_event( slots[ i ] );
        char text[ 80 ] = "";
        struct tm start_tm;

        if ( !event )
            continue;

        localtime_r( &event->start, &start_tm );

        if ( event->allday )
            snprintf( text, sizeof( text ), LV_SYMBOL_BLUETOOTH " %s", event->title );
        else
            snprintf( text, sizeof( text ), LV_SYMBOL_BLUETOOTH " %02d:%02d - %s", start_tm.tm_hour, start_tm.tm_min, event->title );

        calendar_day_add_entry( event->allday ? 0 : start_tm.tm_hour, event->allday ? 0 : start_tm.tm_min, slots[ i ], text );
    }
}

/**
 * @brief build the whole entry list
 */
static void calendar_day_build_list( void ) {
    while ( lv_list_remove( calendar_day_list, 0 ) );

    for ( int i = 0 ; i < calendar_day_entrys ; i++ ) {
        lv_obj_t *list_btn = lv_list_add_btn( calendar_day_list, NULL, calendar_day_entry_table[ i ].text );

        if ( calendar_day_entry_table[ i ].ble_slot < 0 ) {
            lv_obj_set_event_cb( list_btn, calendar_day_edit_event_cb );
        }
        else {
            lv_obj_set_user_data( list_btn, ( lv_obj_user_data_t )( intptr_t )calendar_day_entry_table[ i ].ble_slot );
            lv_obj_set_event_cb( list_btn, calendar_day_ble_event_cb );
        }
    }
}

uint32_t calendar_day_get_tile( void ) {
    return( calendar_day_tile_num );
}

void calendar_day_overview_refresh( int year, int month, int day ) {
    calendar_day_year = year;
    calendar_day_month = month;
    calendar_day_day = day;
    calendar_day_entrys = 0;
    /**
     * build sql query string
     */
#ifdef NATIVE_64BIT
    char sql[512]="";
    snprintf( sql, sizeof( sql ),    "SELECT hour, min, content FROM calendar WHERE year == %d AND month == %d AND day == %d ORDER BY hour, min;",
                                    year,
                                    month,
                                    day );
    /**
     * exec sql query
     */
    if ( calendar_db_exec( calendar_day_overview_callback, sql ) ) {
        CALENDAR_DAY_DEBUG_LOG("list created");
    }
#else
    String sql = (String) "SELECT hour, min, content FROM calendar WHERE year == " + year + " AND month == " + month + " AND day == " + day + " ORDER BY hour, min;";
    /**
     * exec sql query
     */
    if ( calendar_db_exec( calendar_day_overview_callback, sql.c_str() ) ) {
        CALENDAR_DAY_DEBUG_LOG("list created");
    }
#endif
    /**
     * merge the read only events from ble
     */
    calendar_day_add_ble_entrys();
    calendar_day_build_list();
}