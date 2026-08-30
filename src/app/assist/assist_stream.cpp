/****************************************************************************
 *   Aug 30 20:00:00 2026
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

#include <math.h>
#include <string.h>

#include "assist_stream.h"
#include "assist_config.h"
#include "assist_ws.h"

#include "hardware/micctl.h"
#include "hardware/powermgm.h"
#include "utils/alloc.h"

static volatile assist_stream_state_t assist_stream_state = ASSIST_STREAM_IDLE;
static volatile bool assist_stream_stop_request = false;
static volatile bool assist_stream_abort_request = false;
static volatile bool assist_stream_reader_done = false;
static volatile bool assist_stream_vad_end = false;                 /** @brief home assistant ended the stream, no closing frame wanted */
static volatile float assist_stream_level_db = ASSIST_STREAM_DB_FLOOR;
static volatile uint32_t assist_stream_samples = 0;
static volatile uint32_t assist_stream_overrun = 0;

static uint8_t *assist_stream_ring = NULL;
static volatile uint32_t assist_stream_ring_head = 0;               /** @brief written by the reader only */
static volatile uint32_t assist_stream_ring_tail = 0;               /** @brief written by the sender only */

static TaskHandle_t assist_stream_reader_task = NULL;
static TaskHandle_t assist_stream_sender_task = NULL;

static char assist_stream_pipeline[ ASSIST_PIPELINE_LEN ] = "";
static float assist_stream_gain = 1.0f;
static bool assist_stream_boost = false;

static const float assist_stream_gain_table[ ASSIST_GAIN_COUNT ] = { 1.0f, 8.0f, 32.0f, 64.0f, 128.0f };

static bool assist_stream_powermgm_event_cb( EventBits_t event, void *arg );
static void assist_stream_reader( void *arg );
static void assist_stream_sender( void *arg );
static void assist_stream_finalize( void );

void assist_stream_setup( void ) {
    powermgm_register_cb_with_prio( POWERMGM_STANDBY, assist_stream_powermgm_event_cb, "assist stream", CALL_CB_FIRST );
}

static float assist_stream_get_gain( uint8_t index ) {
    if( index >= ASSIST_GAIN_COUNT )
        index = ASSIST_GAIN_DEFAULT;

    return( assist_stream_gain_table[ index ] );
}

static uint32_t assist_stream_ring_used( void ) {
    uint32_t head = assist_stream_ring_head;
    uint32_t tail = assist_stream_ring_tail;

    return( head >= tail ? head - tail : ASSIST_STREAM_RING_SIZE - tail + head );
}

static uint32_t assist_stream_ring_free( void ) {
    return( ASSIST_STREAM_RING_SIZE - 1 - assist_stream_ring_used() );
}

static void assist_stream_ring_put( const uint8_t *data, uint32_t len ) {
    uint32_t head = assist_stream_ring_head;
    uint32_t first = ASSIST_STREAM_RING_SIZE - head;

    if( first > len )
        first = len;

    memcpy( assist_stream_ring + head, data, first );
    if( len > first )
        memcpy( assist_stream_ring, data + first, len - first );

    head += len;
    if( head >= ASSIST_STREAM_RING_SIZE )
        head -= ASSIST_STREAM_RING_SIZE;

    assist_stream_ring_head = head;
}

bool assist_stream_start( const char *pipeline, uint8_t gain_index ) {
    if( assist_stream_reader_task || assist_stream_sender_task )
        return( false );

    assist_stream_ring = ( uint8_t * )MALLOC( ASSIST_STREAM_RING_SIZE );
    if( !assist_stream_ring ) {
        log_e("assist: ring alloc failed");
        assist_stream_state = ASSIST_STREAM_ERROR;
        return( false );
    }

    snprintf( assist_stream_pipeline, sizeof( assist_stream_pipeline ), "%s", pipeline ? pipeline : "" );
    assist_stream_gain = assist_stream_get_gain( gain_index );
    assist_stream_ring_head = 0;
    assist_stream_ring_tail = 0;
    assist_stream_samples = 0;
    assist_stream_overrun = 0;
    assist_stream_stop_request = false;
    assist_stream_abort_request = false;
    assist_stream_reader_done = false;
    assist_stream_vad_end = false;
    assist_stream_level_db = ASSIST_STREAM_DB_FLOOR;

    powermgm_cpu_boost_take();
    assist_stream_boost = true;

    if( !micctl_start() ) {
        log_e("assist: mic start failed");
        powermgm_cpu_boost_give();
        assist_stream_boost = false;
        free( assist_stream_ring );
        assist_stream_ring = NULL;
        assist_stream_state = ASSIST_STREAM_ERROR;
        return( false );
    }

    assist_stream_state = ASSIST_STREAM_RECORDING;

    if( xTaskCreatePinnedToCore( assist_stream_sender, "assist sender", 4096, NULL, 3, &assist_stream_sender_task, 0 ) != pdPASS ) {
        log_e("assist: sender task failed");
        assist_stream_sender_task = NULL;
        assist_stream_abort_request = true;
        assist_stream_finalize();
        assist_stream_state = ASSIST_STREAM_ERROR;
        return( false );
    }

    if( xTaskCreatePinnedToCore( assist_stream_reader, "assist reader", 3072, NULL, 5, &assist_stream_reader_task, 0 ) != pdPASS ) {
        log_e("assist: reader task failed");
        assist_stream_reader_task = NULL;
        assist_stream_abort_request = true;
        assist_stream_reader_done = true;
        return( false );
    }

    return( true );
}

void assist_stream_stop( void ) {
    if( assist_stream_state != ASSIST_STREAM_RECORDING )
        return;

    assist_stream_stop_request = true;
}

void assist_stream_abort( void ) {
    if( assist_stream_reader_task || assist_stream_sender_task )
        assist_stream_abort_request = true;
    else
        assist_ws_run_reset();
}

/**
 * @brief lookahead peak limiter, holds the output at ASSIST_LIM_CEIL
 *
 * @param   lim     limiter state
 * @param   in      gained sample, full scale is 32768
 *
 * @return  the sample that entered the delay line ASSIST_LIM_LOOKAHEAD calls ago
 */
static int32_t assist_stream_limit( assist_limiter_t *lim, float in ) {
    float mag = fabsf( in );
    float target;
    int32_t value;

    if( mag > lim->env ) {
        lim->env = mag;
        lim->hold = ASSIST_LIM_LOOKAHEAD;
    }
    else if( lim->hold )
        lim->hold--;
    else
        lim->env *= ASSIST_LIM_RELEASE;

    target = lim->env > ASSIST_LIM_CEIL ? ASSIST_LIM_CEIL / lim->env : 1.0f;
    lim->gain += ( target - lim->gain ) *
                 ( target < lim->gain ? ASSIST_LIM_ATTACK : ASSIST_LIM_RECOVER );

    value = ( int32_t )( lim->delay[ lim->pos ] * lim->gain );
    lim->delay[ lim->pos ] = in;
    lim->pos = lim->pos + 1 >= ASSIST_LIM_LOOKAHEAD ? 0 : lim->pos + 1;

    if( value > 32767 )
        value = 32767;
    else if( value < -32768 )
        value = -32768;

    return( value );
}

/**
 * @brief drain the dma into the ring until the take ends
 *
 * it starts with the finger tap, the ~200 ms home assistant needs to set the run up
 * are captured instead of lost
 */
static void assist_stream_reader( void *arg ) {
    int16_t *block = ( int16_t * )MALLOC( ASSIST_STREAM_BLOCK_SAMPLES * sizeof( int16_t ) );
    int16_t *out = ( int16_t * )MALLOC( ASSIST_STREAM_BLOCK_SAMPLES * sizeof( int16_t ) );
    assist_limiter_t *lim = ( assist_limiter_t * )CALLOC( 1, sizeof( assist_limiter_t ) );

    uint32_t limit = ASSIST_STREAM_MAX_SECONDS * MICCTL_DEFAULT_SAMPLE_RATE - ASSIST_LIM_LOOKAHEAD;
    float hp_z1 = 0.0f;
    float hp_z2 = 0.0f;
    bool ready = block && out && lim;
    bool run_seen = false;                                          /** @brief the run command is sent by the sender, until then the state is the one of the last run */

    if( !ready ) {
        log_e("assist: reader block alloc failed");
        assist_stream_abort_request = true;
    }
    else {
        lim->gain = 1.0f;
    }

    while( ready && !assist_stream_stop_request && !assist_stream_abort_request && assist_stream_samples < limit ) {
        assist_ws_run_t run = assist_ws_get_run();
        size_t samples;
        uint64_t energy = 0;

        if( run == ASSIST_RUN_STARTING || run == ASSIST_RUN_LISTENING )
            run_seen = true;
        else if( run_seen ) {
            assist_stream_vad_end = ( run == ASSIST_RUN_THINKING );
            break;
        }

        samples = micctl_read( block, ASSIST_STREAM_BLOCK_SAMPLES, 100 );
        if( !samples )
            continue;

        if( assist_stream_samples + samples > limit )
            samples = limit - assist_stream_samples;

        for( size_t i = 0 ; i < samples ; i++ ) {
            float x = ( float )block[ i ];
            float y = ASSIST_HP_B0 * x + hp_z1;

            hp_z1 = ASSIST_HP_B1 * x - ASSIST_HP_A1 * y + hp_z2;
            hp_z2 = ASSIST_HP_B2 * x - ASSIST_HP_A2 * y;
            energy += ( uint64_t )( y * y );

            out[ i ] = ( int16_t )assist_stream_limit( lim, y * assist_stream_gain );
        }

        float rms = sqrtf( ( float )energy / ( float )samples );
        assist_stream_level_db = rms < 1.0f ? ASSIST_STREAM_DB_FLOOR : 20.0f * log10f( rms / 32768.0f );

        uint32_t len = samples * sizeof( int16_t );
        if( assist_stream_ring_free() >= len )
            assist_stream_ring_put( ( uint8_t * )out, len );
        else
            assist_stream_overrun++;

        assist_stream_samples += samples;
    }

    assist_stream_state = ASSIST_STREAM_SENDING;

    if( ready && !assist_stream_abort_request ) {
        uint32_t len = ASSIST_LIM_LOOKAHEAD;

        for( size_t i = 0 ; i < len ; i++ )
            out[ i ] = ( int16_t )assist_stream_limit( lim, 0.0f );

        len *= sizeof( int16_t );
        if( assist_stream_ring_free() >= len )
            assist_stream_ring_put( ( uint8_t * )out, len );

        assist_stream_samples += ASSIST_LIM_LOOKAHEAD;
    }

    if( lim )
        free( lim );
    if( block )
        free( block );
    if( out )
        free( out );

    assist_stream_reader_done = true;
    assist_stream_reader_task = NULL;
    vTaskDelete( NULL );
}

/**
 * @brief move one contiguous piece of the ring onto the wire
 *
 * @return  false if nothing was sent
 */
static bool assist_stream_send_chunk( void ) {
    uint32_t tail = assist_stream_ring_tail;
    uint32_t chunk = assist_stream_ring_used();

    if( chunk > ASSIST_WS_AUDIO_FRAME )
        chunk = ASSIST_WS_AUDIO_FRAME;
    if( chunk > ASSIST_STREAM_RING_SIZE - tail )
        chunk = ASSIST_STREAM_RING_SIZE - tail;

    if( !chunk )
        return( false );

    if( !assist_ws_send_audio( assist_stream_ring + tail, chunk ) ) {
        log_e("assist: sending audio failed");
        assist_stream_abort_request = true;
        return( false );
    }

    tail += chunk;
    if( tail >= ASSIST_STREAM_RING_SIZE )
        tail = 0;

    assist_stream_ring_tail = tail;

    return( true );
}

static void assist_stream_sender( void *arg ) {
    if( !assist_ws_run( assist_stream_pipeline ) ) {
        log_e("assist: run command failed");
        assist_stream_abort_request = true;
    }

    while( !assist_stream_abort_request ) {
        assist_ws_run_t run;

        vTaskDelay( ASSIST_STREAM_SENDER_TICK / portTICK_PERIOD_MS );

        run = assist_ws_get_run();
        if( run == ASSIST_RUN_FAILED || run == ASSIST_RUN_OFF )
            break;

        while( run == ASSIST_RUN_LISTENING && assist_stream_ring_used() ) {
            if( !assist_stream_send_chunk() )
                break;
        }

        if( assist_stream_reader_done && ( !assist_stream_ring_used() || run != ASSIST_RUN_LISTENING ) )
            break;
    }

    if( !assist_stream_abort_request && !assist_stream_vad_end )
        assist_ws_send_audio_end();

    assist_stream_finalize();
    assist_stream_sender_task = NULL;
    vTaskDelete( NULL );
}

static void assist_stream_finalize( void ) {
    assist_stream_state = ASSIST_STREAM_SENDING;

    micctl_stop();

    if( assist_stream_ring ) {
        free( assist_stream_ring );
        assist_stream_ring = NULL;
    }

    if( assist_stream_boost ) {
        powermgm_cpu_boost_give();
        assist_stream_boost = false;
    }

    if( assist_stream_abort_request )
        assist_ws_run_reset();

    if( assist_stream_overrun )
        log_w("assist: overrun, %d blocks dropped", assist_stream_overrun );

    log_i("assist: %d samples captured", assist_stream_samples );

    assist_stream_state = ASSIST_STREAM_IDLE;
}

/**
 * @brief wait until both tasks are gone
 */
static void assist_stream_join( uint32_t timeout ) {
    uint32_t start = millis();

    while( ( assist_stream_reader_task || assist_stream_sender_task ) && millis() - start < timeout )
        vTaskDelay( 10 / portTICK_PERIOD_MS );

    if( assist_stream_reader_task || assist_stream_sender_task )
        log_e("assist: tasks did not finish within %d ms", timeout );
}

static bool assist_stream_powermgm_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case POWERMGM_STANDBY:      if( assist_stream_reader_task || assist_stream_sender_task ) {
                                        assist_stream_abort_request = true;
                                        assist_stream_join( ASSIST_STREAM_JOIN_TIMEOUT );
                                    }
                                    break;
    }
    return( true );
}

assist_stream_state_t assist_stream_get_state( void ) {
    return( assist_stream_state );
}

float assist_stream_get_level_db( void ) {
    return( assist_stream_level_db );
}

uint32_t assist_stream_get_seconds( void ) {
    return( assist_stream_samples / MICCTL_DEFAULT_SAMPLE_RATE );
}
