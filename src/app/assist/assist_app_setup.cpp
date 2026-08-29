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

#include "assist_app_setup.h"
#include "assist_config.h"

#include "gui/keyboard.h"
#include "gui/mainbar/mainbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
    #include <string.h>
#else
    #include <Arduino.h>
#endif

static lv_obj_t *assist_app_setup_tile = NULL;
static lv_obj_t *assist_host_textfield = NULL;
static lv_obj_t *assist_port_textfield = NULL;
static lv_obj_t *assist_token_textfield = NULL;

static void assist_app_setup_hibernate_cb( void );
static void assist_app_setup_store( void );
static lv_obj_t *assist_app_setup_add_row( lv_obj_t *above, const char *text, lv_obj_t **ret_textfield, const char *value, lv_event_cb_t event_cb );
static void assist_app_setup_exit_event_cb( lv_obj_t * obj, lv_event_t event );
static void assist_app_setup_textarea_event_cb( lv_obj_t * obj, lv_event_t event );
static void assist_app_setup_num_textarea_event_cb( lv_obj_t * obj, lv_event_t event );

void assist_app_setup_setup( uint32_t tile_num ) {
    assist_config_t *assist_config = assist_get_config();
    char buf[ 8 ] = "";

    mainbar_add_tile_hibernate_cb( tile_num, assist_app_setup_hibernate_cb );

    assist_app_setup_tile = mainbar_get_tile_obj( tile_num );

    lv_obj_t *header = wf_add_settings_header( assist_app_setup_tile, "assist setup", assist_app_setup_exit_event_cb );
    lv_obj_align( header, assist_app_setup_tile, LV_ALIGN_IN_TOP_LEFT, 10, 10 );

    lv_obj_t *host_cont = assist_app_setup_add_row( header, "host", &assist_host_textfield, assist_config->host, assist_app_setup_textarea_event_cb );

    snprintf( buf, sizeof( buf ), "%d", assist_config->port );
    lv_obj_t *port_cont = assist_app_setup_add_row( host_cont, "port", &assist_port_textfield, buf, assist_app_setup_num_textarea_event_cb );

    lv_obj_t *token_cont = assist_app_setup_add_row( port_cont, "token", &assist_token_textfield, assist_config->token, assist_app_setup_textarea_event_cb );

    lv_tileview_add_element( assist_app_setup_tile, host_cont );
    lv_tileview_add_element( assist_app_setup_tile, port_cont );
    lv_tileview_add_element( assist_app_setup_tile, token_cont );
}

static lv_obj_t *assist_app_setup_add_row( lv_obj_t *above, const char *text, lv_obj_t **ret_textfield, const char *value, lv_event_cb_t event_cb ) {
    lv_obj_t *cont = lv_obj_create( assist_app_setup_tile, NULL );
    lv_obj_set_size( cont, lv_disp_get_hor_res( NULL ), ASSIST_SETUP_CONT_HEIGHT );
    lv_obj_add_style( cont, LV_OBJ_PART_MAIN, SETUP_STYLE );
    lv_obj_align( cont, above, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_ICON_PADDING );

    lv_obj_t *label = lv_label_create( cont, NULL );
    lv_obj_add_style( label, LV_OBJ_PART_MAIN, SETUP_STYLE );
    lv_label_set_text( label, text );
    lv_obj_align( label, cont, LV_ALIGN_IN_LEFT_MID, THEME_ICON_PADDING, 0 );

    lv_obj_t *textfield = lv_textarea_create( cont, NULL );
    lv_textarea_set_text( textfield, value );
    lv_textarea_set_pwd_mode( textfield, false );
    lv_textarea_set_one_line( textfield, true );
    lv_textarea_set_cursor_hidden( textfield, true );
    lv_obj_set_width( textfield, lv_disp_get_hor_res( NULL ) / 4 * 3 - THEME_ICON_PADDING );
    lv_obj_align( textfield, cont, LV_ALIGN_IN_RIGHT_MID, -THEME_ICON_PADDING, 0 );
    lv_obj_set_event_cb( textfield, event_cb );

    *ret_textfield = textfield;

    return( cont );
}

static void assist_app_setup_hibernate_cb( void ) {
    keyboard_hide();
    assist_app_setup_store();
    assist_config_save_dirty();
}

static void assist_app_setup_store( void ) {
    assist_config_t *assist_config = assist_get_config();
    const char *host = lv_textarea_get_text( assist_host_textfield );
    const char *token = lv_textarea_get_text( assist_token_textfield );
    uint16_t port = atoi( lv_textarea_get_text( assist_port_textfield ) );

    if( !port )
        port = ASSIST_PORT_DEFAULT;

    if( strcmp( assist_config->host, host ) ) {
        snprintf( assist_config->host, sizeof( assist_config->host ), "%s", host );
        assist_config_set_dirty();
    }

    if( strcmp( assist_config->token, token ) ) {
        snprintf( assist_config->token, sizeof( assist_config->token ), "%s", token );
        assist_config_set_dirty();
    }

    if( assist_config->port != port ) {
        assist_config->port = port;
        assist_config_set_dirty();
    }
}

static void assist_app_setup_textarea_event_cb( lv_obj_t * obj, lv_event_t event ) {
    if( event == LV_EVENT_CLICKED ) {
        keyboard_set_textarea( obj );
    }
}

static void assist_app_setup_num_textarea_event_cb( lv_obj_t * obj, lv_event_t event ) {
    if( event == LV_EVENT_CLICKED ) {
        num_keyboard_set_textarea( obj );
    }
}

static void assist_app_setup_exit_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       keyboard_hide();
                                        mainbar_jump_back();
                                        break;
    }
}
