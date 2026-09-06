/****************************************************************************
 *   Sep 6 12:00:00 2026
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
#include "assist_config.h"
#include "assist_stream.h"
#include "assist_tts.h"
#include "assist_widget.h"
#include "assist_ws.h"

#include "gui/mainbar/mainbar.h"
#include "gui/mainbar/main_tile/main_tile.h"
#include "gui/widget.h"
#include "hardware/powermgm.h"
#include "hardware/sound.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#else
    #include <Arduino.h>
#endif

LV_IMG_DECLARE(assist_app_64px);

static icon_t *assist_widget = NULL;
static lv_task_t *assist_widget_task = NULL;

static bool assist_widget_session = false;                      /** @brief a voice session is armed or running */
static bool assist_widget_connect_wanted = false;
static bool assist_widget_started = false;                      /** @brief the run was handed to assist_stream */
static bool assist_widget_busy_seen = false;                    /** @brief the engine reported busy at least once, the session may end on idle */
static uint32_t assist_widget_run_start = 0;

static void assist_widget_lv_task( lv_task_t * task );
static void assist_widget_event_cb( lv_obj_t * obj, lv_event_t event );
static bool assist_widget_powermgm_event_cb( EventBits_t event, void *arg );
static bool assist_widget_is_visible( void );
static void assist_widget_toggle( void );
static void assist_widget_end( bool failed );

void assist_widget_setup( void ) {
    assist_widget_task = lv_task_create( assist_widget_lv_task, ASSIST_WIDGET_PERIOD, LV_TASK_PRIO_OFF, NULL );

    powermgm_register_cb_with_prio( POWERMGM_STANDBY, assist_widget_powermgm_event_cb, "assist widget", CALL_CB_LAST );

    if( assist_get_config()->widget )
        assist_widget_enable( true );
}

bool assist_widget_enable( bool enable ) {
    if( !enable ) {
        assist_widget_end( false );
        assist_widget = widget_remove( assist_widget );
        return( true );
    }

    if( assist_widget )
        return( true );

    assist_widget = widget_register( ASSIST_WIDGET_LABEL, &assist_app_64px, assist_widget_event_cb );

    if( !assist_widget )
        return( false );

    widget_hide_indicator( assist_widget );

    return( true );
}

bool assist_widget_active( void ) {
    return( assist_widget != NULL );
}

/*
 * a short click talks, a long press opens the app, LV_EVENT_CLICKED would fire for both
 */
static void assist_widget_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_SHORT_CLICKED ):     assist_widget_toggle();
                                            break;

        case( LV_EVENT_LONG_PRESSED ):      assist_widget_end( false );
                                            mainbar_jump_to_tilenumber( assist_app_get_app_main_tile_num(), LV_ANIM_OFF, true );
                                            break;
    }
}

/*
 * without sound the answer would be neither audible nor visible, so open the app instead
 */
static void assist_widget_toggle( void ) {
    if( !sound_get_enabled_config() ) {
        assist_widget_end( false );
        mainbar_jump_to_tilenumber( assist_app_get_app_main_tile_num(), LV_ANIM_OFF, true );
        return;
    }

    if( assist_stream_get_state() == ASSIST_STREAM_RECORDING ) {
        assist_stream_stop();
        return;
    }

    if( assist_widget_session ) {
        assist_widget_end( false );
        return;
    }

    assist_widget_session = true;
    assist_widget_connect_wanted = true;
    assist_widget_started = false;
    assist_widget_busy_seen = false;
    assist_widget_run_start = millis();

    widget_set_label( assist_widget, "link" );
    widget_set_indicator( assist_widget, ICON_INDICATOR_UPDATE );
    lv_task_set_prio( assist_widget_task, LV_TASK_PRIO_MID );
}

static void assist_widget_end( bool failed ) {
    assist_widget_session = false;
    assist_widget_connect_wanted = false;
    assist_widget_started = false;

    if( assist_widget_task )
        lv_task_set_prio( assist_widget_task, LV_TASK_PRIO_OFF );

    assist_stream_abort();
    assist_tts_stop();
    assist_ws_disconnect();

    widget_set_label( assist_widget, failed ? "fail" : ASSIST_WIDGET_LABEL );

    if( failed ) // red on failure of the run
        widget_set_indicator( assist_widget, ICON_INDICATOR_FAIL );
    else
        widget_hide_indicator( assist_widget );
}

static bool assist_widget_is_visible( void ) {
    lv_area_t area;

    lv_obj_get_coords( mainbar_get_tile_obj( main_tile_get_tile_num() ), &area );

    return( abs( area.x1 ) + abs( area.y1 ) < lv_disp_get_hor_res( NULL ) / 2 );
}

static void assist_widget_lv_task( lv_task_t * task ) {
    assist_config_t *assist_config = assist_get_config();
    assist_ws_run_t run = assist_ws_get_run();
    assist_stream_state_t stream = assist_stream_get_state();
    assist_tts_state_t tts = assist_tts_get_state();
    bool recording = ( stream == ASSIST_STREAM_RECORDING );
    bool waiting = ( run == ASSIST_RUN_STARTING || run == ASSIST_RUN_LISTENING || run == ASSIST_RUN_THINKING );
    bool speaking = ( tts == ASSIST_TTS_LOADING || tts == ASSIST_TTS_READY || tts == ASSIST_TTS_SPEAKING );
    bool busy = recording || waiting || speaking || stream == ASSIST_STREAM_SENDING;

    if( !assist_widget_is_visible() ) {
        assist_widget_end( false );
        return;
    }

    if( !assist_widget_started ) {
        if( assist_widget_connect_wanted && assist_ws_get_state() != ASSIST_WS_READY )
            assist_widget_connect_wanted = !assist_ws_connect( assist_config->token );

        if( assist_ws_get_state() == ASSIST_WS_ERROR || millis() - assist_widget_run_start >= ASSIST_WS_TIMEOUT ) {
            log_e("assist: widget session failed, %s", assist_ws_get_message() );
            assist_widget_end( true );
            return;
        }

        if( assist_ws_get_state() != ASSIST_WS_READY ) {
            lv_disp_trig_activity( NULL );
            return;
        }

        assist_widget_started = true;
        assist_widget_run_start = millis();
        assist_tts_stop();

        if( !assist_stream_start( assist_config->pipeline, assist_config->gain, true ) ) {
            assist_widget_end( true );
            return;
        }

        assist_widget_busy_seen = true;
        widget_set_label( assist_widget, "talk" );
        return;
    }

    if( waiting && millis() - assist_widget_run_start >= ASSIST_WS_RUN_TIMEOUT ) {
        log_e("assist: widget run timed out");
        assist_widget_end( true );
        return;
    }

    assist_ws_take_text();                                      /** @brief the widget has nowhere to show it, but the flag has to be consumed */

    if( assist_ws_take_tts() )
        assist_tts_fetch( assist_ws_get_tts_url() );

    assist_tts_update();
    tts = assist_tts_get_state();
    speaking = ( tts == ASSIST_TTS_LOADING || tts == ASSIST_TTS_READY || tts == ASSIST_TTS_SPEAKING );
    busy = recording || waiting || speaking || stream == ASSIST_STREAM_SENDING;

    if( busy ) {
        assist_widget_busy_seen = true;
        lv_disp_trig_activity( NULL );
    }
    else if( assist_widget_busy_seen ) {
        assist_widget_end( tts == ASSIST_TTS_ERROR || run == ASSIST_RUN_FAILED );
        return;
    }

    if( recording ) { // blue while the user talks
        widget_set_label( assist_widget, "talk" );
        widget_set_indicator( assist_widget, ICON_INDICATOR_UPDATE );
    }
    else if( speaking ) { // green once the answer talks back
        widget_set_label( assist_widget, "speak" );
        widget_set_indicator( assist_widget, ICON_INDICATOR_OK );
    }
    else { // yellow while the run works
        widget_set_label( assist_widget, "think" );
        widget_set_indicator( assist_widget, ICON_INDICATOR_WAIT );
    }
}

static bool assist_widget_powermgm_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case POWERMGM_STANDBY:  assist_widget_end( false );
                                break;
    }

    return( true );
}
