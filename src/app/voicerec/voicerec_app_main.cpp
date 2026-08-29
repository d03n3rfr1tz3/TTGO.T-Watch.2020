/****************************************************************************
 *   Aug 25 20:00:00 2026
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

#include "voicerec_app.h"
#include "voicerec_app_main.h"
#include "voicerec_config.h"
#include "voicerec_recorder.h"

#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"
#include "hardware/micctl.h"

LV_FONT_DECLARE(Ubuntu_32px);

static lv_obj_t *voicerec_app_main_tile = NULL;
static lv_obj_t *voicerec_timer_label = NULL;
static lv_obj_t *voicerec_level_bar = NULL;
static lv_obj_t *voicerec_state_label = NULL;
static lv_obj_t *voicerec_quality_switch = NULL;
static lv_obj_t *voicerec_gain_list = NULL;
static lv_obj_t *voicerec_play_btn = NULL;
static lv_obj_t *voicerec_stop_btn = NULL;

static lv_style_t voicerec_timer_style;
static lv_task_t *voicerec_app_main_task = NULL;

static voicerec_config_t voicerec_config;
static bool voicerec_config_changed = false;

static void voicerec_app_main_activate_cb( void );
static void voicerec_app_main_hibernate_cb( void );
static void voicerec_app_main_lv_task( lv_task_t * task );
static void voicerec_app_main_exit_event_cb( lv_obj_t * obj, lv_event_t event );
static void voicerec_app_main_right_event_cb( lv_obj_t * obj, lv_event_t event );
static void voicerec_app_main_play_event_cb( lv_obj_t * obj, lv_event_t event );
static void voicerec_app_main_stop_event_cb( lv_obj_t * obj, lv_event_t event );
static void voicerec_app_main_quality_event_cb( lv_obj_t * obj, lv_event_t event );
static void voicerec_app_main_gain_event_cb( lv_obj_t * obj, lv_event_t event );

void voicerec_app_main_setup( uint32_t tile_num ) {

    mainbar_add_tile_activate_cb( tile_num, voicerec_app_main_activate_cb );
    mainbar_add_tile_hibernate_cb( tile_num, voicerec_app_main_hibernate_cb );

    voicerec_app_main_tile = mainbar_get_tile_obj( tile_num );

    lv_coord_t hor_res = lv_disp_get_hor_res( NULL );

    voicerec_config.load();

    lv_style_copy( &voicerec_timer_style, ws_get_label_style() );
    lv_style_set_text_font( &voicerec_timer_style, LV_STATE_DEFAULT, &Ubuntu_32px );

    voicerec_timer_label = lv_label_create( voicerec_app_main_tile, NULL );
    lv_label_set_text_fmt( voicerec_timer_label, "0 / %d s", VOICEREC_MAX_SECONDS );
    lv_label_set_align( voicerec_timer_label, LV_LABEL_ALIGN_CENTER );
    lv_obj_add_style( voicerec_timer_label, LV_OBJ_PART_MAIN, &voicerec_timer_style );
    lv_obj_align( voicerec_timer_label, voicerec_app_main_tile, LV_ALIGN_IN_TOP_MID, 0, STATUSBAR_HEIGHT );

    voicerec_level_bar = lv_bar_create( voicerec_app_main_tile, NULL );
    lv_obj_set_size( voicerec_level_bar, hor_res - 4 * THEME_PADDING, VOICEREC_BAR_HEIGHT );
    lv_obj_set_click( voicerec_level_bar, false );
    lv_bar_set_range( voicerec_level_bar, 0, 100 );
    lv_bar_set_anim_time( voicerec_level_bar, VOICEREC_APP_MAIN_PERIOD );
    lv_bar_set_value( voicerec_level_bar, 0, LV_ANIM_OFF );
    lv_obj_align( voicerec_level_bar, voicerec_timer_label, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_PADDING );

    voicerec_state_label = wf_add_label( voicerec_app_main_tile, "ready" );
    lv_label_set_align( voicerec_state_label, LV_LABEL_ALIGN_CENTER );
    lv_obj_align( voicerec_state_label, voicerec_level_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_PADDING );

    lv_obj_t *quality_cont = wf_add_labeled_switch( voicerec_app_main_tile, "8 bit", &voicerec_quality_switch, voicerec_config.low_quality, voicerec_app_main_quality_event_cb, APP_STYLE );
    lv_obj_align( quality_cont, voicerec_state_label, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_PADDING );

    lv_obj_t *gain_cont = wf_add_labeled_list( voicerec_app_main_tile, "gain", &voicerec_gain_list, VOICEREC_GAIN_OPTIONS, voicerec_app_main_gain_event_cb, APP_STYLE );
    lv_dropdown_set_selected( voicerec_gain_list, voicerec_config.gain );
    lv_dropdown_set_dir( voicerec_gain_list, LV_DROPDOWN_DIR_UP );
    lv_dropdown_set_max_height( voicerec_gain_list, 4 * THEME_CONT_HEIGHT );
    lv_obj_align( gain_cont, quality_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_PADDING );

    lv_obj_t *exit_btn = wf_add_exit_button( voicerec_app_main_tile, voicerec_app_main_exit_event_cb );
    lv_obj_align( exit_btn, voicerec_app_main_tile, LV_ALIGN_IN_BOTTOM_LEFT, THEME_ICON_PADDING, -THEME_ICON_PADDING );

    voicerec_play_btn = wf_add_play_button( voicerec_app_main_tile, voicerec_app_main_play_event_cb );
    lv_obj_align( voicerec_play_btn, voicerec_app_main_tile, LV_ALIGN_IN_BOTTOM_MID, 0, -THEME_ICON_PADDING );

    voicerec_stop_btn = wf_add_stop_button( voicerec_app_main_tile, voicerec_app_main_stop_event_cb );
    lv_obj_align( voicerec_stop_btn, voicerec_app_main_tile, LV_ALIGN_IN_BOTTOM_MID, 0, -THEME_ICON_PADDING );
    lv_obj_set_hidden( voicerec_stop_btn, true );

    lv_obj_t *right_btn = wf_add_right_button( voicerec_app_main_tile, voicerec_app_main_right_event_cb );
    lv_obj_align( right_btn, voicerec_app_main_tile, LV_ALIGN_IN_BOTTOM_RIGHT, -THEME_ICON_PADDING, -THEME_ICON_PADDING );

    lv_tileview_add_element( voicerec_app_main_tile, voicerec_timer_label );
    lv_tileview_add_element( voicerec_app_main_tile, voicerec_level_bar );
    lv_tileview_add_element( voicerec_app_main_tile, voicerec_state_label );
    lv_tileview_add_element( voicerec_app_main_tile, quality_cont );
    lv_tileview_add_element( voicerec_app_main_tile, voicerec_quality_switch );
    lv_tileview_add_element( voicerec_app_main_tile, gain_cont );
    lv_tileview_add_element( voicerec_app_main_tile, voicerec_gain_list );
    lv_tileview_add_element( voicerec_app_main_tile, exit_btn );
    lv_tileview_add_element( voicerec_app_main_tile, voicerec_play_btn );
    lv_tileview_add_element( voicerec_app_main_tile, voicerec_stop_btn );
    lv_tileview_add_element( voicerec_app_main_tile, right_btn );

    voicerec_app_main_task = lv_task_create( voicerec_app_main_lv_task, VOICEREC_APP_MAIN_PERIOD, LV_TASK_PRIO_OFF, NULL );
}

static void voicerec_app_main_activate_cb( void ) {
    lv_bar_set_value( voicerec_level_bar, 0, LV_ANIM_OFF );
    lv_task_set_prio( voicerec_app_main_task, LV_TASK_PRIO_MID );
}

static void voicerec_app_main_hibernate_cb( void ) {
    lv_task_set_prio( voicerec_app_main_task, LV_TASK_PRIO_OFF );

    if( voicerec_config_changed ) {
        voicerec_config.save();
        voicerec_config_changed = false;
    }
}

static void voicerec_app_main_lv_task( lv_task_t * task ) {
    voicerec_state_t state = voicerec_recorder_get_state();
    bool recording = state == VOICEREC_RECORDING;
    bool busy = recording || state == VOICEREC_FINALIZING;
    uint32_t remaining = voicerec_recorder_get_remaining_seconds( voicerec_config.low_quality );
    bool full = voicerec_recorder_get_disk_full() || ( !busy && remaining < VOICEREC_MAX_SECONDS );
    int16_t level = 0;

    lv_label_set_text_fmt( voicerec_timer_label, "%d / %d s", busy ? voicerec_recorder_get_seconds() : 0, VOICEREC_MAX_SECONDS );
    lv_obj_align( voicerec_timer_label, voicerec_app_main_tile, LV_ALIGN_IN_TOP_MID, 0, STATUSBAR_HEIGHT );

    if( state == VOICEREC_FINALIZING )
        lv_label_set_text( voicerec_state_label, "saving..." );
    else if( full )
        lv_label_set_text( voicerec_state_label, "storage full" );
    else
        lv_label_set_text_fmt( voicerec_state_label, "%d:%02d left", remaining / 60, remaining % 60 );

    lv_obj_align( voicerec_state_label, voicerec_level_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_PADDING );

    if( recording )
        level = ( int16_t )( ( micctl_dbfs_to_spl( voicerec_recorder_get_level_db() ) - VOICEREC_SPL_FLOOR ) * 100.0f / ( VOICEREC_SPL_CEIL - VOICEREC_SPL_FLOOR ) );

    lv_bar_set_value( voicerec_level_bar, level, LV_ANIM_ON );

    if( busy )
        lv_disp_trig_activity( NULL );

    lv_obj_set_hidden( voicerec_play_btn, busy );
    lv_obj_set_hidden( voicerec_stop_btn, !busy );
    lv_obj_set_click( voicerec_stop_btn, recording );
    lv_obj_set_click( voicerec_play_btn, !busy && !full );
    lv_obj_set_click( voicerec_quality_switch, !busy );
    lv_obj_set_click( voicerec_gain_list, !busy );
}

static void voicerec_app_main_play_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       voicerec_recorder_start( voicerec_config.low_quality, voicerec_recorder_get_gain( voicerec_config.gain ) );
                                        break;
    }
}

static void voicerec_app_main_stop_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       voicerec_recorder_stop();
                                        break;
    }
}

static void voicerec_app_main_quality_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_VALUE_CHANGED ): voicerec_config.low_quality = lv_switch_get_state( obj );
                                        voicerec_config_changed = true;
                                        break;
    }
}

static void voicerec_app_main_gain_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_VALUE_CHANGED ): voicerec_config.gain = lv_dropdown_get_selected( obj );
                                        voicerec_config_changed = true;
                                        break;
    }
}

static void voicerec_app_main_right_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       voicerec_app_slide( VOICEREC_APP_LIST_TILE );
                                        break;
    }
}

static void voicerec_app_main_exit_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       voicerec_recorder_stop();
                                        mainbar_jump_back();
                                        break;
    }
}
