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

#include <stdio.h>

#include "analyzer_app.h"
#include "analyzer_tone.h"

#include "gui/mainbar/mainbar.h"
#include "gui/widget_factory.h"
#include "hardware/sound.h"

static const uint16_t analyzer_tone_freq[] = { 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                                               1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000 };
#define ANALYZER_TONE_COUNT     ( sizeof( analyzer_tone_freq ) / sizeof( analyzer_tone_freq[ 0 ] ) )
#define ANALYZER_TONE_DEFAULT   10

static lv_obj_t *analyzer_tone_tile = NULL;
static lv_obj_t *analyzer_tone_roller = NULL;
static lv_obj_t *analyzer_tone_play_btn = NULL;
static lv_obj_t *analyzer_tone_stop_btn = NULL;
static bool analyzer_tone_playing = false;

static void analyzer_tone_play_event_cb( lv_obj_t * obj, lv_event_t event );
static void analyzer_tone_stop_event_cb( lv_obj_t * obj, lv_event_t event );
static void analyzer_tone_roller_event_cb( lv_obj_t * obj, lv_event_t event );
static void analyzer_tone_start( void );
static void analyzer_tone_show_playing( bool playing );

void analyzer_tone_setup( uint32_t tile_num ) {
    char options[ ANALYZER_TONE_COUNT * 6 ];
    size_t len = 0;

    for( size_t i = 0 ; i < ANALYZER_TONE_COUNT ; i++ )
        len += snprintf( options + len, sizeof( options ) - len, i ? "\n%d" : "%d", analyzer_tone_freq[ i ] );

    analyzer_tone_tile = mainbar_get_tile_obj( tile_num );

    analyzer_tone_roller = wf_add_roller( analyzer_tone_tile, options, LV_ROLLER_MODE_NORMAL, 3 );
    lv_obj_set_size( analyzer_tone_roller, 100, 90 );
    lv_roller_set_selected( analyzer_tone_roller, ANALYZER_TONE_DEFAULT, LV_ANIM_OFF );
    lv_obj_set_event_cb( analyzer_tone_roller, analyzer_tone_roller_event_cb );
    lv_obj_align( analyzer_tone_roller, analyzer_tone_tile, LV_ALIGN_CENTER, -THEME_ICON_SIZE / 2, 0 );

    lv_obj_t *unit_label = wf_add_label( analyzer_tone_tile, "Hz" );
    lv_obj_align( unit_label, analyzer_tone_roller, LV_ALIGN_OUT_RIGHT_MID, THEME_PADDING, 0 );

    analyzer_tone_play_btn = wf_add_play_button( analyzer_tone_tile, analyzer_tone_play_event_cb );
    lv_obj_align( analyzer_tone_play_btn, analyzer_tone_tile, LV_ALIGN_IN_BOTTOM_RIGHT, -THEME_ICON_PADDING, -THEME_ICON_PADDING );
    analyzer_tone_stop_btn = wf_add_stop_button( analyzer_tone_tile, analyzer_tone_stop_event_cb );
    lv_obj_align( analyzer_tone_stop_btn, analyzer_tone_tile, LV_ALIGN_IN_BOTTOM_RIGHT, -THEME_ICON_PADDING, -THEME_ICON_PADDING );
    lv_obj_set_hidden( analyzer_tone_stop_btn, true );

    analyzer_app_add_footer( analyzer_tone_tile );

    lv_tileview_add_element( analyzer_tone_tile, unit_label );
    lv_tileview_add_element( analyzer_tone_tile, analyzer_tone_play_btn );
    lv_tileview_add_element( analyzer_tone_tile, analyzer_tone_stop_btn );
}

void analyzer_tone_leave( void ) {
    sound_stop_tone();
    analyzer_tone_show_playing( false );
}

void analyzer_tone_update( void ) {
    /*
     * the tone ends by itself, so the button has to follow without a click
     */
    analyzer_tone_show_playing( sound_tone_is_running() );
}

static void analyzer_tone_show_playing( bool playing ) {
    if( playing == analyzer_tone_playing )
        return;

    analyzer_tone_playing = playing;
    lv_obj_set_hidden( analyzer_tone_play_btn, playing );
    lv_obj_set_hidden( analyzer_tone_stop_btn, !playing );
}

static void analyzer_tone_start( void ) {
    /*
     * a measuring tool started by hand, so it plays inside the silence timeframe too
     */
    sound_play_tone( analyzer_tone_freq[ lv_roller_get_selected( analyzer_tone_roller ) ], SOUND_TYPE_FOREGROUND );
    analyzer_tone_show_playing( sound_tone_is_running() );
}

static void analyzer_tone_play_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       analyzer_tone_start();
                                        break;
    }
}

static void analyzer_tone_stop_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       sound_stop_tone();
                                        analyzer_tone_show_playing( false );
                                        break;
    }
}

static void analyzer_tone_roller_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_VALUE_CHANGED ): if( analyzer_tone_playing )
                                            analyzer_tone_start();
                                        break;
    }
}
