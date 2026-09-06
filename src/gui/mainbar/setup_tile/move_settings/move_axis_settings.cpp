/****************************************************************************
 *   September 07 12:00:00 2026
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
#include "move_axis_settings.h"

#if defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 ) || defined( LILYGO_WATCH_2021 )

#include "gui/mainbar/mainbar.h"
#include "gui/mainbar/setup_tile/setup_tile.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

#include "hardware/motion.h"

#define AXIS_TESTER_RANGE       512                         /** @brief accel value that moves the dot to the edge */
#define AXIS_TESTER_DOT_SIZE    12                          /** @brief dot size in pixel */
#define AXIS_TESTER_MIN_SIZE    40                          /** @brief smallest useful tester size in pixel */

lv_obj_t *move_axis_settings_tile = NULL;
lv_obj_t *move_axis_tester = NULL;
lv_obj_t *move_axis_dot = NULL;
lv_obj_t *move_axis_label = NULL;
lv_obj_t *move_axis_swap_onoff = NULL;
lv_obj_t *move_axis_invert_x_onoff = NULL;
lv_obj_t *move_axis_invert_y_onoff = NULL;
lv_task_t *move_axis_task = NULL;
uint32_t move_axis_tile_num;
bool move_axis_active = false;

static void exit_move_axis_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static void move_axis_swap_event_handler( lv_obj_t * obj, lv_event_t event );
static void move_axis_invert_x_event_handler( lv_obj_t * obj, lv_event_t event );
static void move_axis_invert_y_event_handler( lv_obj_t * obj, lv_event_t event );
static void move_axis_settings_activate_cb( void );
static void move_axis_settings_hibernate_cb( void );
static void move_axis_settings_task( lv_task_t * task );

void move_axis_settings_tile_setup( void ) {
    move_axis_tile_num = mainbar_add_setup_tile( 1, 1, "move axis settings" );
    move_axis_settings_tile = mainbar_get_tile_obj( move_axis_tile_num );

    lv_obj_add_style( move_axis_settings_tile, LV_OBJ_PART_MAIN, ws_get_setup_tile_style() );

    lv_obj_t *header = wf_add_settings_header( move_axis_settings_tile, "axis setup", exit_move_axis_setup_event_cb );
    lv_obj_align( header, move_axis_settings_tile, LV_ALIGN_IN_TOP_LEFT, 10, STATUSBAR_HEIGHT + 10 );

    /*
     * the switches are placed bottom up, the tester takes whatever is left
     */
    lv_obj_t *invert_y_cont = wf_add_labeled_switch( move_axis_settings_tile, "invert y", &move_axis_invert_y_onoff, bma_get_axis_config( BMA_AXIS_INVERT_Y ), move_axis_invert_y_event_handler, ws_get_setup_tile_style() );
    lv_obj_align( invert_y_cont, move_axis_settings_tile, LV_ALIGN_IN_BOTTOM_MID, 0, -4 );

    lv_obj_t *invert_x_cont = wf_add_labeled_switch( move_axis_settings_tile, "invert x", &move_axis_invert_x_onoff, bma_get_axis_config( BMA_AXIS_INVERT_X ), move_axis_invert_x_event_handler, ws_get_setup_tile_style() );
    lv_obj_align( invert_x_cont, invert_y_cont, LV_ALIGN_OUT_TOP_MID, 0, -4 );

    lv_obj_t *swap_cont = wf_add_labeled_switch( move_axis_settings_tile, "swap x/y", &move_axis_swap_onoff, bma_get_axis_config( BMA_AXIS_SWAP_XY ), move_axis_swap_event_handler, ws_get_setup_tile_style() );
    lv_obj_align( swap_cont, invert_x_cont, LV_ALIGN_OUT_TOP_MID, 0, -4 );

    lv_coord_t top = lv_obj_get_y( header ) + lv_obj_get_height( header ) + 4;
    lv_coord_t size = lv_obj_get_y( swap_cont ) - 4 - top;
    if ( size < AXIS_TESTER_MIN_SIZE )
        size = AXIS_TESTER_MIN_SIZE;

    move_axis_tester = lv_obj_create( move_axis_settings_tile, NULL );
    lv_obj_reset_style_list( move_axis_tester, LV_OBJ_PART_MAIN );
    lv_obj_set_size( move_axis_tester, size, size );
    lv_obj_set_click( move_axis_tester, false );
    lv_obj_set_style_local_bg_color( move_axis_tester, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK );
    lv_obj_set_style_local_bg_opa( move_axis_tester, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER );
    lv_obj_set_style_local_border_color( move_axis_tester, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GRAY );
    lv_obj_set_style_local_border_width( move_axis_tester, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 1 );
    lv_obj_set_style_local_radius( move_axis_tester, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0 );
    lv_obj_align( move_axis_tester, move_axis_settings_tile, LV_ALIGN_IN_TOP_MID, 0, top );

    move_axis_label = lv_label_create( move_axis_tester, NULL );
    lv_label_set_text( move_axis_label, "x: 0 y: 0" );
    lv_obj_align( move_axis_label, move_axis_tester, LV_ALIGN_IN_TOP_MID, 0, 2 );

    move_axis_dot = lv_obj_create( move_axis_tester, NULL );
    lv_obj_reset_style_list( move_axis_dot, LV_OBJ_PART_MAIN );
    lv_obj_set_size( move_axis_dot, AXIS_TESTER_DOT_SIZE, AXIS_TESTER_DOT_SIZE );
    lv_obj_set_click( move_axis_dot, false );
    lv_obj_set_style_local_bg_color( move_axis_dot, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_YELLOW );
    lv_obj_set_style_local_bg_opa( move_axis_dot, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER );
    lv_obj_set_style_local_radius( move_axis_dot, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE );
    lv_obj_align( move_axis_dot, move_axis_tester, LV_ALIGN_CENTER, 0, 0 );

    mainbar_add_tile_activate_cb( move_axis_tile_num, move_axis_settings_activate_cb );
    mainbar_add_tile_hibernate_cb( move_axis_tile_num, move_axis_settings_hibernate_cb );

    move_axis_task = lv_task_create( move_axis_settings_task, 100, LV_TASK_PRIO_LOW, NULL );
}

uint32_t move_axis_settings_get_tile_num( void ) {
    return( move_axis_tile_num );
}

static void exit_move_axis_setup_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_jump_back();
                                        break;
    }
}

static void move_axis_swap_event_handler( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_VALUE_CHANGED ): bma_set_axis_config( BMA_AXIS_SWAP_XY, lv_switch_get_state( obj ) );
    }
}

static void move_axis_invert_x_event_handler( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_VALUE_CHANGED ): bma_set_axis_config( BMA_AXIS_INVERT_X, lv_switch_get_state( obj ) );
    }
}

static void move_axis_invert_y_event_handler( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_VALUE_CHANGED ): bma_set_axis_config( BMA_AXIS_INVERT_Y, lv_switch_get_state( obj ) );
    }
}

static void move_axis_settings_activate_cb( void ) {
    move_axis_active = true;
}

static void move_axis_settings_hibernate_cb( void ) {
    move_axis_active = false;
}

static void move_axis_settings_task( lv_task_t * task ) {
    if ( !move_axis_active )
        return;

    int16_t acc_x = 0;
    int16_t acc_y = 0;
    if ( !bma_get_accel_rotated( acc_x, acc_y ) )
        return;

    lv_coord_t range = ( lv_obj_get_width( move_axis_tester ) - AXIS_TESTER_DOT_SIZE ) / 2;
    int32_t x = ( (int32_t)acc_x * range ) / AXIS_TESTER_RANGE;
    int32_t y = ( (int32_t)acc_y * range ) / AXIS_TESTER_RANGE;
    if ( x > range ) x = range; else if ( x < -range ) x = -range;
    if ( y > range ) y = range; else if ( y < -range ) y = -range;

    lv_obj_align( move_axis_dot, move_axis_tester, LV_ALIGN_CENTER, x, y );

    char axis_label[32] = "";
    snprintf( axis_label, sizeof( axis_label ), "x: %d y: %d", acc_x, acc_y );
    lv_label_set_text( move_axis_label, axis_label );
    lv_obj_align( move_axis_label, move_axis_tester, LV_ALIGN_IN_TOP_MID, 0, 2 );
}

#endif // LILYGO_WATCH_2020_V1 || LILYGO_WATCH_2020_V2 || LILYGO_WATCH_2020_V3 || LILYGO_WATCH_2021
