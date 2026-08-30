/****************************************************************************
 *   Aug 29 20:00:00 2026
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

#include "assist_app.h"
#include "assist_app_main.h"
#include "assist_config.h"
#include "assist_stream.h"
#include "assist_tts.h"
#include "assist_ws.h"

#include "gui/mainbar/mainbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"
#include "hardware/micctl.h"
#include "hardware/sound.h"

LV_FONT_DECLARE(Ubuntu_16px);                                   /** @brief the only font in the tree that carries latin-1, montserrat stops at ascii */

static lv_obj_t *assist_app_main_tile = NULL;
static lv_obj_t *assist_state_label = NULL;
static lv_obj_t *assist_level_bar = NULL;
static lv_obj_t *assist_answer_page = NULL;
static lv_obj_t *assist_answer_label = NULL;
static lv_obj_t *assist_speak_switch = NULL;
static lv_obj_t *assist_gain_list = NULL;
static lv_obj_t *assist_talk_btn = NULL;
static lv_obj_t *assist_send_btn = NULL;
static lv_obj_t *assist_cancel_btn = NULL;
static lv_task_t *assist_app_main_task = NULL;
static lv_style_t assist_answer_style;

static bool assist_speak = false;                               /** @brief follows the system sound setting, not stored */
static bool assist_connect_wanted = false;
static bool assist_no_answer = false;                           /** @brief the run ran into ASSIST_WS_RUN_TIMEOUT */
static uint32_t assist_run_start = 0;

static void assist_app_main_activate_cb( void );
static void assist_app_main_hibernate_cb( void );
static void assist_app_main_lv_task( lv_task_t * task );
static void assist_app_main_exit_event_cb( lv_obj_t * obj, lv_event_t event );
static void assist_app_main_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static void assist_app_main_talk_event_cb( lv_obj_t * obj, lv_event_t event );
static void assist_app_main_send_event_cb( lv_obj_t * obj, lv_event_t event );
static void assist_app_main_cancel_event_cb( lv_obj_t * obj, lv_event_t event );
static void assist_app_main_speak_event_cb( lv_obj_t * obj, lv_event_t event );
static void assist_app_main_gain_event_cb( lv_obj_t * obj, lv_event_t event );

void assist_app_main_setup( uint32_t tile_num ) {
    assist_config_t *assist_config = assist_get_config();
    lv_coord_t hor_res = lv_disp_get_hor_res( NULL );
    lv_coord_t label_height = 0;
    lv_coord_t page_y = 0;

    mainbar_add_tile_activate_cb( tile_num, assist_app_main_activate_cb );
    mainbar_add_tile_hibernate_cb( tile_num, assist_app_main_hibernate_cb );

    assist_app_main_tile = mainbar_get_tile_obj( tile_num );

    assist_state_label = wf_add_label( assist_app_main_tile, "ready" );
    lv_obj_set_style_local_text_font( assist_state_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &Ubuntu_16px );
    label_height = lv_obj_get_height( assist_state_label );
    lv_label_set_long_mode( assist_state_label, LV_LABEL_LONG_DOT );
    lv_label_set_align( assist_state_label, LV_LABEL_ALIGN_CENTER );
    lv_obj_set_size( assist_state_label, hor_res - 2 * THEME_PADDING, label_height );
    lv_obj_align( assist_state_label, assist_app_main_tile, LV_ALIGN_IN_TOP_MID, 0, THEME_PADDING );

    assist_level_bar = lv_bar_create( assist_app_main_tile, NULL );
    lv_obj_set_size( assist_level_bar, hor_res - 4 * THEME_PADDING, ASSIST_BAR_HEIGHT );
    lv_obj_set_click( assist_level_bar, false );
    lv_bar_set_range( assist_level_bar, 0, 100 );
    lv_bar_set_anim_time( assist_level_bar, ASSIST_APP_MAIN_PERIOD );
    lv_bar_set_value( assist_level_bar, 0, LV_ANIM_OFF );
    lv_obj_align( assist_level_bar, assist_state_label, LV_ALIGN_OUT_BOTTOM_MID, 0, ASSIST_TIGHT_PADDING );

    lv_obj_t *exit_btn = wf_add_exit_button( assist_app_main_tile, assist_app_main_exit_event_cb );
    lv_obj_align( exit_btn, assist_app_main_tile, LV_ALIGN_IN_BOTTOM_LEFT, THEME_ICON_PADDING, -THEME_ICON_PADDING );

    lv_obj_t *setup_btn = wf_add_setup_button( assist_app_main_tile, assist_app_main_setup_event_cb );
    lv_obj_align( setup_btn, assist_app_main_tile, LV_ALIGN_IN_BOTTOM_RIGHT, -THEME_ICON_PADDING, -THEME_ICON_PADDING );

    assist_talk_btn = wf_add_play_button( assist_app_main_tile, assist_app_main_talk_event_cb );
    lv_obj_align( assist_talk_btn, assist_app_main_tile, LV_ALIGN_IN_BOTTOM_MID, 0, -THEME_ICON_PADDING );

    assist_send_btn = wf_add_stop_button( assist_app_main_tile, assist_app_main_send_event_cb );
    lv_obj_align( assist_send_btn, assist_app_main_tile, LV_ALIGN_IN_BOTTOM_MID, 0, -THEME_ICON_PADDING );
    lv_obj_set_hidden( assist_send_btn, true );

    assist_cancel_btn = wf_add_close_button( assist_app_main_tile, assist_app_main_cancel_event_cb );
    lv_obj_align( assist_cancel_btn, assist_app_main_tile, LV_ALIGN_IN_BOTTOM_MID, 0, -THEME_ICON_PADDING );
    lv_obj_set_hidden( assist_cancel_btn, true );

    lv_obj_t *gain_cont = wf_add_labeled_list( assist_app_main_tile, "gain", &assist_gain_list, ASSIST_GAIN_OPTIONS, assist_app_main_gain_event_cb, APP_STYLE );
    lv_obj_set_style_local_pad_top( assist_gain_list, LV_DROPDOWN_PART_MAIN, LV_STATE_DEFAULT, ASSIST_ROW_PADDING );
    lv_obj_set_style_local_pad_bottom( assist_gain_list, LV_DROPDOWN_PART_MAIN, LV_STATE_DEFAULT, ASSIST_ROW_PADDING );
    lv_dropdown_set_selected( assist_gain_list, assist_config->gain );
    lv_dropdown_set_dir( assist_gain_list, LV_DROPDOWN_DIR_UP );
    lv_dropdown_set_max_height( assist_gain_list, 4 * THEME_CONT_HEIGHT );
    lv_obj_align( gain_cont, assist_app_main_tile, LV_ALIGN_IN_BOTTOM_MID, 0, -( THEME_ICON_SIZE + THEME_ICON_PADDING + ASSIST_TIGHT_PADDING ) );

    assist_speak = sound_get_enabled_config();
    lv_obj_t *speak_cont = wf_add_labeled_switch( assist_app_main_tile, "speak answer", &assist_speak_switch, assist_speak, assist_app_main_speak_event_cb, APP_STYLE );
    lv_obj_set_size( assist_speak_switch, ASSIST_SWITCH_WIDTH, ASSIST_SWITCH_HEIGHT );
    lv_obj_align( speak_cont, gain_cont, LV_ALIGN_OUT_TOP_MID, 0, -ASSIST_TIGHT_PADDING );

    lv_style_init( &assist_answer_style );
    lv_style_set_border_width( &assist_answer_style, LV_OBJ_PART_MAIN, 0 );
    lv_style_set_radius( &assist_answer_style, LV_OBJ_PART_MAIN, 0 );
    lv_style_set_bg_opa( &assist_answer_style, LV_OBJ_PART_MAIN, LV_OPA_0 );

    page_y = lv_obj_get_y( assist_level_bar ) + lv_obj_get_height( assist_level_bar ) + ASSIST_TIGHT_PADDING;

    assist_answer_page = lv_page_create( assist_app_main_tile, NULL );
    lv_obj_set_size( assist_answer_page, hor_res - 2 * THEME_PADDING, lv_obj_get_y( speak_cont ) - ASSIST_TIGHT_PADDING - page_y );
    lv_obj_add_style( assist_answer_page, LV_OBJ_PART_MAIN, &assist_answer_style );
    lv_obj_set_style_local_pad_all( assist_answer_page, LV_PAGE_PART_BG, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_pad_all( assist_answer_page, LV_PAGE_PART_SCROLLABLE, LV_STATE_DEFAULT, 0 );
    lv_page_set_scrlbar_mode( assist_answer_page, LV_SCRLBAR_MODE_DRAG );
    lv_obj_align( assist_answer_page, assist_app_main_tile, LV_ALIGN_IN_TOP_MID, 0, page_y );

    assist_answer_label = wf_add_label( assist_answer_page, "", APP_STYLE );
    lv_obj_set_style_local_text_font( assist_answer_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &Ubuntu_16px );
    lv_label_set_long_mode( assist_answer_label, LV_LABEL_LONG_BREAK );
    lv_obj_set_width( assist_answer_label, lv_page_get_width_fit( assist_answer_page ) );

    lv_tileview_add_element( assist_app_main_tile, assist_state_label );
    lv_tileview_add_element( assist_app_main_tile, assist_level_bar );
    lv_tileview_add_element( assist_app_main_tile, exit_btn );
    lv_tileview_add_element( assist_app_main_tile, setup_btn );
    lv_tileview_add_element( assist_app_main_tile, assist_talk_btn );
    lv_tileview_add_element( assist_app_main_tile, assist_send_btn );
    lv_tileview_add_element( assist_app_main_tile, assist_cancel_btn );
    lv_tileview_add_element( assist_app_main_tile, gain_cont );
    lv_tileview_add_element( assist_app_main_tile, assist_gain_list );
    lv_tileview_add_element( assist_app_main_tile, speak_cont );
    lv_tileview_add_element( assist_app_main_tile, assist_speak_switch );

    assist_app_main_task = lv_task_create( assist_app_main_lv_task, ASSIST_APP_MAIN_PERIOD, LV_TASK_PRIO_OFF, NULL );
}

bool assist_app_main_get_speak( void ) {
    return( assist_speak );
}

/*
 * the switch default state is based on the sound active state, but can be changed independently
 */
static void assist_app_main_activate_cb( void ) {
    assist_speak = sound_get_enabled_config();

    if( assist_speak )
        lv_switch_on( assist_speak_switch, LV_ANIM_OFF );
    else
        lv_switch_off( assist_speak_switch, LV_ANIM_OFF );

    lv_label_set_text( assist_answer_label, "" );
    lv_bar_set_value( assist_level_bar, 0, LV_ANIM_OFF );
    assist_no_answer = false;
    assist_connect_wanted = true;
    lv_task_set_prio( assist_app_main_task, LV_TASK_PRIO_MID );
}

static void assist_app_main_hibernate_cb( void ) {
    lv_task_set_prio( assist_app_main_task, LV_TASK_PRIO_OFF );
    assist_connect_wanted = false;

    assist_stream_abort();
    assist_tts_stop();
    assist_config_save_dirty();
    assist_ws_disconnect();
}

static void assist_app_main_lv_task( lv_task_t * task ) {
    assist_config_t *assist_config = assist_get_config();
    assist_ws_run_t run = assist_ws_get_run();
    assist_stream_state_t stream = assist_stream_get_state();
    assist_tts_state_t tts = assist_tts_get_state();
    bool recording = ( stream == ASSIST_STREAM_RECORDING );
    bool waiting = ( run == ASSIST_RUN_STARTING || run == ASSIST_RUN_LISTENING || run == ASSIST_RUN_THINKING );
    bool speaking = ( tts == ASSIST_TTS_LOADING || tts == ASSIST_TTS_READY || tts == ASSIST_TTS_SPEAKING );
    bool busy = recording || waiting || speaking || stream == ASSIST_STREAM_SENDING;
    const char *message = assist_ws_get_message();
    int16_t level = 0;

    if( assist_connect_wanted && assist_ws_get_state() != ASSIST_WS_READY )
        assist_connect_wanted = !assist_ws_connect( assist_config->token );

    if( waiting && millis() - assist_run_start >= ASSIST_WS_RUN_TIMEOUT ) {
        assist_stream_abort();
        assist_no_answer = true;
        waiting = false;
        busy = recording || speaking;
    }

    if( recording )
        lv_label_set_text( assist_state_label, "listening..." );
    else if( assist_ws_get_transcript()[ 0 ] )
        lv_label_set_text( assist_state_label, assist_ws_get_transcript() );
    else if( waiting )
        lv_label_set_text( assist_state_label, "thinking..." );
    else if( assist_no_answer )
        lv_label_set_text( assist_state_label, "no answer" );
    else
        lv_label_set_text( assist_state_label, message[ 0 ] ? message : "ready" );

    if( assist_ws_take_text() )
        lv_label_set_text( assist_answer_label, assist_ws_get_answer() );

    if( assist_ws_take_tts() && assist_speak )
        assist_tts_fetch( assist_ws_get_tts_url() );

    assist_tts_update();

    if( recording )
        level = ( int16_t )( ( micctl_dbfs_to_spl( assist_stream_get_level_db() ) - ASSIST_SPL_FLOOR ) * 100.0f / ( ASSIST_SPL_CEIL - ASSIST_SPL_FLOOR ) );

    lv_bar_set_value( assist_level_bar, level, LV_ANIM_ON );

    if( busy )
        lv_disp_trig_activity( NULL );

    lv_obj_set_hidden( assist_talk_btn, busy );
    lv_obj_set_hidden( assist_send_btn, !recording );
    lv_obj_set_hidden( assist_cancel_btn, recording || !busy );
    lv_obj_set_click( assist_talk_btn, !busy );
    lv_obj_set_click( assist_speak_switch, !busy );
    lv_obj_set_click( assist_gain_list, !busy );
}

static void assist_app_main_talk_event_cb( lv_obj_t * obj, lv_event_t event ) {
    assist_config_t *assist_config = assist_get_config();

    switch( event ) {
        case( LV_EVENT_CLICKED ):       if( assist_ws_get_state() != ASSIST_WS_READY )
                                            break;

                                        lv_label_set_text( assist_answer_label, "" );
                                        assist_no_answer = false;
                                        assist_run_start = millis();
                                        assist_tts_stop();
                                        assist_stream_start( assist_config->pipeline, assist_config->gain, assist_speak );
                                        break;
    }
}

static void assist_app_main_send_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       assist_stream_stop();
                                        break;
    }
}

static void assist_app_main_cancel_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       assist_stream_abort();
                                        assist_tts_stop();
                                        break;
    }
}

static void assist_app_main_speak_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_VALUE_CHANGED ): assist_speak = lv_switch_get_state( obj );
                                        break;
    }
}

static void assist_app_main_gain_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_VALUE_CHANGED ): assist_get_config()->gain = lv_dropdown_get_selected( obj );
                                        assist_config_set_dirty();
                                        break;
    }
}

static void assist_app_main_setup_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_jump_to_tilenumber( assist_app_get_setup_tile_num(), LV_ANIM_ON, true );
                                        break;
    }
}

static void assist_app_main_exit_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       assist_stream_abort();
                                        assist_tts_stop();
                                        mainbar_jump_back();
                                        break;
    }
}
