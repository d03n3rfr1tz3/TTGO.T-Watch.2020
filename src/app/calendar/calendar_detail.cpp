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
#include "calendar_detail.h"

#include "gui/mainbar/mainbar.h"
#include "gui/app.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

#include "utils/alloc.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
    #include <time.h>
#else
    #include <time.h>
    #include <Arduino.h>
#endif

LV_FONT_DECLARE(Ubuntu_12px);
LV_FONT_DECLARE(Ubuntu_16px);
LV_FONT_DECLARE(Ubuntu_32px);

#if defined( BIG_THEME )
    static lv_font_t *calendar_detail_font = &Ubuntu_16px;
    static lv_font_t *calendar_detail_title_font = &Ubuntu_32px;
#elif defined( MID_THEME )
    static lv_font_t *calendar_detail_font = &Ubuntu_12px;
    static lv_font_t *calendar_detail_title_font = &Ubuntu_16px;
#else
    static lv_font_t *calendar_detail_font = &Ubuntu_12px;
    static lv_font_t *calendar_detail_title_font = &Ubuntu_16px;
#endif

static uint32_t calendar_detail_tile_num;               /** @brief allocated calendar detail tile number */
static lv_style_t calendar_detail_style;                /** @brief calendar detail style object */
static lv_obj_t *calendar_detail_page = NULL;
static lv_obj_t *calendar_detail_title_label = NULL;
static lv_obj_t *calendar_detail_time_label = NULL;
static lv_obj_t *calendar_detail_location_label = NULL;
static lv_obj_t *calendar_detail_calname_label = NULL;
static lv_obj_t *calendar_detail_description_label = NULL;

static void calendar_detail_build_ui( void );
static lv_obj_t *calendar_detail_add_label( lv_font_t *font );
static lv_obj_t *calendar_detail_set_label( lv_obj_t *label, lv_obj_t *prev, const char *text );
static void calendar_detail_exit_event_cb( lv_obj_t * obj, lv_event_t event );

void calendar_detail_setup( void ) {
    calendar_detail_tile_num = mainbar_add_app_tile( 1, 1, "calendar detail" );
    calendar_detail_build_ui();
}

uint32_t calendar_detail_get_tile( void ) {
    return( calendar_detail_tile_num );
}

static void calendar_detail_build_ui( void ) {
    lv_obj_t *calendar_detail_tile = mainbar_get_tile_obj( calendar_detail_tile_num );

    lv_style_init( &calendar_detail_style );
    lv_style_set_border_width( &calendar_detail_style, LV_OBJ_PART_MAIN, 0 );
    lv_style_set_radius( &calendar_detail_style, LV_OBJ_PART_MAIN, 0 );
    lv_style_set_bg_opa( &calendar_detail_style, LV_OBJ_PART_MAIN, LV_OPA_0 );
    lv_style_set_text_font( &calendar_detail_style, LV_OBJ_PART_MAIN, calendar_detail_font );

    calendar_detail_page = lv_page_create( calendar_detail_tile, NULL );
    lv_obj_set_size( calendar_detail_page, lv_disp_get_hor_res( NULL ) - ( 2 * THEME_PADDING ), lv_disp_get_ver_res( NULL ) - THEME_ICON_SIZE - THEME_PADDING );
    lv_obj_add_style( calendar_detail_page, LV_OBJ_PART_MAIN, &calendar_detail_style );
    lv_page_set_scrlbar_mode( calendar_detail_page, LV_SCRLBAR_MODE_DRAG );
    lv_obj_align( calendar_detail_page, calendar_detail_tile, LV_ALIGN_IN_TOP_MID, 0, THEME_PADDING );

    calendar_detail_title_label = calendar_detail_add_label( calendar_detail_title_font );
    calendar_detail_time_label = calendar_detail_add_label( calendar_detail_font );
    calendar_detail_location_label = calendar_detail_add_label( calendar_detail_font );
    calendar_detail_calname_label = calendar_detail_add_label( calendar_detail_font );
    calendar_detail_description_label = calendar_detail_add_label( calendar_detail_font );
    /**
     * no add and no trash button here, the tile has no write path
     */
    lv_obj_t *exit_button = wf_add_exit_button( calendar_detail_tile, calendar_detail_exit_event_cb );
    lv_obj_align( exit_button, calendar_detail_tile, LV_ALIGN_IN_BOTTOM_LEFT, THEME_ICON_PADDING, -THEME_ICON_PADDING );
}

static lv_obj_t *calendar_detail_add_label( lv_font_t *font ) {
    lv_obj_t *label = lv_label_create( calendar_detail_page, NULL );

    lv_label_set_long_mode( label, LV_LABEL_LONG_BREAK );
    lv_obj_set_width( label, lv_page_get_width_fit( calendar_detail_page ) );
    lv_obj_add_style( label, LV_OBJ_PART_MAIN, &calendar_detail_style );
    lv_obj_set_style_local_text_font( label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, font );
    lv_label_set_text( label, "" );

    return( label );
}

/**
 * @brief set a label and put it under the last visible one, empty fields stay hidden
 */
static lv_obj_t *calendar_detail_set_label( lv_obj_t *label, lv_obj_t *prev, const char *text ) {
    if ( !text || !*text ) {
        lv_obj_set_hidden( label, true );
        return( prev );
    }

    lv_obj_set_hidden( label, false );
    lv_label_set_text( label, text );

    if ( prev )
        lv_obj_align( label, prev, LV_ALIGN_OUT_BOTTOM_LEFT, 0, THEME_PADDING );
    else
        lv_obj_align( label, NULL, LV_ALIGN_IN_TOP_LEFT, 0, 0 );

    return( label );
}

void calendar_detail_show( int slot ) {
    const blecalendar_event_t *event = blecalendar_get_event( slot );
    char time_str[ 64 ] = "";
    lv_obj_t *prev = NULL;

    if ( !event ) {
        CALENDAR_DEBUG_LOG("event slot %d is gone", slot );
        return;
    }

    if ( event->allday ) {
        snprintf( time_str, sizeof( time_str ), "all day" );
    }
    else {
        struct tm start_tm;
        struct tm end_tm;
        time_t end = event->start + ( time_t )event->duration;

        localtime_r( &event->start, &start_tm );
        localtime_r( &end, &end_tm );

        if ( event->duration )
            snprintf( time_str, sizeof( time_str ), "%02d:%02d - %02d:%02d", start_tm.tm_hour, start_tm.tm_min, end_tm.tm_hour, end_tm.tm_min );
        else
            snprintf( time_str, sizeof( time_str ), "%02d:%02d", start_tm.tm_hour, start_tm.tm_min );
    }

    prev = calendar_detail_set_label( calendar_detail_title_label, prev, event->title );
    prev = calendar_detail_set_label( calendar_detail_time_label, prev, time_str );
    prev = calendar_detail_set_label( calendar_detail_location_label, prev, event->location );
    prev = calendar_detail_set_label( calendar_detail_calname_label, prev, event->calname );
    prev = calendar_detail_set_label( calendar_detail_description_label, prev, event->description );

    mainbar_jump_to_tilenumber( calendar_detail_tile_num, LV_ANIM_OFF );
}

static void calendar_detail_exit_event_cb( lv_obj_t * obj, lv_event_t event ) {
    if ( event == LV_EVENT_CLICKED ) {
        mainbar_jump_back();
    }
}
