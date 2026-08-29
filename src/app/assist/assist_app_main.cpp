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

#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"
#include "hardware/sound.h"

static lv_obj_t *assist_app_main_tile = NULL;
static lv_obj_t *assist_state_label = NULL;
static lv_obj_t *assist_speak_switch = NULL;
static lv_obj_t *assist_gain_list = NULL;
static bool assist_speak = false;                               /** @brief follows the system sound setting, not stored */

static void assist_app_main_activate_cb( void );
static void assist_app_main_exit_event_cb( lv_obj_t * obj, lv_event_t event );
static void assist_app_main_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static void assist_app_main_speak_event_cb( lv_obj_t * obj, lv_event_t event );
static void assist_app_main_gain_event_cb( lv_obj_t * obj, lv_event_t event );

void assist_app_main_setup( uint32_t tile_num ) {
    assist_config_t *assist_config = assist_get_config();

    mainbar_add_tile_activate_cb( tile_num, assist_app_main_activate_cb );

    assist_app_main_tile = mainbar_get_tile_obj( tile_num );

    assist_state_label = wf_add_label( assist_app_main_tile, "ready" );
    lv_label_set_align( assist_state_label, LV_LABEL_ALIGN_CENTER );
    lv_obj_align( assist_state_label, assist_app_main_tile, LV_ALIGN_IN_TOP_MID, 0, STATUSBAR_HEIGHT + THEME_PADDING );

    lv_obj_t *exit_btn = wf_add_exit_button( assist_app_main_tile, assist_app_main_exit_event_cb );
    lv_obj_align( exit_btn, assist_app_main_tile, LV_ALIGN_IN_BOTTOM_LEFT, THEME_ICON_PADDING, -THEME_ICON_PADDING );

    lv_obj_t *setup_btn = wf_add_setup_button( assist_app_main_tile, assist_app_main_setup_event_cb );
    lv_obj_align( setup_btn, assist_app_main_tile, LV_ALIGN_IN_BOTTOM_RIGHT, -THEME_ICON_PADDING, -THEME_ICON_PADDING );

    lv_obj_t *gain_cont = wf_add_labeled_list( assist_app_main_tile, "gain", &assist_gain_list, ASSIST_GAIN_OPTIONS, assist_app_main_gain_event_cb, APP_STYLE );
    lv_dropdown_set_selected( assist_gain_list, assist_config->gain );
    lv_dropdown_set_dir( assist_gain_list, LV_DROPDOWN_DIR_UP );
    lv_dropdown_set_max_height( assist_gain_list, 4 * THEME_CONT_HEIGHT );
    lv_obj_align( gain_cont, assist_app_main_tile, LV_ALIGN_IN_BOTTOM_MID, 0, -( THEME_ICON_SIZE + 2 * THEME_ICON_PADDING ) );

    assist_speak = sound_get_enabled_config();
    lv_obj_t *speak_cont = wf_add_labeled_switch( assist_app_main_tile, "speak answer", &assist_speak_switch, assist_speak, assist_app_main_speak_event_cb, APP_STYLE );
    lv_obj_align( speak_cont, gain_cont, LV_ALIGN_OUT_TOP_MID, 0, -THEME_PADDING );

    lv_tileview_add_element( assist_app_main_tile, assist_state_label );
    lv_tileview_add_element( assist_app_main_tile, exit_btn );
    lv_tileview_add_element( assist_app_main_tile, setup_btn );
    lv_tileview_add_element( assist_app_main_tile, gain_cont );
    lv_tileview_add_element( assist_app_main_tile, assist_gain_list );
    lv_tileview_add_element( assist_app_main_tile, speak_cont );
    lv_tileview_add_element( assist_app_main_tile, assist_speak_switch );
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
        case( LV_EVENT_CLICKED ):       mainbar_jump_back();
                                        break;
    }
}
