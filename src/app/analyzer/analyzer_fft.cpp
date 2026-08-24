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

#include "analyzer_fft.h"

#if defined(__has_include)
    #if __has_include(<esp_dsp.h>)
        #pragma message "esp-dsp is available now, the own fft can be replaced"
    #endif
#endif

#define ANALYZER_FFT_HALF           ( ANALYZER_FFT_SIZE / 2 )

static float *analyzer_fft_window = NULL;                                       /** @brief hann window, ANALYZER_FFT_SIZE */
static float *analyzer_fft_cos = NULL;                                          /** @brief twiddle real part, ANALYZER_FFT_HALF */
static float *analyzer_fft_sin = NULL;                                          /** @brief twiddle imaginary part, ANALYZER_FFT_HALF */
static float *analyzer_fft_re = NULL;                                           /** @brief working buffer, ANALYZER_FFT_SIZE */
static float *analyzer_fft_im = NULL;                                           /** @brief working buffer, ANALYZER_FFT_SIZE */

static const float analyzer_fft_scale = 4.0f / ( 32768.0f * ( float )ANALYZER_FFT_SIZE );

bool analyzer_fft_setup( void ) {
    if( analyzer_fft_window )
        return( true );

    analyzer_fft_window = ( float * )malloc( ANALYZER_FFT_SIZE * sizeof( float ) );
    analyzer_fft_cos = ( float * )malloc( ANALYZER_FFT_HALF * sizeof( float ) );
    analyzer_fft_sin = ( float * )malloc( ANALYZER_FFT_HALF * sizeof( float ) );
    analyzer_fft_re = ( float * )malloc( ANALYZER_FFT_SIZE * sizeof( float ) );
    analyzer_fft_im = ( float * )malloc( ANALYZER_FFT_SIZE * sizeof( float ) );

    if( !analyzer_fft_window || !analyzer_fft_cos || !analyzer_fft_sin || !analyzer_fft_re || !analyzer_fft_im ) {
        log_e("analyzer fft alloc failed");
        analyzer_fft_free();
        return( false );
    }

    for( uint16_t i = 0 ; i < ANALYZER_FFT_SIZE ; i++ )
        analyzer_fft_window[ i ] = 0.5f * ( 1.0f - cosf( 2.0f * ( float )M_PI * ( float )i / ( float )( ANALYZER_FFT_SIZE - 1 ) ) );

    for( uint16_t i = 0 ; i < ANALYZER_FFT_HALF ; i++ ) {
        float angle = -2.0f * ( float )M_PI * ( float )i / ( float )ANALYZER_FFT_SIZE;
        analyzer_fft_cos[ i ] = cosf( angle );
        analyzer_fft_sin[ i ] = sinf( angle );
    }

    return( true );
}

void analyzer_fft_free( void ) {
    if( analyzer_fft_window ) { free( analyzer_fft_window ); analyzer_fft_window = NULL; }
    if( analyzer_fft_cos ) { free( analyzer_fft_cos ); analyzer_fft_cos = NULL; }
    if( analyzer_fft_sin ) { free( analyzer_fft_sin ); analyzer_fft_sin = NULL; }
    if( analyzer_fft_re ) { free( analyzer_fft_re ); analyzer_fft_re = NULL; }
    if( analyzer_fft_im ) { free( analyzer_fft_im ); analyzer_fft_im = NULL; }
}

static void analyzer_fft_transform( void ) {
    float *re = analyzer_fft_re;
    float *im = analyzer_fft_im;

    for( uint16_t i = 1, j = 0 ; i < ANALYZER_FFT_SIZE ; i++ ) {
        uint16_t bit = ANALYZER_FFT_HALF;
        for( ; j & bit ; bit >>= 1 )
            j ^= bit;
        j ^= bit;
        if( i < j ) {
            float tmp = re[ i ]; re[ i ] = re[ j ]; re[ j ] = tmp;
            tmp = im[ i ]; im[ i ] = im[ j ]; im[ j ] = tmp;
        }
    }

    for( uint16_t len = 2 ; len <= ANALYZER_FFT_SIZE ; len <<= 1 ) {
        uint16_t half = len >> 1;
        uint16_t step = ANALYZER_FFT_SIZE / len;
        for( uint16_t base = 0 ; base < ANALYZER_FFT_SIZE ; base += len ) {
            for( uint16_t k = 0 ; k < half ; k++ ) {
                uint16_t a = base + k;
                uint16_t b = a + half;
                float wr = analyzer_fft_cos[ k * step ];
                float wi = analyzer_fft_sin[ k * step ];
                float vr = re[ b ] * wr - im[ b ] * wi;
                float vi = re[ b ] * wi + im[ b ] * wr;
                re[ b ] = re[ a ] - vr;
                im[ b ] = im[ a ] - vi;
                re[ a ] += vr;
                im[ a ] += vi;
            }
        }
    }
}

void analyzer_fft_magnitudes( const int16_t *samples, float *mag_db ) {
    if( !analyzer_fft_window || !samples || !mag_db )
        return;

    for( uint16_t i = 0 ; i < ANALYZER_FFT_SIZE ; i++ ) {
        analyzer_fft_re[ i ] = ( float )samples[ i ] * analyzer_fft_window[ i ];
        analyzer_fft_im[ i ] = 0.0f;
    }

    analyzer_fft_transform();

    for( uint16_t i = 0 ; i < ANALYZER_FFT_BINS ; i++ ) {
        float power = analyzer_fft_re[ i ] * analyzer_fft_re[ i ] + analyzer_fft_im[ i ] * analyzer_fft_im[ i ];
        power *= analyzer_fft_scale * analyzer_fft_scale;
        mag_db[ i ] = power < 1e-10f ? ANALYZER_FFT_DB_FLOOR : 10.0f * log10f( power );
        if( mag_db[ i ] < ANALYZER_FFT_DB_FLOOR )
            mag_db[ i ] = ANALYZER_FFT_DB_FLOOR;
    }
}
