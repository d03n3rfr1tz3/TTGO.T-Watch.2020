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

#include "soundmeter_app.h"
#include "soundmeter_app_main.h"

#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/app.h"
#include "hardware/micctl.h"

uint32_t soundmeter_app_main_tile_num;

icon_t *soundmeter_app = NULL;

LV_IMG_DECLARE(soundmeter_app_64px);

static void enter_soundmeter_app_event_cb( lv_obj_t * obj, lv_event_t event );

/*
 * automatic register the app setup function with explicit call in main.cpp
 */
static int registed = app_autocall_function( &soundmeter_app_setup, APP_PRIO( APP_GROUP_AUDIO, 0 ) );           /** @brief app autocall function */

void soundmeter_app_setup( void ) {
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

    soundmeter_app_main_tile_num = mainbar_add_app_tile( 1, 1, "soundmeter app" );

    soundmeter_app = app_register( "sound\nmeter", &soundmeter_app_64px, enter_soundmeter_app_event_cb );

    soundmeter_app_main_setup( soundmeter_app_main_tile_num );
}

uint32_t soundmeter_app_get_app_main_tile_num( void ) {
    return( soundmeter_app_main_tile_num );
}

static void enter_soundmeter_app_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       app_hide_indicator( soundmeter_app );
                                        mainbar_jump_to_tilenumber( soundmeter_app_main_tile_num, LV_ANIM_OFF, true );
                                        break;
    }
}
