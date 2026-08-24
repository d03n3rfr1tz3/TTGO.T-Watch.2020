/****************************************************************************
 *   Aug 20 22:14:00 2026
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

#include <math.h>

#include "soundmeter_app.h"
#include "soundmeter_app_main.h"

#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"
#include "hardware/micctl.h"
#include "hardware/powermgm.h"
#include "utils/alloc.h"

LV_FONT_DECLARE(Ubuntu_32px);

static lv_obj_t *soundmeter_app_main_tile = NULL;
static lv_obj_t *soundmeter_level_label = NULL;
static lv_obj_t *soundmeter_level_bar = NULL;
static lv_obj_t *soundmeter_peak_label = NULL;

static lv_style_t soundmeter_level_style;
static lv_task_t *soundmeter_app_lv_task = NULL;

static int16_t *soundmeter_buffer = NULL;
static bool soundmeter_boost = false;
static float soundmeter_level_db = SOUNDMETER_DB_FLOOR;
static float soundmeter_peak_db = SOUNDMETER_DB_FLOOR;

static void exit_soundmeter_app_main_event_cb( lv_obj_t * obj, lv_event_t event );
static void soundmeter_app_activate_cb( void );
static void soundmeter_app_hibernate_cb( void );
static void soundmeter_app_task( lv_task_t * task );

void soundmeter_app_main_setup( uint32_t tile_num ) {

    mainbar_add_tile_activate_cb( tile_num, soundmeter_app_activate_cb );
    mainbar_add_tile_hibernate_cb( tile_num, soundmeter_app_hibernate_cb );

    soundmeter_app_main_tile = mainbar_get_tile_obj( tile_num );

    lv_coord_t hor_res = lv_disp_get_hor_res( NULL );

    lv_style_copy( &soundmeter_level_style, ws_get_label_style() );
    lv_style_set_text_font( &soundmeter_level_style, LV_STATE_DEFAULT, &Ubuntu_32px );

    soundmeter_level_label = lv_label_create( soundmeter_app_main_tile, NULL );
    lv_label_set_text( soundmeter_level_label, "-- dB" );
    lv_label_set_align( soundmeter_level_label, LV_LABEL_ALIGN_CENTER );
    lv_obj_add_style( soundmeter_level_label, LV_OBJ_PART_MAIN, &soundmeter_level_style );
    lv_obj_align( soundmeter_level_label, soundmeter_app_main_tile, LV_ALIGN_IN_TOP_MID, 0, THEME_CONT_HEIGHT );

    soundmeter_level_bar = lv_bar_create( soundmeter_app_main_tile, NULL );
    lv_obj_set_size( soundmeter_level_bar, hor_res - 4 * THEME_PADDING, 24 );
    lv_obj_set_click( soundmeter_level_bar, false );
    lv_bar_set_range( soundmeter_level_bar, 0, 100 );
    lv_bar_set_anim_time( soundmeter_level_bar, 100 );
    lv_bar_set_value( soundmeter_level_bar, 0, LV_ANIM_OFF );
    lv_obj_align( soundmeter_level_bar, soundmeter_level_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 2 * THEME_PADDING );

    soundmeter_peak_label = wf_add_label( soundmeter_app_main_tile, "peak -- dB" );
    lv_label_set_align( soundmeter_peak_label, LV_LABEL_ALIGN_CENTER );
    lv_obj_align( soundmeter_peak_label, soundmeter_level_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_PADDING );

    lv_obj_t *exit_btn = wf_add_exit_button( soundmeter_app_main_tile, exit_soundmeter_app_main_event_cb );
    lv_obj_align( exit_btn, soundmeter_app_main_tile, LV_ALIGN_IN_BOTTOM_LEFT, THEME_ICON_PADDING, -THEME_ICON_PADDING );

    lv_tileview_add_element( soundmeter_app_main_tile, soundmeter_level_label );
    lv_tileview_add_element( soundmeter_app_main_tile, soundmeter_level_bar );
    lv_tileview_add_element( soundmeter_app_main_tile, soundmeter_peak_label );
    lv_tileview_add_element( soundmeter_app_main_tile, exit_btn );

    soundmeter_app_lv_task = lv_task_create( soundmeter_app_task, 100, LV_TASK_PRIO_OFF, NULL );
}

static void soundmeter_app_activate_cb( void ) {
    soundmeter_level_db = SOUNDMETER_DB_FLOOR;
    soundmeter_peak_db = SOUNDMETER_DB_FLOOR;

    if ( !soundmeter_buffer )
        soundmeter_buffer = ( int16_t * )MALLOC( SOUNDMETER_BLOCK_SAMPLES * sizeof( int16_t ) );

    if ( !soundmeter_buffer || !micctl_start( SOUNDMETER_SAMPLE_RATE ) ) {
        lv_label_set_text( soundmeter_level_label, "no mic" );
        lv_obj_align( soundmeter_level_label, soundmeter_app_main_tile, LV_ALIGN_IN_TOP_MID, 0, THEME_CONT_HEIGHT );
        return;
    }
    
    powermgm_cpu_boost_take();
    soundmeter_boost = true;
    lv_task_set_prio( soundmeter_app_lv_task, LV_TASK_PRIO_MID );
}

static void soundmeter_app_hibernate_cb( void ) {
    lv_task_set_prio( soundmeter_app_lv_task, LV_TASK_PRIO_OFF );
    micctl_stop();

    if ( soundmeter_boost ) {
        powermgm_cpu_boost_give();
        soundmeter_boost = false;
    }

    if ( soundmeter_buffer ) {
        free( soundmeter_buffer );
        soundmeter_buffer = NULL;
    }
}

/**
 * @brief   drain the dma buffers and return the level in dBFS
 *
 * @param   peak_db     takes the peak level of the same samples
 *
 * @return  true if samples were available
 */
static bool soundmeter_measure( float *level_db, float *peak_db ) {
    uint64_t energy = 0;
    int32_t peak = 0;
    int32_t offset = 0;
    size_t total = 0;

    for ( int block = 0 ; block < SOUNDMETER_MAX_BLOCKS ; block++ ) {
        size_t samples = micctl_read( soundmeter_buffer, SOUNDMETER_BLOCK_SAMPLES, 0 );
        if ( !samples )
            break;

        int32_t sum = 0;
        for ( size_t i = 0 ; i < samples ; i++ )
            sum += soundmeter_buffer[ i ];
        offset = sum / ( int32_t )samples;

        for ( size_t i = 0 ; i < samples ; i++ ) {
            int32_t value = soundmeter_buffer[ i ] - offset;
            energy += ( uint64_t )( value * value );
            if ( abs( value ) > peak )
                peak = abs( value );
        }
        total += samples;
    }

    if ( !total )
        return( false );

    float rms = sqrtf( ( float )energy / ( float )total );

    *level_db = rms < 1.0f ? SOUNDMETER_DB_FLOOR : 20.0f * log10f( rms / 32768.0f );
    *peak_db = peak < 1 ? SOUNDMETER_DB_FLOOR : 20.0f * log10f( ( float )peak / 32768.0f );

    return( true );
}

static void soundmeter_app_task( lv_task_t * task ) {
    float level_db = SOUNDMETER_DB_FLOOR;
    float peak_db = SOUNDMETER_DB_FLOOR;

    if ( !soundmeter_buffer || !micctl_is_running() )
        return;

    if ( !soundmeter_measure( &level_db, &peak_db ) )
        return;

    if ( level_db < SOUNDMETER_DB_FLOOR )
        level_db = SOUNDMETER_DB_FLOOR;

    soundmeter_level_db = level_db > soundmeter_level_db ? level_db : soundmeter_level_db + ( level_db - soundmeter_level_db ) * 0.3f;
    soundmeter_peak_db = peak_db > soundmeter_peak_db ? peak_db : soundmeter_peak_db - SOUNDMETER_PEAK_DECAY;
    if ( soundmeter_peak_db < SOUNDMETER_DB_FLOOR )
        soundmeter_peak_db = SOUNDMETER_DB_FLOOR;

    float level_spl = micctl_dbfs_to_spl( soundmeter_level_db );

    lv_label_set_text_fmt( soundmeter_level_label, "%d dB", ( int )level_spl );
    lv_obj_align( soundmeter_level_label, soundmeter_app_main_tile, LV_ALIGN_IN_TOP_MID, 0, THEME_CONT_HEIGHT );
    lv_label_set_text_fmt( soundmeter_peak_label, "peak %d dB", ( int )micctl_dbfs_to_spl( soundmeter_peak_db ) );
    lv_obj_align( soundmeter_peak_label, soundmeter_level_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_PADDING );
    lv_bar_set_value( soundmeter_level_bar, ( int16_t )( ( level_spl - SOUNDMETER_SPL_FLOOR ) * 100.0f / ( SOUNDMETER_SPL_CEIL - SOUNDMETER_SPL_FLOOR ) ), LV_ANIM_ON );
}

static void exit_soundmeter_app_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_jump_back();
                                        break;
    }
}
