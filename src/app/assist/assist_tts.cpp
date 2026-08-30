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

#include <string.h>

#include "assist_tts.h"
#include "assist_config.h"

#include "hardware/powermgm.h"
#include "hardware/sound.h"
#include "utils/uri_load/uri_load.h"

static volatile assist_tts_state_t assist_tts_state = ASSIST_TTS_OFF;
static volatile bool assist_tts_abort = false;
static TaskHandle_t assist_tts_task = NULL;
static SemaphoreHandle_t assist_tts_mutex = NULL;                   /** @brief guards the handover of the buffer between the load task and the gui */
static uri_load_dsc_t *assist_tts_dsc = NULL;
static char assist_tts_url[ ASSIST_TTS_URL_LEN ] = "";

static bool assist_tts_powermgm_event_cb( EventBits_t event, void *arg );
static void assist_tts_load_task( void *arg );
static void assist_tts_release( void );

void assist_tts_setup( void ) {
    assist_tts_mutex = xSemaphoreCreateMutex();

    powermgm_register_cb_with_prio( POWERMGM_STANDBY, assist_tts_powermgm_event_cb, "assist tts", CALL_CB_FIRST );
}

bool assist_tts_fetch( const char *url ) {
    assist_config_t *assist_config = assist_get_config();

    if( assist_tts_task || !url || !url[ 0 ] )
        return( false );

    if( !strncmp( url, "http", 4 ) )
        snprintf( assist_tts_url, sizeof( assist_tts_url ), "%s", url );
    else
        snprintf( assist_tts_url, sizeof( assist_tts_url ), "http://%s:%d%s", assist_config->host, assist_config->port, url );

    assist_tts_release();
    assist_tts_abort = false;
    assist_tts_state = ASSIST_TTS_LOADING;

    if( xTaskCreatePinnedToCore( assist_tts_load_task, "assist tts", ASSIST_TTS_TASK_STACK, NULL, 1, &assist_tts_task, 0 ) != pdPASS ) {
        log_e("assist: tts task failed");
        assist_tts_task = NULL;
        assist_tts_state = ASSIST_TTS_ERROR;
        return( false );
    }

    return( true );
}

void assist_tts_update( void ) {
    switch( assist_tts_state ) {
        case ASSIST_TTS_READY:
            if( assist_tts_dsc && assist_tts_dsc->data ) {
                sound_play_ram_audio( assist_tts_dsc->data, assist_tts_dsc->size, SOUND_TYPE_FOREGROUND );
            }

            if( sound_ram_audio_is_running() ) {
                assist_tts_state = ASSIST_TTS_SPEAKING;
            }
            else {
                assist_tts_release();
                assist_tts_state = ASSIST_TTS_OFF;
            }
            break;

        case ASSIST_TTS_SPEAKING:
            if( !sound_ram_audio_is_running() ) {
                assist_tts_release();
                assist_tts_state = ASSIST_TTS_OFF;
            }
            break;

        default:
            break;
    }
}

void assist_tts_stop( void ) {
    assist_tts_abort = true;

    sound_stop_ram_audio();
    assist_tts_release();

    if( assist_tts_state != ASSIST_TTS_LOADING )
        assist_tts_state = ASSIST_TTS_OFF;
}

assist_tts_state_t assist_tts_get_state( void ) {
    return( assist_tts_state );
}

/**
 * @brief free the buffer, the mutex keeps it apart from the handover in the load task
 */
static void assist_tts_release( void ) {
    if( assist_tts_mutex )
        xSemaphoreTake( assist_tts_mutex, portMAX_DELAY );

    if( assist_tts_dsc ) {
        uri_load_free_all( assist_tts_dsc );
        assist_tts_dsc = NULL;
    }

    if( assist_tts_mutex )
        xSemaphoreGive( assist_tts_mutex );
}

static void assist_tts_load_task( void *arg ) {
    uri_load_dsc_t *dsc = uri_load_to_ram( assist_tts_url );

    if( !dsc || !dsc->data || !dsc->size ) {
        if( dsc )
            uri_load_free_all( dsc );

        log_e("assist: tts download failed, %s", assist_tts_url );
        assist_tts_state = assist_tts_abort ? ASSIST_TTS_OFF : ASSIST_TTS_ERROR;
    }
    else if( dsc->size > ASSIST_TTS_MAX_BYTES ) {
        log_e("assist: tts answer too big, %d bytes", dsc->size );
        uri_load_free_all( dsc );
        assist_tts_state = ASSIST_TTS_ERROR;
    }
    else {
        if( assist_tts_mutex )
            xSemaphoreTake( assist_tts_mutex, portMAX_DELAY );

        if( assist_tts_abort ) {
            uri_load_free_all( dsc );
            assist_tts_state = ASSIST_TTS_OFF;
        }
        else {
            assist_tts_dsc = dsc;
            assist_tts_state = ASSIST_TTS_READY;
        }

        if( assist_tts_mutex )
            xSemaphoreGive( assist_tts_mutex );
    }

    if( assist_tts_abort && assist_tts_state == ASSIST_TTS_ERROR )
        assist_tts_state = ASSIST_TTS_OFF;

    assist_tts_task = NULL;
    vTaskDelete( NULL );
}

static bool assist_tts_powermgm_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case POWERMGM_STANDBY:  assist_tts_stop();
                                break;
    }

    return( true );
}
