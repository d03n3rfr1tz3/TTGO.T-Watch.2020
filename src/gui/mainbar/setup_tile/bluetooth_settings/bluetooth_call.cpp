/****************************************************************************
 *   Aug 14 12:37:31 2020
 *   Copyright  2020  Dirk Brosswick
 *   Email: dirk.brosswick@googlemail.com
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
#include "ArduinoJson.h"
#include "bluetooth_call.h"
#include "bluetooth_message.h"

#include "gui/mainbar/mainbar.h"
#include "gui/mainbar/setup_tile/setup_tile.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"
#include "hardware/ble/gadgetbridge.h"
#include "hardware/powermgm.h"
#include "hardware/motor.h"

#include "utils/bluejsonrequest.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
    #include "utils/millis.h"
#else
    #include <Arduino.h>
#endif

/**
 * @brief vibration on every ring interval, in 10ms steps
 */
#define BLUETOOTH_CALL_RING_VIBE        3
#define BLUETOOTH_CALL_RING_INTERVAL    2000

lv_obj_t *bluetooth_call_tile=NULL;
lv_style_t bluetooth_call_style;
uint32_t bluetooth_call_tile_num;

lv_obj_t *bluetooth_call_img = NULL;
lv_obj_t *bluetooth_call_number_label = NULL;

static bool bluetooth_call_tile_active = false;
static bool bluetooth_call_incoming = false;
static bool bluetooth_call_accepted = false;
static bool bluetooth_call_handled = false;
static char bluetooth_call_caller[ 64 ] = "";
static lv_task_t *bluetooth_call_ring_task = NULL;

LV_IMG_DECLARE(call_ok_128px);
LV_FONT_DECLARE(Ubuntu_16px);
LV_FONT_DECLARE(Ubuntu_32px);
LV_FONT_DECLARE(Ubuntu_72px);

#if defined( BIG_THEME )
    lv_font_t *caller_font = &Ubuntu_72px;
#elif defined( MID_THEME )
    lv_font_t *caller_font = &Ubuntu_32px;
#else
    lv_font_t *caller_font = &Ubuntu_16px;
#endif


bool bluetooth_call_style_change_event_cb( EventBits_t event, void *arg );
static void exit_bluetooth_call_event_cb( lv_obj_t * obj, lv_event_t event );
static void reject_bluetooth_call_event_cb( lv_obj_t * obj, lv_event_t event );
static void bluetooth_call_activate_cb( void );
static void bluetooth_call_hibernate_cb( void );
bool bluetooth_call_event_cb( EventBits_t event, void *arg );
static void bluetooth_call_msg_pharse( BluetoothJsonRequest &doc );
static void bluetooth_call_ring_task_cb( lv_task_t *task );
static void bluetooth_call_ring_start( void );
static void bluetooth_call_ring_stop( void );
static void bluetooth_call_start( BluetoothJsonRequest &doc );
static void bluetooth_call_accept( void );
static void bluetooth_call_end( void );
static void bluetooth_call_close( void );
static void bluetooth_call_add_missed_call( void );

void bluetooth_call_tile_setup( void ) {
    // get an app tile and copy mainstyle
    bluetooth_call_tile_num = mainbar_add_app_tile( 1, 1, "bluetooth call" );
    bluetooth_call_tile = mainbar_get_tile_obj( bluetooth_call_tile_num );

    lv_style_copy( &bluetooth_call_style, APP_STYLE );
    lv_style_set_text_font( &bluetooth_call_style, LV_STATE_DEFAULT, caller_font );
    lv_obj_add_style( bluetooth_call_tile, LV_OBJ_PART_MAIN, &bluetooth_call_style );

    bluetooth_call_img = lv_img_create( bluetooth_call_tile, NULL );
    lv_img_set_src( bluetooth_call_img, &call_ok_128px );
    lv_obj_align( bluetooth_call_img, bluetooth_call_tile, LV_ALIGN_CENTER, 0, -THEME_ICON_SIZE / 2 );

    bluetooth_call_number_label = lv_label_create( bluetooth_call_tile, NULL);
    lv_obj_add_style( bluetooth_call_number_label, LV_OBJ_PART_MAIN, &bluetooth_call_style  );
    lv_label_set_text( bluetooth_call_number_label, "");
    lv_obj_align( bluetooth_call_number_label, bluetooth_call_img, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_PADDING );

    lv_obj_t * exit_btn = wf_add_exit_button( bluetooth_call_tile, exit_bluetooth_call_event_cb );
    lv_obj_align( exit_btn, bluetooth_call_tile, LV_ALIGN_IN_TOP_RIGHT, -THEME_PADDING, THEME_PADDING );

    lv_obj_t * reject_btn = wf_add_close_button( bluetooth_call_tile, reject_bluetooth_call_event_cb, SYSTEM_ICON_STYLE );
    lv_obj_align( reject_btn, bluetooth_call_tile, LV_ALIGN_IN_BOTTOM_MID, 0, -THEME_PADDING );

    gadgetbridge_register_cb( GADGETBRIDGE_JSON_MSG, bluetooth_call_event_cb, "bluetooth_call" );
    styles_register_cb( STYLE_CHANGE, bluetooth_call_style_change_event_cb, "bluetooth call style" );
    mainbar_add_tile_activate_cb( bluetooth_call_tile_num, bluetooth_call_activate_cb );
    mainbar_add_tile_hibernate_cb( bluetooth_call_tile_num, bluetooth_call_hibernate_cb );
}

bool bluetooth_call_style_change_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case STYLE_CHANGE:  lv_style_copy( &bluetooth_call_style, APP_STYLE );
                            lv_style_set_text_font( &bluetooth_call_style, LV_STATE_DEFAULT, caller_font );
                            break;
    }
    return( true );
}

bool bluetooth_call_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case GADGETBRIDGE_JSON_MSG:
            bluetooth_call_msg_pharse( *(BluetoothJsonRequest*)arg );
            break;
    }
    return( true );
}

static void bluetooth_call_activate_cb( void ) {
    bluetooth_call_tile_active = true;
}

static void bluetooth_call_hibernate_cb( void ) {
    bluetooth_call_tile_active = false;
    /*
     * stop ringing when the call screen is no longer in front
     */
    bluetooth_call_ring_stop();
}

static void exit_bluetooth_call_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):
            /*
             * mute the ringtone on the phone, the call itself keeps running
             */
            if( bluetooth_call_incoming && !bluetooth_call_accepted ) {
                gadgetbridge_send_msg( "\r\n{\"t\":\"call\",\"n\":\"IGNORE\"}\r\n" );
                bluetooth_call_handled = true;
            }
            bluetooth_call_close();
            break;
    }
}

static void reject_bluetooth_call_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):
            if( bluetooth_call_incoming && !bluetooth_call_accepted ) {
                gadgetbridge_send_msg( "\r\n{\"t\":\"call\",\"n\":\"REJECT\"}\r\n" );
                bluetooth_call_handled = true;
            }
            bluetooth_call_close();
            break;
    }
}

static void bluetooth_call_ring_task_cb( lv_task_t *task ) {
    if( !bluetooth_call_incoming ) {
        bluetooth_call_ring_task = NULL;
        lv_task_del( task );
        return;
    }
    motor_vibe( BLUETOOTH_CALL_RING_VIBE );
    lv_disp_trig_activity( NULL );
}

static void bluetooth_call_ring_start( void ) {
    if( bluetooth_call_ring_task )
        return;

    bluetooth_call_ring_task = lv_task_create( bluetooth_call_ring_task_cb, BLUETOOTH_CALL_RING_INTERVAL, LV_TASK_PRIO_MID, NULL );
}

static void bluetooth_call_ring_stop( void ) {
    if( !bluetooth_call_ring_task )
        return;

    lv_task_del( bluetooth_call_ring_task );
    bluetooth_call_ring_task = NULL;
}

static void bluetooth_call_start( BluetoothJsonRequest &doc ) {
    /*
     * gadgetbridge repeats the incoming msg, only alert once per call
     */
    if( bluetooth_call_incoming )
        return;

    bluetooth_call_incoming = true;
    bluetooth_call_accepted = false;
    bluetooth_call_handled = false;

    if( doc.containsKey("name") )
        strlcpy( bluetooth_call_caller, doc["name"], sizeof( bluetooth_call_caller ) );
    else if( doc.containsKey("number") )
        strlcpy( bluetooth_call_caller, doc["number"], sizeof( bluetooth_call_caller ) );
    else
        strlcpy( bluetooth_call_caller, "n/a", sizeof( bluetooth_call_caller ) );

    log_d("incoming call from %s", bluetooth_call_caller );

    lv_label_set_text( bluetooth_call_number_label, bluetooth_call_caller );
    lv_obj_align( bluetooth_call_number_label, bluetooth_call_img, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_PADDING );

    powermgm_set_event( POWERMGM_WAKEUP_REQUEST );
    lv_disp_trig_activity( NULL );
    mainbar_jump_to_tilenumber( bluetooth_call_tile_num, LV_ANIM_OFF, true );

    motor_vibe( 250 );
    bluetooth_call_ring_start();

    lv_obj_invalidate( lv_scr_act() );
}

static void bluetooth_call_accept( void ) {
    if( !bluetooth_call_incoming )
        return;

    log_d("call accepted");
    bluetooth_call_accepted = true;
    bluetooth_call_close();
}

static void bluetooth_call_end( void ) {
    if( !bluetooth_call_incoming )
        return;

    bool missed = !bluetooth_call_accepted && !bluetooth_call_handled;

    log_d("call ended, missed = %d", missed );
    bluetooth_call_close();
    /*
     * queue after closing, jumping to the msg tile needs a free history
     */
    if( missed )
        bluetooth_call_add_missed_call();
}

static void bluetooth_call_close( void ) {
    bluetooth_call_incoming = false;
    bluetooth_call_accepted = false;
    bluetooth_call_handled = false;
    bluetooth_call_ring_stop();
    /*
     * only jump back if we own the screen, jump_back pops the history
     */
    if( bluetooth_call_tile_active )
        mainbar_jump_back();
}

static void bluetooth_call_add_missed_call( void ) {
    char msg[ 256 ] = "";
    SpiRamJsonDocument doc( 512 );

    doc["t"] = "notify";
    doc["id"] = (long)millis();
    doc["src"] = "Call";
    doc["title"] = bluetooth_call_caller;
    doc["body"] = "missed call";

    if( serializeJson( doc, msg, sizeof( msg ) ) )
        bluetooth_message_queue_msg( msg );

    doc.clear();
}

static void bluetooth_call_msg_pharse( BluetoothJsonRequest &doc ) {
    if( !doc.isEqualKeyValue( "t", "call" ) || !doc.containsKey("cmd") )
        return;

    const char *cmd = doc["cmd"];

    if( !strcmp( cmd, "incoming" ) )
        bluetooth_call_start( doc );
    else if( !strcmp( cmd, "accept" ) || !strcmp( cmd, "start" ) )
        bluetooth_call_accept();
    else if( !strcmp( cmd, "end" ) || !strcmp( cmd, "reject" ) )
        bluetooth_call_end();
}
