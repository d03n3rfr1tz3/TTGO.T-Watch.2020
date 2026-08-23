/****************************************************************************
 *   Aug 22 23:00:00 2026
 *   Copyright  2026  Dirk Sarodnick
 *   Email: programmer@dirk-sarodnick.de
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

#include <stdlib.h>

#include "analyzer_app.h"
#include "analyzer_canvas.h"
#include "analyzer_dsp.h"
#include "analyzer_waterfall.h"
#include "analyzer_spectrum.h"
#include "analyzer_scope.h"
#include "analyzer_tone.h"

#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/app.h"
#include "hardware/micctl.h"

uint32_t analyzer_app_main_tile_num;

icon_t *analyzer_app = NULL;

LV_IMG_DECLARE(analyzer_app_64px);

typedef struct {
    lv_obj_t *tile;                                     /** @brief the mainbar tile, also the visibility probe */
    lv_obj_t *header;                                   /** @brief the readout above the canvas */
    MAINBAR_CALLBACK_FUNC enter;                        /** @brief take the canvas and reset the state */
    MAINBAR_CALLBACK_FUNC leave;                        /** @brief give the canvas back */
    MAINBAR_CALLBACK_FUNC update;                       /** @brief draw one frame */
    bool with_fft;                                      /** @brief the time domain tiles do not need the transform */
} analyzer_app_tile_t;

static analyzer_app_tile_t analyzer_app_tile_table[ ANALYZER_APP_TILES ] = {
    { NULL, NULL, analyzer_waterfall_enter, analyzer_waterfall_leave, analyzer_waterfall_update, true },
    { NULL, NULL, analyzer_spectrum_enter, analyzer_spectrum_leave, analyzer_spectrum_update, true },
    { NULL, NULL, NULL, NULL, NULL, false },
    { NULL, NULL, NULL, NULL, NULL, false },
};

static lv_task_t *analyzer_app_task = NULL;
static bool analyzer_app_running = false;

static void enter_analyzer_app_event_cb( lv_obj_t * obj, lv_event_t event );
static void exit_analyzer_app_event_cb( lv_obj_t * obj, lv_event_t event );
static void analyzer_app_activate_cb( void );
static void analyzer_app_hibernate_cb( void );
static void analyzer_app_lv_task( lv_task_t * task );
static void analyzer_app_set_running( bool running );
static int analyzer_app_visible_tile( void );
static lv_obj_t * analyzer_app_add_header( lv_obj_t *tile );
static void analyzer_app_set_header( int index, const char *text );

/*
 * automatic register the app setup function with explicit call in main.cpp
 */
static int registed = app_autocall_function( &analyzer_app_setup, APP_PRIO( APP_GROUP_AUDIO, 1 ) );           /** @brief app autocall function */

void analyzer_app_setup( void ) {
    /*
     * check if app already registered for autocall
     */
    if( !registed ) {
        return;
    }
    /*
     * without a microphone the app has nothing to show
     */
    if( !micctl_get_available() ) {
        return;
    }

    analyzer_app_main_tile_num = mainbar_add_app_tile( ANALYZER_APP_TILES, 1, "analyzer app" );
    analyzer_app = app_register( "sound\nanalyzer", &analyzer_app_64px, enter_analyzer_app_event_cb );

    for( int i = 0 ; i < ANALYZER_APP_TILES ; i++ ) {
        uint32_t tile_num = analyzer_app_main_tile_num + i;

        mainbar_add_tile_activate_cb( tile_num, analyzer_app_activate_cb );
        mainbar_add_tile_hibernate_cb( tile_num, analyzer_app_hibernate_cb );

        analyzer_app_tile_table[ i ].tile = mainbar_get_tile_obj( tile_num );
        analyzer_app_tile_table[ i ].header = analyzer_app_add_header( analyzer_app_tile_table[ i ].tile );
    }

    analyzer_waterfall_setup( analyzer_app_main_tile_num );
    analyzer_spectrum_setup( analyzer_app_main_tile_num + 1 );
    analyzer_scope_setup( analyzer_app_main_tile_num + 2 );
    analyzer_tone_setup( analyzer_app_main_tile_num + 3 );

    analyzer_app_task = lv_task_create( analyzer_app_lv_task, ANALYZER_TASK_PERIOD, LV_TASK_PRIO_OFF, NULL );
}

uint32_t analyzer_app_get_app_main_tile_num( void ) {
    return( analyzer_app_main_tile_num );
}

static lv_obj_t * analyzer_app_add_header( lv_obj_t *tile ) {
    lv_obj_t *header = wf_add_label( tile, "---- Hz   -- dB" );

    lv_label_set_align( header, LV_LABEL_ALIGN_CENTER );
    lv_obj_align( header, tile, LV_ALIGN_IN_TOP_MID, 0, ANALYZER_APP_HEADER_Y );
    lv_tileview_add_element( tile, header );

    return( header );
}

static void analyzer_app_set_header( int index, const char *text ) {
    lv_obj_t *header = analyzer_app_tile_table[ index ].header;

    lv_label_set_text( header, text );
    lv_obj_align( header, analyzer_app_tile_table[ index ].tile, LV_ALIGN_IN_TOP_MID, 0, ANALYZER_APP_HEADER_Y );
}

static int analyzer_app_visible_tile( void ) {
    lv_coord_t best = lv_disp_get_hor_res( NULL ) / 2;
    int index = -1;
    lv_area_t area;

    for( int i = 0 ; i < ANALYZER_APP_TILES ; i++ ) {
        lv_obj_get_coords( analyzer_app_tile_table[ i ].tile, &area );

        lv_coord_t distance = abs( area.x1 ) + abs( area.y1 );
        if( distance < best ) {
            best = distance;
            index = i;
        }
    }

    return( index );
}

static void analyzer_app_set_running( bool running ) {
    analyzer_app_running = running;

    if( !running ) {
        for( int i = 0 ; i < ANALYZER_APP_TILES ; i++ )
            if( analyzer_app_tile_table[ i ].leave )
                analyzer_app_tile_table[ i ].leave();

        analyzer_dsp_stop();
        lv_task_set_period( analyzer_app_task, ANALYZER_APP_IDLE_PERIOD );
        return;
    }

    lv_task_set_period( analyzer_app_task, ANALYZER_TASK_PERIOD );
    
    if( !analyzer_dsp_start() ) {
        for( int i = 0 ; i < ANALYZER_APP_TILES ; i++ )
            analyzer_app_set_header( i, "no mic" );
        return;
    }
    
    for( int i = 0 ; i < ANALYZER_APP_TILES ; i++ )
        if( analyzer_app_tile_table[ i ].enter )
            analyzer_app_tile_table[ i ].enter();
}

static void analyzer_app_lv_task( lv_task_t * task ) {
    int index = analyzer_app_visible_tile();

    if( ( index >= 0 ) != analyzer_app_running )
        analyzer_app_set_running( index >= 0 );

    if( index < 0 || !analyzer_dsp_update( analyzer_app_tile_table[ index ].with_fft ) )
        return;

    int spl = ( int )micctl_dbfs_to_spl( analyzer_dsp_get_peak_db() );
    
    if( analyzer_app_tile_table[ index ].with_fft )
        lv_label_set_text_fmt( analyzer_app_tile_table[ index ].header, "%d Hz   %d dB", ( int )analyzer_dsp_get_peak_frequency(), spl );
    else
        lv_label_set_text_fmt( analyzer_app_tile_table[ index ].header, "%d dB", spl );

    lv_obj_align( analyzer_app_tile_table[ index ].header, analyzer_app_tile_table[ index ].tile, LV_ALIGN_IN_TOP_MID, 0, ANALYZER_APP_HEADER_Y );

    if( analyzer_app_tile_table[ index ].update )
        analyzer_app_tile_table[ index ].update();
}

static void analyzer_app_activate_cb( void ) {
    lv_task_set_prio( analyzer_app_task, LV_TASK_PRIO_MID );
}

static void analyzer_app_hibernate_cb( void ) {
    if( analyzer_app_running )
        analyzer_app_set_running( false );

    lv_task_set_prio( analyzer_app_task, LV_TASK_PRIO_OFF );
}

lv_obj_t * analyzer_app_add_footer( lv_obj_t *tile ) {
    lv_obj_t *footer = wf_add_tile_footer_container( tile, LV_LAYOUT_PRETTY_MID );
    wf_add_exit_button( footer, exit_analyzer_app_event_cb );
    lv_obj_align( footer, tile, LV_ALIGN_IN_BOTTOM_MID, 0, -10 );
    return( footer );
}

void analyzer_app_add_axis( lv_obj_t *tile, const char **text, const lv_coord_t *x, int count, lv_coord_t y ) {
    for( int i = 0 ; i < count ; i++ ) {
        lv_obj_t *label = wf_add_label( tile, text[ i ] );
        lv_coord_t width = lv_obj_get_width( label );
        lv_coord_t left = x[ i ] - width / 2;

        if( left < 0 )
            left = 0;
        if( left > ANALYZER_CANVAS_WIDTH - width )
            left = ANALYZER_CANVAS_WIDTH - width;

        lv_obj_align( label, tile, LV_ALIGN_IN_TOP_LEFT, left, y );
        lv_tileview_add_element( tile, label );
    }
}

static void enter_analyzer_app_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       app_hide_indicator( analyzer_app );
                                        mainbar_jump_to_tilenumber( analyzer_app_main_tile_num, LV_ANIM_OFF, true );
                                        break;
    }
}

static void exit_analyzer_app_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_jump_back();
                                        break;
    }
}
