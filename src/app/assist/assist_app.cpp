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
#include "assist_app_pair.h"
#include "assist_app_setup.h"
#include "assist_config.h"
#include "assist_stream.h"
#include "assist_tts.h"
#include "assist_widget.h"
#include "assist_ws.h"

#include "gui/mainbar/mainbar.h"
#include "gui/app.h"
#include "hardware/micctl.h"

uint32_t assist_app_main_tile_num;
uint32_t assist_app_setup_tile_num;

icon_t *assist_app = NULL;

LV_IMG_DECLARE(assist_app_64px);

static void enter_assist_app_event_cb( lv_obj_t * obj, lv_event_t event );

static int registed = app_autocall_function( &assist_app_setup, APP_PRIO( APP_GROUP_AUDIO, 3 ) );             /** @brief app autocall function */

void assist_app_setup( void ) {
    /*
     * check if app already registered for autocall
     */
    if( !registed ) {
        return;
    }
    /*
     * without a microphone there is nothing to ask with
     */
    if( !micctl_get_available() ) {
        return;
    }

    assist_get_config()->load();
    assist_ws_setup();
    assist_stream_setup();
    assist_tts_setup();
    assist_widget_setup();

    assist_app_main_tile_num = mainbar_add_app_tile( 1, 1, "assist app" );
    assist_app_setup_tile_num = mainbar_add_setup_tile( ASSIST_SETUP_TILES, 1, "assist setup" );
    assist_app = app_register( "assist", &assist_app_64px, enter_assist_app_event_cb );

    assist_app_main_setup( assist_app_main_tile_num );
    assist_app_setup_setup( assist_app_setup_tile_num );
    assist_app_pair_setup( assist_app_setup_tile_num + 1 );
}

uint32_t assist_app_get_app_main_tile_num( void ) {
    return( assist_app_main_tile_num );
}

uint32_t assist_app_get_setup_tile_num( void ) {
    return( assist_app_setup_tile_num );
}

uint32_t assist_app_get_pair_tile_num( void ) {
    return( assist_app_setup_tile_num + 1 );
}

static void enter_assist_app_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       app_hide_indicator( assist_app );
                                        mainbar_jump_to_tilenumber( assist_app_main_tile_num, LV_ANIM_OFF, true );
                                        break;
    }
}
