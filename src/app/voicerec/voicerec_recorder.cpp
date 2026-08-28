/****************************************************************************
 *   Aug 25 20:00:00 2026
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
#include <time.h>
#include <string.h>
#include <FS.h>
#include <SPIFFS.h>

#include "voicerec_recorder.h"

#include "hardware/micctl.h"
#include "hardware/powermgm.h"
#include "utils/alloc.h"

static volatile voicerec_state_t voicerec_state = VOICEREC_IDLE;
static volatile bool voicerec_stop_request = false;
static volatile bool voicerec_reader_done = false;
static volatile bool voicerec_disk_full = false;
static volatile float voicerec_level_db = VOICEREC_DB_FLOOR;
static volatile uint32_t voicerec_samples_recorded = 0;
static volatile uint32_t voicerec_bytes_written = 0;
static volatile uint32_t voicerec_overrun = 0;

static uint8_t *voicerec_ring = NULL;
static volatile uint32_t voicerec_ring_head = 0;                    /** @brief written by the reader only */
static volatile uint32_t voicerec_ring_tail = 0;                    /** @brief written by the writer only */

static TaskHandle_t voicerec_reader_task = NULL;
static TaskHandle_t voicerec_writer_task = NULL;

static fs::File voicerec_file;
static char voicerec_filename[ VOICEREC_PATH_MAX ] = "";
static bool voicerec_low_quality = false;
static float voicerec_gain = 1.0f;
static bool voicerec_boost = false;
static uint32_t voicerec_budget = 0;
static uint32_t voicerec_free_cache = 0;
static uint32_t voicerec_free_cache_time = 0;

static bool voicerec_powermgm_event_cb( EventBits_t event, void *arg );
static void voicerec_reader( void *arg );
static void voicerec_writer( void *arg );
static void voicerec_finalize( void );

void voicerec_recorder_setup( void ) {
    powermgm_register_cb_with_prio( POWERMGM_STANDBY, voicerec_powermgm_event_cb, "voicerec", CALL_CB_FIRST );
}

static const float voicerec_gain_table[ VOICEREC_GAIN_COUNT ] = { 1.0f, 8.0f, 32.0f, 64.0f, 128.0f };

float voicerec_recorder_get_gain( uint8_t index ) {
    if( index >= VOICEREC_GAIN_COUNT )
        index = VOICEREC_GAIN_DEFAULT;

    return( voicerec_gain_table[ index ] );
}

static uint32_t voicerec_bytes_per_second( bool low_quality ) {
    return( low_quality ? VOICEREC_SAMPLE_RATE : VOICEREC_SAMPLE_RATE * sizeof( int16_t ) );
}

static uint32_t voicerec_get_free_bytes( bool refresh ) {
    if( refresh || !voicerec_free_cache_time || millis() - voicerec_free_cache_time >= VOICEREC_SPACE_CACHE ) {
        size_t total = SPIFFS.totalBytes();
        size_t used = SPIFFS.usedBytes();

        voicerec_free_cache = total > used ? total - used : 0;
        voicerec_free_cache_time = millis();
    }

    return( voicerec_free_cache );
}

static uint32_t voicerec_ring_used( void ) {
    uint32_t head = voicerec_ring_head;
    uint32_t tail = voicerec_ring_tail;

    return( head >= tail ? head - tail : VOICEREC_RING_SIZE - tail + head );
}

static uint32_t voicerec_ring_free( void ) {
    return( VOICEREC_RING_SIZE - 1 - voicerec_ring_used() );
}

static void voicerec_ring_put( const uint8_t *data, uint32_t len ) {
    uint32_t head = voicerec_ring_head;
    uint32_t first = VOICEREC_RING_SIZE - head;

    if( first > len )
        first = len;

    memcpy( voicerec_ring + head, data, first );
    if( len > first )
        memcpy( voicerec_ring, data + first, len - first );

    head += len;
    if( head >= VOICEREC_RING_SIZE )
        head -= VOICEREC_RING_SIZE;

    voicerec_ring_head = head;
}

static void voicerec_put_u16( uint8_t *header, uint32_t offset, uint16_t value ) {
    header[ offset ] = ( uint8_t )value;
    header[ offset + 1 ] = ( uint8_t )( value >> 8 );
}

static void voicerec_put_u32( uint8_t *header, uint32_t offset, uint32_t value ) {
    header[ offset ] = ( uint8_t )value;
    header[ offset + 1 ] = ( uint8_t )( value >> 8 );
    header[ offset + 2 ] = ( uint8_t )( value >> 16 );
    header[ offset + 3 ] = ( uint8_t )( value >> 24 );
}

/**
 * @brief build the 84 byte header, sizes stay zero until the take is finalized
 *
 * @param   header          VOICEREC_HEADER_SIZE bytes
 * @param   icrd            creation date, "YYYY-MM-DD HH:MM:SS"
 * @param   low_quality     true for unsigned 8 bit, false for signed 16 bit
 */
static void voicerec_build_header( uint8_t *header, const char *icrd, bool low_quality ) {
    uint16_t bits = low_quality ? 8 : 16;
    uint16_t align = bits / 8;

    memset( header, 0, VOICEREC_HEADER_SIZE );

    memcpy( header + 0, "RIFF", 4 );
    memcpy( header + 8, "WAVE", 4 );
    memcpy( header + 12, "fmt ", 4 );
    voicerec_put_u32( header, 16, 16 );
    voicerec_put_u16( header, 20, 1 );                              /* pcm */
    voicerec_put_u16( header, 22, 1 );                              /* mono */
    voicerec_put_u32( header, 24, VOICEREC_SAMPLE_RATE );
    voicerec_put_u32( header, 28, VOICEREC_SAMPLE_RATE * align );
    voicerec_put_u16( header, 32, align );
    voicerec_put_u16( header, 34, bits );

    memcpy( header + 36, "LIST", 4 );
    voicerec_put_u32( header, 40, 32 );
    memcpy( header + 44, "INFO", 4 );
    memcpy( header + 48, "ICRD", 4 );
    voicerec_put_u32( header, 52, VOICEREC_ICRD_SIZE );
    strncpy( ( char * )header + VOICEREC_ICRD_OFFSET, icrd, VOICEREC_ICRD_SIZE - 1 );

    memcpy( header + 76, "data", 4 );
}

static void voicerec_patch_u32( uint32_t offset, uint32_t value ) {
    uint8_t data[ 4 ];

    voicerec_put_u32( data, 0, value );

    if( !voicerec_file.seek( offset ) || voicerec_file.write( data, sizeof( data ) ) != sizeof( data ) )
        log_e("voicerec: header patch at %d failed", offset );
}

bool voicerec_recorder_start( bool low_quality, float gain ) {
    uint32_t bps = voicerec_bytes_per_second( low_quality );
    uint32_t free_bytes = voicerec_get_free_bytes( true );
    time_t now;
    struct tm info;
    char icrd[ VOICEREC_ICRD_SIZE ];
    uint8_t header[ VOICEREC_HEADER_SIZE ];

    if( voicerec_state != VOICEREC_IDLE && voicerec_state != VOICEREC_ERROR )
        return( false );

    if( !micctl_get_available() )
        return( false );

    voicerec_budget = free_bytes > VOICEREC_RESERVE_BYTES ? free_bytes - VOICEREC_RESERVE_BYTES : 0;
    if( voicerec_budget < bps ) {
        log_w("voicerec: only %d bytes left, no room for a take", free_bytes );
        voicerec_disk_full = true;
        voicerec_state = VOICEREC_IDLE;
        return( false );
    }

    time( &now );
    localtime_r( &now, &info );

    snprintf( voicerec_filename, sizeof( voicerec_filename ), "%s/%02d%02d%02d-%02d%02d%02d.wav", VOICEREC_DIR,
              info.tm_year % 100, info.tm_mon + 1, info.tm_mday, info.tm_hour, info.tm_min, info.tm_sec );
    snprintf( icrd, sizeof( icrd ), "%04d-%02d-%02d %02d:%02d:%02d",
              info.tm_year + 1900, info.tm_mon + 1, info.tm_mday, info.tm_hour, info.tm_min, info.tm_sec );

    voicerec_file = SPIFFS.open( voicerec_filename, FILE_WRITE );
    if( !voicerec_file ) {
        log_e("voicerec: open %s failed", voicerec_filename );
        voicerec_state = VOICEREC_ERROR;
        return( false );
    }

    voicerec_build_header( header, icrd, low_quality );
    if( voicerec_file.write( header, sizeof( header ) ) != sizeof( header ) ) {
        log_e("voicerec: header write failed");
        voicerec_file.close();
        SPIFFS.remove( voicerec_filename );
        voicerec_state = VOICEREC_ERROR;
        return( false );
    }

    voicerec_ring = ( uint8_t * )MALLOC( VOICEREC_RING_SIZE );
    if( !voicerec_ring ) {
        log_e("voicerec: ring alloc failed");
        voicerec_file.close();
        SPIFFS.remove( voicerec_filename );
        voicerec_state = VOICEREC_ERROR;
        return( false );
    }

    voicerec_low_quality = low_quality;
    voicerec_gain = gain;
    voicerec_ring_head = 0;
    voicerec_ring_tail = 0;
    voicerec_bytes_written = 0;
    voicerec_samples_recorded = 0;
    voicerec_overrun = 0;
    voicerec_disk_full = false;
    voicerec_stop_request = false;
    voicerec_reader_done = false;
    voicerec_level_db = VOICEREC_DB_FLOOR;

    powermgm_cpu_boost_take();
    voicerec_boost = true;

    if( !micctl_start( VOICEREC_SAMPLE_RATE ) ) {
        log_e("voicerec: mic start failed");
        powermgm_cpu_boost_give();
        voicerec_boost = false;
        free( voicerec_ring );
        voicerec_ring = NULL;
        voicerec_file.close();
        SPIFFS.remove( voicerec_filename );
        voicerec_state = VOICEREC_ERROR;
        return( false );
    }

    voicerec_state = VOICEREC_RECORDING;

    if( xTaskCreatePinnedToCore( voicerec_writer, "voicerec writer", 4096, NULL, 2, &voicerec_writer_task, 0 ) != pdPASS ) {
        log_e("voicerec: writer task failed");
        voicerec_writer_task = NULL;
        voicerec_finalize();
        return( false );
    }

    if( xTaskCreatePinnedToCore( voicerec_reader, "voicerec reader", 3072, NULL, 5, &voicerec_reader_task, 0 ) != pdPASS ) {
        log_e("voicerec: reader task failed");
        voicerec_reader_task = NULL;
        voicerec_stop_request = true;
        voicerec_reader_done = true;
        return( false );
    }

    return( true );
}

void voicerec_recorder_stop( void ) {
    if( voicerec_state != VOICEREC_RECORDING )
        return;

    voicerec_stop_request = true;
}

/**
 * @brief lookahead peak limiter, holds the output at VOICEREC_LIM_CEIL
 *
 * @param   lim     limiter state
 * @param   in      gained sample, full scale is 32768
 *
 * @return  the sample that entered the delay line VOICEREC_LIM_LOOKAHEAD calls ago
 */
static int32_t voicerec_limit( voicerec_limiter_t *lim, float in ) {
    float mag = fabsf( in );
    float target;
    int32_t value;

    if( mag > lim->env ) {
        lim->env = mag;
        lim->hold = VOICEREC_LIM_LOOKAHEAD;
    }
    else if( lim->hold )
        lim->hold--;
    else
        lim->env *= VOICEREC_LIM_RELEASE;

    target = lim->env > VOICEREC_LIM_CEIL ? VOICEREC_LIM_CEIL / lim->env : 1.0f;
    lim->gain += ( target - lim->gain ) *
                 ( target < lim->gain ? VOICEREC_LIM_ATTACK : VOICEREC_LIM_RECOVER );

    value = ( int32_t )( lim->delay[ lim->pos ] * lim->gain );
    lim->delay[ lim->pos ] = in;
    lim->pos = lim->pos + 1 >= VOICEREC_LIM_LOOKAHEAD ? 0 : lim->pos + 1;

    if( value > 32767 )
        value = 32767;
    else if( value < -32768 )
        value = -32768;

    return( value );
}

static void voicerec_store( uint8_t *out, size_t i, int32_t value ) {
    if( voicerec_low_quality )
        out[ i ] = ( uint8_t )( ( value >> 8 ) + 128 );
    else
        ( ( int16_t * )out )[ i ] = ( int16_t )value;
}

/**
 * @brief drain the dma into the ring until stopped or the cap is reached
 */
static void voicerec_reader( void *arg ) {
    int16_t *block = ( int16_t * )MALLOC( VOICEREC_BLOCK_SAMPLES * sizeof( int16_t ) );
    uint8_t *out = ( uint8_t * )MALLOC( VOICEREC_BLOCK_SAMPLES * sizeof( int16_t ) );
    voicerec_limiter_t *lim = ( voicerec_limiter_t * )CALLOC( 1, sizeof( voicerec_limiter_t ) );

    uint32_t limit = VOICEREC_MAX_SECONDS * VOICEREC_SAMPLE_RATE - VOICEREC_LIM_LOOKAHEAD;
    float hp_z1 = 0.0f;
    float hp_z2 = 0.0f;

    if( !block || !out || !lim ) {
        log_e("voicerec: reader block alloc failed");
        voicerec_stop_request = true;
    }
    else {
        lim->gain = 1.0f;
    }

    while( block && out && lim && !voicerec_stop_request && voicerec_samples_recorded < limit ) {
        size_t samples = micctl_read( block, VOICEREC_BLOCK_SAMPLES, 100 );
        uint64_t energy = 0;
        uint32_t len = 0;

        if( !samples )
            continue;

        if( voicerec_samples_recorded + samples > limit )
            samples = limit - voicerec_samples_recorded;

        for( size_t i = 0 ; i < samples ; i++ ) {
            float x = ( float )block[ i ];
            float y = VOICEREC_HP_B0 * x + hp_z1;

            hp_z1 = VOICEREC_HP_B1 * x - VOICEREC_HP_A1 * y + hp_z2;
            hp_z2 = VOICEREC_HP_B2 * x - VOICEREC_HP_A2 * y;
            energy += ( uint64_t )( y * y );

            voicerec_store( out, i, voicerec_limit( lim, y * voicerec_gain ) );
        }

        float rms = sqrtf( ( float )energy / ( float )samples );
        voicerec_level_db = rms < 1.0f ? VOICEREC_DB_FLOOR : 20.0f * log10f( rms / 32768.0f );

        len = voicerec_low_quality ? samples : samples * sizeof( int16_t );
        if( voicerec_ring_free() >= len )
            voicerec_ring_put( out, len );
        else
            voicerec_overrun++;

        voicerec_samples_recorded += samples;
    }
    
    if( block && out && lim ) {
        uint32_t len = VOICEREC_LIM_LOOKAHEAD;

        for( size_t i = 0 ; i < VOICEREC_LIM_LOOKAHEAD ; i++ )
            voicerec_store( out, i, voicerec_limit( lim, 0.0f ) );

        if( !voicerec_low_quality )
            len *= sizeof( int16_t );
        if( voicerec_ring_free() >= len )
            voicerec_ring_put( out, len );

        voicerec_samples_recorded += VOICEREC_LIM_LOOKAHEAD;
    }

    if( lim )
        free( lim );
    if( block )
        free( block );
    if( out )
        free( out );

    voicerec_reader_done = true;
    voicerec_reader_task = NULL;
    vTaskDelete( NULL );
}

/**
 * @brief move one contiguous piece of the ring to spiffs
 *
 * @return  false if the take has to end
 */
static bool voicerec_flush_chunk( void ) {
    uint32_t tail = voicerec_ring_tail;
    uint32_t chunk = voicerec_ring_used();

    if( chunk > VOICEREC_WRITE_CHUNK )
        chunk = VOICEREC_WRITE_CHUNK;
    if( chunk > VOICEREC_RING_SIZE - tail )
        chunk = VOICEREC_RING_SIZE - tail;
    if( voicerec_bytes_written + chunk > voicerec_budget )
        chunk = voicerec_budget > voicerec_bytes_written ? voicerec_budget - voicerec_bytes_written : 0;

    if( !chunk || voicerec_file.write( voicerec_ring + tail, chunk ) != chunk ) {
        log_w("voicerec: out of space after %d bytes", voicerec_bytes_written );
        voicerec_disk_full = true;
        voicerec_stop_request = true;
        return( false );
    }

    tail += chunk;
    if( tail >= VOICEREC_RING_SIZE )
        tail = 0;

    voicerec_ring_tail = tail;
    voicerec_bytes_written += chunk;

    return( true );
}

static void voicerec_writer( void *arg ) {
    while( true ) {
        vTaskDelay( VOICEREC_WRITER_TICK / portTICK_PERIOD_MS );

        while( true ) {
            uint32_t used = voicerec_ring_used();

            if( !used )
                break;
            if( used < VOICEREC_WRITE_CHUNK && !voicerec_reader_done )
                break;
            if( !voicerec_flush_chunk() )
                break;
        }

        if( voicerec_reader_done && ( !voicerec_ring_used() || voicerec_disk_full ) )
            break;
    }

    voicerec_finalize();
    voicerec_writer_task = NULL;
    vTaskDelete( NULL );
}

static void voicerec_finalize( void ) {
    voicerec_state = VOICEREC_FINALIZING;

    if( voicerec_file ) {
        voicerec_patch_u32( VOICEREC_PATCH_RIFF, VOICEREC_HEADER_SIZE + voicerec_bytes_written - 8 );
        voicerec_patch_u32( VOICEREC_PATCH_DATA, voicerec_bytes_written );
        voicerec_file.close();
    }

    micctl_stop();

    if( voicerec_ring ) {
        free( voicerec_ring );
        voicerec_ring = NULL;
    }

    if( voicerec_boost ) {
        powermgm_cpu_boost_give();
        voicerec_boost = false;
    }

    if( voicerec_overrun )
        log_w("voicerec: %d blocks dropped", voicerec_overrun );

    if( voicerec_bytes_written ) {
        log_i("voicerec: %s, %d bytes, %d samples", voicerec_filename, voicerec_bytes_written, voicerec_samples_recorded );
    }
    else {
        SPIFFS.remove( voicerec_filename );
        voicerec_filename[ 0 ] = '\0';
    }

    voicerec_free_cache_time = 0;
    voicerec_state = VOICEREC_IDLE;
}

/**
 * @brief wait until both tasks are gone
 */
static void voicerec_recorder_join( uint32_t timeout ) {
    uint32_t start = millis();

    while( ( voicerec_reader_task || voicerec_writer_task ) && millis() - start < timeout )
        vTaskDelay( 10 / portTICK_PERIOD_MS );

    if( voicerec_reader_task || voicerec_writer_task )
        log_e("voicerec: tasks did not finish within %d ms", timeout );
}

static bool voicerec_powermgm_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case POWERMGM_STANDBY:      if( voicerec_state == VOICEREC_RECORDING ) {
                                        voicerec_recorder_stop();
                                        voicerec_recorder_join( VOICEREC_JOIN_TIMEOUT );
                                    }
                                    break;
    }
    return( true );
}

voicerec_state_t voicerec_recorder_get_state( void ) {
    return( voicerec_state );
}

uint32_t voicerec_recorder_get_seconds( void ) {
    return( voicerec_samples_recorded / VOICEREC_SAMPLE_RATE );
}

float voicerec_recorder_get_level_db( void ) {
    return( voicerec_level_db );
}

bool voicerec_recorder_get_disk_full( void ) {
    return( voicerec_disk_full );
}

uint32_t voicerec_recorder_get_remaining_seconds( bool low_quality ) {
    uint32_t bps = voicerec_bytes_per_second( low_quality );
    uint32_t free_bytes;

    if( voicerec_state == VOICEREC_RECORDING || voicerec_state == VOICEREC_FINALIZING )
        return( ( voicerec_budget > voicerec_bytes_written ? voicerec_budget - voicerec_bytes_written : 0 ) / bps );

    free_bytes = voicerec_get_free_bytes( false );

    return( free_bytes > VOICEREC_RESERVE_BYTES ? ( free_bytes - VOICEREC_RESERVE_BYTES ) / bps : 0 );
}

const char *voicerec_recorder_get_filename( void ) {
    return( voicerec_filename );
}
