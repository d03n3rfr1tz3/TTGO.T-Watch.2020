/****************************************************************************
 *   Aug 30 12:00:00 2026
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
#include "assist_pair.h"
#include "assist_qr.h"

#include "gui/mainbar/mainbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#else
    #include <Arduino.h>
#endif

static lv_obj_t *assist_app_pair_tile = NULL;
static lv_obj_t *assist_app_pair_cont = NULL;
static lv_obj_t *assist_app_pair_qr = NULL;
static lv_obj_t *assist_app_pair_label = NULL;
static lv_task_t *assist_app_pair_task = NULL;
static bool assist_app_pair_visible = false;
static bool assist_app_pair_started = false;
static bool assist_app_pair_slid = false;

static void assist_app_pair_hibernate_cb( void );
static void assist_app_pair_lv_task( lv_task_t * task );
static bool assist_app_pair_is_visible( void );

void assist_app_pair_setup( uint32_t tile_num ) {
    mainbar_add_tile_hibernate_cb( tile_num, assist_app_pair_hibernate_cb );

    assist_app_pair_tile = mainbar_get_tile_obj( tile_num );

    assist_app_pair_cont = lv_obj_create( assist_app_pair_tile, NULL );
    lv_obj_set_size( assist_app_pair_cont, lv_disp_get_hor_res( NULL ), lv_disp_get_ver_res( NULL ) );
    lv_obj_align( assist_app_pair_cont, assist_app_pair_tile, LV_ALIGN_IN_TOP_LEFT, 0, 0 );
    lv_obj_set_style_local_bg_color( assist_app_pair_cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE );
    lv_obj_set_style_local_bg_opa( assist_app_pair_cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER );
    lv_obj_set_style_local_border_width( assist_app_pair_cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_radius( assist_app_pair_cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0 );

    assist_app_pair_qr = assist_qr_create( assist_app_pair_cont );
    if( assist_app_pair_qr )
        lv_obj_align( assist_app_pair_qr, assist_app_pair_cont, LV_ALIGN_IN_TOP_MID, 0, ASSIST_PAIR_QR_OFFSET );

    assist_app_pair_label = lv_label_create( assist_app_pair_cont, NULL );
    lv_label_set_text( assist_app_pair_label, "" );
    lv_obj_set_style_local_text_color( assist_app_pair_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK );
    lv_obj_align( assist_app_pair_label, assist_app_pair_cont, LV_ALIGN_IN_BOTTOM_MID, 0, -2 );

    lv_tileview_add_element( assist_app_pair_tile, assist_app_pair_cont );

    assist_app_pair_task = lv_task_create( assist_app_pair_lv_task, ASSIST_SETUP_PERIOD, LV_TASK_PRIO_OFF, NULL );
}

void assist_app_pair_enable( bool enable ) {
    if( enable ) {
        assist_qr_alloc( assist_app_pair_qr );
    }
    else {
        assist_pair_stop();
        assist_qr_free( assist_app_pair_qr );
        assist_app_pair_visible = false;
    }

    lv_task_set_prio( assist_app_pair_task, enable ? LV_TASK_PRIO_MID : LV_TASK_PRIO_OFF );
}

static void assist_app_pair_hibernate_cb( void ) {
    assist_app_setup_leave();
    assist_app_setup_enable( false );
    assist_app_pair_enable( false );
}

static bool assist_app_pair_is_visible( void ) {
    lv_area_t area;

    lv_obj_get_coords( assist_app_pair_tile, &area );

    return( abs( area.x1 ) + abs( area.y1 ) < lv_disp_get_hor_res( NULL ) / 2 );
}

static void assist_app_pair_lv_task( lv_task_t * task ) {
    bool visible = assist_app_pair_is_visible();
    assist_pair_state_t state = assist_pair_get_state();

    if( visible != assist_app_pair_visible ) {
        assist_app_pair_visible = visible;
        assist_app_pair_started = false;
        assist_app_pair_slid = false;

        if( visible ) {
            assist_app_setup_leave();
            assist_qr_clear( assist_app_pair_qr );
        }
        else {
            assist_pair_stop();
        }
    }

    if( visible && !assist_app_pair_started ) {
        assist_app_pair_started = assist_pair_start();

        if( assist_app_pair_started )
            assist_qr_update( assist_app_pair_qr, assist_pair_get_url() );

        state = assist_pair_get_state();
    }

    lv_label_set_text( assist_app_pair_label, assist_pair_get_message() );
    lv_obj_align( assist_app_pair_label, assist_app_pair_cont, LV_ALIGN_IN_BOTTOM_MID, 0, -2 );

    if( !visible )
        return;

    if( state == ASSIST_PAIR_WAIT || state == ASSIST_PAIR_EXCHANGE || state == ASSIST_PAIR_TOKEN )
        lv_disp_trig_activity( NULL );

    if( state == ASSIST_PAIR_DONE && !assist_app_pair_slid ) {
        assist_app_pair_slid = true;
        mainbar_slide_to_tilenumber( assist_app_get_setup_tile_num(), LV_ANIM_ON );
    }
}
