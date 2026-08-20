/****************************************************************************
 *  NetTools.cpp
 *  Copyright  2020  David Stewart / NorthernDIY
 *  Email: genericsoftwaredeveloper@gmail.com
 *
 *  Requires Libraries:
 *      WakeOnLan by a7md0      https://github.com/a7md0/WakeOnLan
 *
 *  Based on the work of Dirk Brosswick,  sharandac / My-TTGO-Watch  Example_App"
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

#include "NetTools.h"
#include "NetTools_main.h"
#include "NetTools_setup.h"
#include "NetTools_sniff.h"

#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/app.h"
#include "gui/widget.h"

nettools_config_t NetTools_config;

uint32_t NetTools_main_tile_num;
uint32_t NetTools_sniff_tile_num;
uint32_t NetTools_setup_tile_num;

// app icon
icon_t *NetTools = NULL;

// declare you images or fonts you need
LV_IMG_DECLARE(NetTools_64px);

// declare callback functions for the app and widget icon to enter the app
static void enter_NetTools_event_cb( lv_obj_t * obj, lv_event_t event );

/*
 * automatic register the app setup function with explicit call in main.cpp
 */
static int registed = app_autocall_function( &NetTools_setup, APP_PRIO( APP_GROUP_NETWORK, 4 ) );           /** @brief app autocall function */

/*
 * setup routine for example app
 */
void NetTools_setup( void ) {
    /*
     * check if app already registered for autocall
     */
    if( !registed ) {
        return;
    }

    NetTools_config.load();
    NetTools_main_tile_num = mainbar_add_app_tile( 2, 1, "NetTools");
    NetTools_sniff_tile_num = NetTools_main_tile_num + 1;
    NetTools_setup_tile_num = mainbar_add_setup_tile( 1, 1, "NetTools setup");

    NetTools = app_register( "Net\nTools", &NetTools_64px, enter_NetTools_event_cb );

    NetTools_main_setup( NetTools_main_tile_num );
    NetTools_sniff_setup( NetTools_sniff_tile_num );
    NetTools_setup_setup( NetTools_setup_tile_num );
}

/*
 *
 */
uint32_t NetTools_get_app_main_tile_num( void ) {
    return( NetTools_main_tile_num );
}

/*
 *
 */
uint32_t NetTools_get_app_sniff_tile_num( void ) {
    return( NetTools_sniff_tile_num );
}

/*
 *
 */
uint32_t NetTools_get_app_setup_tile_num( void ) {
    return( NetTools_setup_tile_num );
}

/*
 *
 */
static void enter_NetTools_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       app_hide_indicator( NetTools );
                                        mainbar_jump_to_tilenumber( NetTools_main_tile_num, LV_ANIM_OFF, true );
                                        break;
    }
}

nettools_config_t *NetTools_get_config( void ) {
    return( &NetTools_config );
}
