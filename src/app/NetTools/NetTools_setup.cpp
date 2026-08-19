/****************************************************************************
 *  NetTools_setup.cpp
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
#include "NetTools_setup.h"

#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/keyboard.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

#define NETTOOLS_ROW_HEIGHT 37

lv_obj_t *NetTools_setup_tile = NULL;
lv_obj_t *NetTools_name_textfield[ NETTOOLS_TARGETS ] = { NULL };
lv_obj_t *NetTools_mac_textfield[ NETTOOLS_TARGETS ] = { NULL };

static void exit_NetTools_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static void NetTools_textarea_event_cb( lv_obj_t * obj, lv_event_t event );
static void NetTools_setup_activate_cb( void );
static void NetTools_setup_hibernate_cb( void );
static void NetTools_setup_save_config( void );

void NetTools_setup_setup( uint32_t tile_num ) {

    mainbar_add_tile_activate_cb( tile_num, NetTools_setup_activate_cb );
    mainbar_add_tile_hibernate_cb( tile_num, NetTools_setup_hibernate_cb );

    NetTools_setup_tile = mainbar_get_tile_obj( tile_num );

    lv_obj_t *header = wf_add_settings_header( NetTools_setup_tile, "NetTools setup", exit_NetTools_setup_event_cb );
    lv_obj_align( header, NetTools_setup_tile, LV_ALIGN_IN_TOP_LEFT, 10, 10 );

    lv_coord_t hor_res = lv_disp_get_hor_res( NULL );
    lv_obj_t *prev = header;

    for ( int i = 0 ; i < NETTOOLS_TARGETS ; i++ ) {
        lv_obj_t *row = lv_obj_create( NetTools_setup_tile, NULL );
        lv_obj_set_size( row, hor_res, NETTOOLS_ROW_HEIGHT );
        lv_obj_add_style( row, LV_OBJ_PART_MAIN, SETUP_STYLE );
        lv_obj_align( row, prev, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_ICON_PADDING );

        NetTools_name_textfield[ i ] = lv_textarea_create( row, NULL );
        lv_textarea_set_text( NetTools_name_textfield[ i ], "" );
        lv_textarea_set_pwd_mode( NetTools_name_textfield[ i ], false );
        lv_textarea_set_one_line( NetTools_name_textfield[ i ], true );
        lv_textarea_set_cursor_hidden( NetTools_name_textfield[ i ], true );
        lv_textarea_set_max_length( NetTools_name_textfield[ i ], NETTOOLS_NAME_LEN - 1 );
        lv_obj_set_width( NetTools_name_textfield[ i ], hor_res / 3 - THEME_ICON_PADDING );
        lv_obj_align( NetTools_name_textfield[ i ], row, LV_ALIGN_IN_LEFT_MID, THEME_ICON_PADDING, 0 );
        lv_obj_set_event_cb( NetTools_name_textfield[ i ], NetTools_textarea_event_cb );

        NetTools_mac_textfield[ i ] = lv_textarea_create( row, NULL );
        lv_textarea_set_text( NetTools_mac_textfield[ i ], "" );
        lv_textarea_set_pwd_mode( NetTools_mac_textfield[ i ], false );
        lv_textarea_set_one_line( NetTools_mac_textfield[ i ], true );
        lv_textarea_set_cursor_hidden( NetTools_mac_textfield[ i ], true );
        // stringToArray takes colons, dashes and both letter cases
        lv_textarea_set_accepted_chars( NetTools_mac_textfield[ i ], "0123456789ABCDEFabcdef:-" );
        lv_textarea_set_max_length( NetTools_mac_textfield[ i ], NETTOOLS_MAC_LEN - 1 );
        lv_obj_set_width( NetTools_mac_textfield[ i ], hor_res / 3 * 2 - THEME_ICON_PADDING );
        lv_obj_align( NetTools_mac_textfield[ i ], row, LV_ALIGN_IN_RIGHT_MID, -THEME_ICON_PADDING, 0 );
        lv_obj_set_event_cb( NetTools_mac_textfield[ i ], NetTools_textarea_event_cb );

        lv_tileview_add_element( NetTools_setup_tile, row );
        prev = row;
    }

    NetTools_setup_activate_cb();
}

bool NetTools_setup_add_target( const char *mac, const char *host ) {
    nettools_config_t *NetTools_config = NetTools_get_config();

    if ( !nettools_mac_valid( mac ) )
        return( false );

    for ( int i = 0 ; i < NETTOOLS_TARGETS ; i++ ) {
        if ( strlen( NetTools_config->target[ i ].mac ) > 0 )
            continue;

        strncpy( NetTools_config->target[ i ].mac, mac, sizeof( NetTools_config->target[ i ].mac ) - 1 );
        if ( host && strlen( host ) > 0 ) {
            strncpy( NetTools_config->target[ i ].name, host, sizeof( NetTools_config->target[ i ].name ) - 1 );
        }
        else {
            // fall back to the last three octets, which are unique enough to tell targets apart
            snprintf( NetTools_config->target[ i ].name, sizeof( NetTools_config->target[ i ].name ), "%s", mac + ( strlen( mac ) > 8 ? strlen( mac ) - 8 : 0 ) );
        }
        return( true );
    }

    return( false );
}

static void NetTools_setup_activate_cb( void ) {
    nettools_config_t *NetTools_config = NetTools_get_config();

    for ( int i = 0 ; i < NETTOOLS_TARGETS ; i++ ) {
        if ( !NetTools_name_textfield[ i ] || !NetTools_mac_textfield[ i ] )
            continue;
        lv_textarea_set_text( NetTools_name_textfield[ i ], NetTools_config->target[ i ].name );
        lv_textarea_set_text( NetTools_mac_textfield[ i ], NetTools_config->target[ i ].mac );
    }
}

static void NetTools_setup_hibernate_cb( void ) {
    keyboard_hide();
    NetTools_setup_save_config();
}

static void NetTools_setup_save_config( void ) {
    nettools_config_t *NetTools_config = NetTools_get_config();

    for ( int i = 0 ; i < NETTOOLS_TARGETS ; i++ ) {
        if ( !NetTools_name_textfield[ i ] || !NetTools_mac_textfield[ i ] )
            continue;
        strncpy( NetTools_config->target[ i ].name, lv_textarea_get_text( NetTools_name_textfield[ i ] ), sizeof( NetTools_config->target[ i ].name ) - 1 );
        strncpy( NetTools_config->target[ i ].mac, lv_textarea_get_text( NetTools_mac_textfield[ i ] ), sizeof( NetTools_config->target[ i ].mac ) - 1 );
    }

    NetTools_config->save();
}

static void NetTools_textarea_event_cb( lv_obj_t * obj, lv_event_t event ) {
    if( event == LV_EVENT_CLICKED ) {
        keyboard_set_textarea( obj );
    }
}

static void exit_NetTools_setup_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_jump_back();
                                        break;
    }
}
