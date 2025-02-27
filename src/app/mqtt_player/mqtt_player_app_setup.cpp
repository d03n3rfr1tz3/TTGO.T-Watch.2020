/****************************************************************************
 *   June 14 02:01:00 2021
 *   Copyright  2021  Dirk Sarodnick
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
#include <TTGO.h>

#include "mqtt_player_app.h"
#include "mqtt_player_app_setup.h"

#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/keyboard.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

lv_obj_t *mqtt_player_setup_tile = NULL;
lv_style_t mqtt_player_setup_style;

static void exit_mqtt_player_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static void mqtt_player_setup_hibernate_callback ( void );

void mqtt_player_setup_setup( uint32_t tile_num ) {
    mainbar_add_tile_hibernate_cb( tile_num, mqtt_player_setup_hibernate_callback );
    /**
     * get tile obj from tile number
     */
    mqtt_player_setup_tile = mainbar_get_tile_obj( tile_num );
    /**
     * get mainbar style, setup it and add it to setup tile
     */
    lv_style_copy( &mqtt_player_setup_style, ws_get_setup_tile_style() );
    lv_obj_add_style( mqtt_player_setup_tile, LV_OBJ_PART_MAIN, &mqtt_player_setup_style );
    /**
     * add setup header with exit button
     */
    lv_obj_t *header = wf_add_settings_header( mqtt_player_setup_tile, "MQTT Player setup", exit_mqtt_player_setup_event_cb );
    lv_obj_align( header, mqtt_player_setup_tile, LV_ALIGN_IN_TOP_LEFT, 10, 10 );
}

static void exit_mqtt_player_setup_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_jump_to_tilenumber( mqtt_player_get_app_main_tile_num(), LV_ANIM_ON );
                                        mqtt_player_save_config();
                                        break;
    }
}

static void mqtt_player_setup_hibernate_callback ( void ) {
    keyboard_hide();
    mqtt_player_save_config();
}