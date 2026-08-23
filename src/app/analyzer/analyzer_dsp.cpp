/****************************************************************************
 *   Aug 22 23:00:00 2026
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
#include <stdlib.h>
#include <string.h>

#include "analyzer_dsp.h"

#include "hardware/micctl.h"
#include "hardware/powermgm.h"

static int16_t *analyzer_dsp_capture = NULL;                                    /** @brief rolling window, dc free, newest sample last */
static int16_t *analyzer_dsp_block = NULL;                                      /** @brief scratch for one micctl_read */
static float *analyzer_dsp_mag = NULL;                                          /** @brief magnitudes in dBFS */

static int analyzer_dsp_refs = 0;
static bool analyzer_dsp_boost = false;
static size_t analyzer_dsp_filled = 0;
static float analyzer_dsp_peak_db = ANALYZER_DB_FLOOR;
static uint16_t analyzer_dsp_peak_bin = 0;

static void analyzer_dsp_free( void );

bool analyzer_dsp_start( void ) {
    analyzer_dsp_refs++;

    if( !analyzer_dsp_capture ) {
        analyzer_dsp_capture = ( int16_t * )calloc( ANALYZER_CAPTURE_SAMPLES, sizeof( int16_t ) );
        analyzer_dsp_block = ( int16_t * )malloc( ANALYZER_BLOCK_SAMPLES * sizeof( int16_t ) );
        analyzer_dsp_mag = ( float * )calloc( ANALYZER_FFT_BINS, sizeof( float ) );

        if( !analyzer_dsp_capture || !analyzer_dsp_block || !analyzer_dsp_mag || !analyzer_fft_setup() ) {
            log_e("analyzer dsp alloc failed");
            analyzer_dsp_free();
            return( false );
        }

        analyzer_dsp_filled = 0;
        analyzer_dsp_peak_db = ANALYZER_DB_FLOOR;
        analyzer_dsp_peak_bin = 0;
    }

    if( !micctl_start( ANALYZER_SAMPLE_RATE ) ) {
        log_e("analyzer dsp mic start failed");
        return( false );
    }

    if( !analyzer_dsp_boost ) {
        powermgm_cpu_boost_take();
        analyzer_dsp_boost = true;
    }

    return( true );
}

void analyzer_dsp_stop( void ) {
    if( analyzer_dsp_refs > 0 )
        analyzer_dsp_refs--;

    if( analyzer_dsp_refs )
        return;

    micctl_stop();

    if( analyzer_dsp_boost ) {
        powermgm_cpu_boost_give();
        analyzer_dsp_boost = false;
    }

    analyzer_dsp_free();
}

static void analyzer_dsp_free( void ) {
    if( analyzer_dsp_capture ) { free( analyzer_dsp_capture ); analyzer_dsp_capture = NULL; }
    if( analyzer_dsp_block ) { free( analyzer_dsp_block ); analyzer_dsp_block = NULL; }
    if( analyzer_dsp_mag ) { free( analyzer_dsp_mag ); analyzer_dsp_mag = NULL; }
    analyzer_fft_free();
    analyzer_dsp_filled = 0;
}

bool analyzer_dsp_is_running( void ) {
    return( analyzer_dsp_capture && micctl_is_running() );
}

bool analyzer_dsp_update( bool with_fft ) {
    size_t total = 0;

    if( !analyzer_dsp_is_running() )
        return( false );

    for( int block = 0 ; block < ANALYZER_MAX_BLOCKS ; block++ ) {
        size_t samples = micctl_read( analyzer_dsp_block, ANALYZER_BLOCK_SAMPLES, 0 );
        if( !samples )
            break;

        int32_t sum = 0;
        for( size_t i = 0 ; i < samples ; i++ )
            sum += analyzer_dsp_block[ i ];
        int32_t offset = sum / ( int32_t )samples;

        memmove( analyzer_dsp_capture, analyzer_dsp_capture + samples, ( ANALYZER_CAPTURE_SAMPLES - samples ) * sizeof( int16_t ) );
        for( size_t i = 0 ; i < samples ; i++ )
            analyzer_dsp_capture[ ANALYZER_CAPTURE_SAMPLES - samples + i ] = ( int16_t )( analyzer_dsp_block[ i ] - offset );

        total += samples;
    }

    if( !total )
        return( false );

    analyzer_dsp_filled += total;
    if( analyzer_dsp_filled > ANALYZER_CAPTURE_SAMPLES )
        analyzer_dsp_filled = ANALYZER_CAPTURE_SAMPLES;

    const int16_t *window = analyzer_dsp_capture + ANALYZER_CAPTURE_SAMPLES - ANALYZER_FFT_SIZE;

    int32_t peak = 0;
    for( uint16_t i = 0 ; i < ANALYZER_FFT_SIZE ; i++ ) {
        int32_t value = abs( window[ i ] );
        if( value > peak )
            peak = value;
    }
    analyzer_dsp_peak_db = peak < 1 ? ANALYZER_DB_FLOOR : 20.0f * log10f( ( float )peak / 32768.0f );

    if( with_fft ) {
        analyzer_fft_magnitudes( window, analyzer_dsp_mag );

        uint16_t peak_bin = ANALYZER_PEAK_FIRST_BIN;
        for( uint16_t i = ANALYZER_PEAK_FIRST_BIN + 1 ; i < ANALYZER_FFT_BINS ; i++ )
            if( analyzer_dsp_mag[ i ] > analyzer_dsp_mag[ peak_bin ] )
                peak_bin = i;
        analyzer_dsp_peak_bin = peak_bin;
    }

    return( true );
}

const float * analyzer_dsp_get_magnitudes( void ) {
    return( analyzer_dsp_mag );
}

const int16_t * analyzer_dsp_get_samples( void ) {
    return( analyzer_dsp_capture );
}

size_t analyzer_dsp_get_sample_count( void ) {
    return( analyzer_dsp_filled );
}

float analyzer_dsp_get_peak_db( void ) {
    return( analyzer_dsp_peak_db );
}

uint16_t analyzer_dsp_get_peak_bin( void ) {
    return( analyzer_dsp_peak_bin );
}

float analyzer_dsp_get_peak_frequency( void ) {
    return( ( float )analyzer_dsp_peak_bin * ANALYZER_BIN_WIDTH );
}
