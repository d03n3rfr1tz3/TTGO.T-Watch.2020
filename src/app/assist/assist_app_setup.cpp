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
#include "assist_app_pair.h"
#include "assist_app_setup.h"
#include "assist_config.h"
#include "assist_ws.h"

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
static lv_obj_t *assist_pair_button = NULL;
static lv_obj_t *assist_pair_state_label = NULL;
static lv_obj_t *assist_pipeline_list = NULL;
static lv_obj_t *assist_setup_state_label = NULL;
static lv_task_t *assist_app_setup_task = NULL;
static bool assist_setup_connect_wanted = false;
static bool assist_setup_visible = false;

static void assist_app_setup_activate_cb( void );
static void assist_app_setup_hibernate_cb( void );
static void assist_app_setup_lv_task( lv_task_t * task );
static bool assist_app_setup_is_visible( void );
static void assist_app_setup_store( void );
static lv_obj_t *assist_app_setup_add_row( lv_obj_t *above, const char *text, lv_obj_t **ret_textfield, const char *value, lv_event_cb_t event_cb );
static lv_obj_t *assist_app_setup_add_pair_row( lv_obj_t *above );
static void assist_app_setup_fill_pipelines( void );
static void assist_app_setup_pair_event_cb( lv_obj_t * obj, lv_event_t event );
static void assist_app_setup_pipeline_event_cb( lv_obj_t * obj, lv_event_t event );
static void assist_app_setup_exit_event_cb( lv_obj_t * obj, lv_event_t event );
static void assist_app_setup_textarea_event_cb( lv_obj_t * obj, lv_event_t event );
static void assist_app_setup_num_textarea_event_cb( lv_obj_t * obj, lv_event_t event );

void assist_app_setup_setup( uint32_t tile_num ) {
    assist_config_t *assist_config = assist_get_config();
    char buf[ 8 ] = "";

    mainbar_add_tile_activate_cb( tile_num, assist_app_setup_activate_cb );
    mainbar_add_tile_hibernate_cb( tile_num, assist_app_setup_hibernate_cb );

    assist_app_setup_tile = mainbar_get_tile_obj( tile_num );

    lv_obj_t *header = wf_add_settings_header( assist_app_setup_tile, "assist setup", assist_app_setup_exit_event_cb );
    lv_obj_align( header, assist_app_setup_tile, LV_ALIGN_IN_TOP_LEFT, 10, 10 );

    lv_obj_t *host_cont = assist_app_setup_add_row( header, "host", &assist_host_textfield, assist_config->host, assist_app_setup_textarea_event_cb );

    snprintf( buf, sizeof( buf ), "%d", assist_config->port );
    lv_obj_t *port_cont = assist_app_setup_add_row( host_cont, "port", &assist_port_textfield, buf, assist_app_setup_num_textarea_event_cb );

    lv_obj_t *pair_cont = assist_app_setup_add_pair_row( port_cont );

    lv_obj_t *pipeline_cont = wf_add_labeled_list( assist_app_setup_tile, "pipeline", &assist_pipeline_list, ASSIST_SETUP_PIPELINE_PREFERRED, assist_app_setup_pipeline_event_cb, SETUP_STYLE );
    lv_dropdown_set_max_height( assist_pipeline_list, ASSIST_SETUP_LIST_HEIGHT );
    lv_obj_align( pipeline_cont, pair_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_ICON_PADDING );

    assist_setup_state_label = wf_add_label( assist_app_setup_tile, "", SETUP_STYLE );
    lv_label_set_long_mode( assist_setup_state_label, LV_LABEL_LONG_BREAK );
    lv_label_set_align( assist_setup_state_label, LV_LABEL_ALIGN_CENTER );
    lv_obj_set_width( assist_setup_state_label, lv_disp_get_hor_res( NULL ) - 2 * THEME_ICON_PADDING );
    lv_obj_align( assist_setup_state_label, pipeline_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_ICON_PADDING );

    lv_tileview_add_element( assist_app_setup_tile, host_cont );
    lv_tileview_add_element( assist_app_setup_tile, port_cont );
    lv_tileview_add_element( assist_app_setup_tile, pair_cont );
    lv_tileview_add_element( assist_app_setup_tile, pipeline_cont );
    lv_tileview_add_element( assist_app_setup_tile, assist_setup_state_label );

    assist_app_setup_task = lv_task_create( assist_app_setup_lv_task, ASSIST_SETUP_PERIOD, LV_TASK_PRIO_OFF, NULL );
}

static void assist_app_setup_activate_cb( void ) {
    lv_label_set_text( assist_setup_state_label, "" );
    assist_app_setup_enable( true );
    assist_app_pair_enable( true );
}

void assist_app_setup_enable( bool enable ) {
    if( !enable ) {
        assist_setup_connect_wanted = false;
        assist_setup_visible = false;
    }

    lv_task_set_prio( assist_app_setup_task, enable ? LV_TASK_PRIO_MID : LV_TASK_PRIO_OFF );
}

static bool assist_app_setup_is_visible( void ) {
    lv_area_t area;

    lv_obj_get_coords( assist_app_setup_tile, &area );

    return( abs( area.x1 ) + abs( area.y1 ) < lv_disp_get_hor_res( NULL ) / 2 );
}

static void assist_app_setup_lv_task( lv_task_t * task ) {
    assist_config_t *assist_config = assist_get_config();
    bool visible = assist_app_setup_is_visible();

    if( visible != assist_setup_visible ) {
        assist_setup_visible = visible;

        if( visible ) {
            lv_label_set_text( assist_setup_state_label, "" );
            assist_setup_connect_wanted = true;
        }
        else {
            assist_app_setup_leave();
            return;
        }
    }

    if( !visible )
        return;

    if( assist_setup_connect_wanted && assist_ws_get_state() != ASSIST_WS_READY )
        assist_setup_connect_wanted = !assist_ws_connect( assist_config->token );

    if( assist_ws_take_pipelines() )
        assist_app_setup_fill_pipelines();

    lv_label_set_text( assist_setup_state_label, assist_ws_get_message() );
    lv_label_set_text( assist_pair_state_label, assist_config->token[ 0 ] ? "paired" : "not paired" );
    lv_obj_align( assist_pair_state_label, assist_pair_button, LV_ALIGN_OUT_LEFT_MID, -THEME_ICON_PADDING, 0 );
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

static lv_obj_t *assist_app_setup_add_pair_row( lv_obj_t *above ) {
    lv_obj_t *cont = lv_obj_create( assist_app_setup_tile, NULL );
    lv_obj_set_size( cont, lv_disp_get_hor_res( NULL ), ASSIST_SETUP_CONT_HEIGHT );
    lv_obj_add_style( cont, LV_OBJ_PART_MAIN, SETUP_STYLE );
    lv_obj_align( cont, above, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_ICON_PADDING );

    lv_obj_t *label = lv_label_create( cont, NULL );
    lv_obj_add_style( label, LV_OBJ_PART_MAIN, SETUP_STYLE );
    lv_label_set_text( label, "pairing" );
    lv_obj_align( label, cont, LV_ALIGN_IN_LEFT_MID, THEME_ICON_PADDING, 0 );

    assist_pair_button = wf_add_right_button( cont, assist_app_setup_pair_event_cb );
    lv_obj_set_size( assist_pair_button, wf_get_right_img().header.w, wf_get_right_img().header.h );
    lv_obj_align( assist_pair_button, cont, LV_ALIGN_IN_RIGHT_MID, -THEME_ICON_PADDING, 0 );

    assist_pair_state_label = lv_label_create( cont, NULL );
    lv_obj_add_style( assist_pair_state_label, LV_OBJ_PART_MAIN, SETUP_STYLE );
    lv_label_set_text( assist_pair_state_label, "" );
    lv_obj_align( assist_pair_state_label, assist_pair_button, LV_ALIGN_OUT_LEFT_MID, -THEME_ICON_PADDING, 0 );

    return( cont );
}

static void assist_app_setup_fill_pipelines( void ) {
    assist_config_t *assist_config = assist_get_config();
    char options[ ASSIST_WS_PIPELINE_MAX * ASSIST_WS_PIPELINE_NAME_LEN + sizeof( ASSIST_SETUP_PIPELINE_PREFERRED ) + 1 ] = "";
    uint8_t count = assist_ws_get_pipeline_count();

    snprintf( options, sizeof( options ), "%s\n%s", ASSIST_SETUP_PIPELINE_PREFERRED, assist_ws_get_pipeline_options() );
    lv_dropdown_set_options( assist_pipeline_list, options );

    for( uint8_t index = 0 ; index < count ; index++ ) {
        if( !strcmp( assist_ws_get_pipeline_id( index ), assist_config->pipeline ) ) {
            lv_dropdown_set_selected( assist_pipeline_list, index + 1 );
            return;
        }
    }

    lv_dropdown_set_selected( assist_pipeline_list, 0 );
}

static void assist_app_setup_hibernate_cb( void ) {
    assist_app_setup_leave();
    assist_app_setup_enable( false );
    assist_app_pair_enable( false );
}

void assist_app_setup_leave( void ) {
    assist_setup_connect_wanted = false;

    keyboard_hide();
    assist_app_setup_store();
    assist_config_save_dirty();
    assist_ws_disconnect();
}

static void assist_app_setup_store( void ) {
    assist_config_t *assist_config = assist_get_config();
    const char *host = lv_textarea_get_text( assist_host_textfield );
    uint16_t port = atoi( lv_textarea_get_text( assist_port_textfield ) );

    if( !port )
        port = ASSIST_PORT_DEFAULT;

    if( strcmp( assist_config->host, host ) ) {
        snprintf( assist_config->host, sizeof( assist_config->host ), "%s", host );
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

static void assist_app_setup_pair_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       keyboard_hide();
                                        mainbar_slide_to_tilenumber( assist_app_get_pair_tile_num(), LV_ANIM_ON );
                                        break;
    }
}

static void assist_app_setup_pipeline_event_cb( lv_obj_t * obj, lv_event_t event ) {
    assist_config_t *assist_config = assist_get_config();

    switch( event ) {
        case( LV_EVENT_VALUE_CHANGED ):     {
                                                uint16_t index = lv_dropdown_get_selected( obj );
                                                const char *id = index ? assist_ws_get_pipeline_id( index - 1 ) : "";

                                                if( strcmp( assist_config->pipeline, id ) ) {
                                                    snprintf( assist_config->pipeline, sizeof( assist_config->pipeline ), "%s", id );
                                                    assist_config_set_dirty();
                                                }
                                                break;
                                            }
    }
}

static void assist_app_setup_exit_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       keyboard_hide();
                                        mainbar_jump_back();
                                        break;
    }
}
