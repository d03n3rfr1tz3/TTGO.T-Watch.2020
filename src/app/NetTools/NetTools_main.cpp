/****************************************************************************
 *  NetTools_main.cpp
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

#ifndef NATIVE_64BIT
    #include <WiFi.h>
    #include <WiFiUdp.h>
    #include <WakeOnLan.h>
#endif

#include "NetTools.h"
#include "NetTools_main.h"
#include "NetTools_sniff.h"

#include "gui/mainbar/app_tile/app_tile.h"
#include "gui/mainbar/main_tile/main_tile.h"
#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"
#include "hardware/motor.h"

lv_obj_t *NetTools_main_tile = NULL;
lv_obj_t *NetTools_main_status_label = NULL;
lv_obj_t *NetTools_main_wol_btn[ NETTOOLS_TARGETS ] = { NULL };

static void exit_NetTools_main_event_cb( lv_obj_t * obj, lv_event_t event );
static void enter_NetTools_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static void enter_NetTools_sniff_event_cb( lv_obj_t * obj, lv_event_t event );
static void wol_send_NetTools_main_event_cb( lv_obj_t * obj, lv_event_t event );
static void NetTools_main_activate_cb( void );
static void NetTools_main_refresh( void );

void NetTools_main_setup( uint32_t tile_num ) {

    mainbar_add_tile_activate_cb( tile_num, NetTools_main_activate_cb );
    NetTools_main_tile = mainbar_get_tile_obj( tile_num );

    lv_obj_t *cont = wf_add_tile_container( NetTools_main_tile, LV_LAYOUT_COLUMN_MID );
    lv_obj_align( cont, NetTools_main_tile, LV_ALIGN_IN_TOP_MID, 0, 20 );

    for ( int i = 0 ; i < NETTOOLS_TARGETS ; i++ ) {
        NetTools_main_wol_btn[ i ] = wf_add_button( cont, "", 160, 40, wol_send_NetTools_main_event_cb );
        lv_btn_set_checkable( NetTools_main_wol_btn[ i ], false );
        lv_btn_set_state( NetTools_main_wol_btn[ i ], LV_BTN_STATE_RELEASED );
        lv_obj_set_user_data( NetTools_main_wol_btn[ i ], ( lv_obj_user_data_t )( intptr_t )i );
    }

    NetTools_main_status_label = wf_add_label( cont, "" );

    lv_obj_t *footer = wf_add_tile_footer_container( NetTools_main_tile, LV_LAYOUT_PRETTY_MID );
    lv_obj_t *exit_btn = wf_add_exit_button( footer, exit_NetTools_main_event_cb );
    lv_obj_t *setup_btn = wf_add_setup_button( footer, enter_NetTools_setup_event_cb );
    lv_obj_t *sniff_btn = wf_add_right_button( footer, enter_NetTools_sniff_event_cb );
    lv_obj_align( footer, NetTools_main_tile, LV_ALIGN_IN_BOTTOM_MID, 0, -10 );

    mainbar_add_slide_element( footer );
    mainbar_add_slide_element( exit_btn );
    mainbar_add_slide_element( setup_btn );
    mainbar_add_slide_element( sniff_btn );

    NetTools_main_refresh();
}

static void NetTools_main_activate_cb( void ) {
    NetTools_main_refresh();
    NetTools_sniff_arm();
}

static void NetTools_main_refresh( void ) {
    nettools_config_t *NetTools_config = NetTools_get_config();

    for ( int i = 0 ; i < NETTOOLS_TARGETS ; i++ ) {
        if ( !NetTools_main_wol_btn[ i ] )
            continue;

        bool used = strlen( NetTools_config->target[ i ].mac ) > 0;
        lv_obj_set_hidden( NetTools_main_wol_btn[ i ], !used );
        if ( used ) {
            const char *name = NetTools_config->target[ i ].name;
            lv_label_set_text( lv_obj_get_child( NetTools_main_wol_btn[ i ], NULL ), strlen( name ) > 0 ? name : NetTools_config->target[ i ].mac );
        }
    }

    lv_label_set_text( NetTools_main_status_label, "" );
}

static void wol_send_NetTools_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ): {
            int slot = ( int )( intptr_t )lv_obj_get_user_data( obj );
            if ( slot < 0 || slot >= NETTOOLS_TARGETS )
                break;

            nettools_config_t *NetTools_config = NetTools_get_config();
            const char *mac = NetTools_config->target[ slot ].mac;

            if ( !nettools_mac_valid( mac ) ) {
                lv_label_set_text( NetTools_main_status_label, "invalid MAC" );
                motor_vibe( 7 );
                break;
            }

            #ifdef NATIVE_64BIT
                lv_label_set_text( NetTools_main_status_label, "no wifi" );
            #else
                if ( !WiFi.isConnected() ) {
                    log_i("WIFI is disconnected, nothing to do.");
                    lv_label_set_text( NetTools_main_status_label, "no wifi" );
                    motor_vibe( 7 );
                    break;
                }

                log_i("WIFI is connected, sending packet! (Target: %s)", mac );

                WiFiUDP UDP;
                WakeOnLan WOL( UDP );
                motor_vibe( 7 );

                // some access points drop the limited broadcast
                bool sent = WOL.sendMagicPacket( mac );
                WOL.setBroadcastAddress( WOL.calculateBroadcastAddress( WiFi.localIP(), WiFi.subnetMask() ) );
                sent |= WOL.sendMagicPacket( mac );

                lv_label_set_text( NetTools_main_status_label, sent ? "sent" : "send failed" );
            #endif
            break;
        }
    }
}

static void enter_NetTools_setup_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_jump_to_tilenumber( NetTools_get_app_setup_tile_num(), LV_ANIM_ON, true );
                                        break;
    }
}

static void enter_NetTools_sniff_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_slide_to_tilenumber( NetTools_get_app_sniff_tile_num(), LV_ANIM_ON );
                                        break;
    }
}

static void exit_NetTools_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_jump_back();
                                        break;
    }
}
