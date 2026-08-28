/****************************************************************************
 *   Aug 28 20:00:00 2026
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

#include "voicerec_nr.h"

#ifdef VOICEREC_NR_ENABLE

static float *voicerec_nr_window = NULL;                                        /** @brief sqrt hann, VOICEREC_NR_SIZE */
static float *voicerec_nr_cos = NULL;                                           /** @brief twiddle real part, VOICEREC_NR_HALF */
static float *voicerec_nr_sin = NULL;                                           /** @brief twiddle imaginary part, VOICEREC_NR_HALF */
static float *voicerec_nr_re = NULL;                                            /** @brief working buffer, VOICEREC_NR_SIZE */
static float *voicerec_nr_im = NULL;                                            /** @brief working buffer, VOICEREC_NR_SIZE */
static float *voicerec_nr_hist = NULL;                                          /** @brief the last VOICEREC_NR_SIZE input samples */
static float *voicerec_nr_ola = NULL;                                           /** @brief overlap add accumulator, VOICEREC_NR_SIZE */
static float *voicerec_nr_in = NULL;                                            /** @brief hop being filled, VOICEREC_NR_HOP */
static float *voicerec_nr_noise = NULL;                                         /** @brief tracked noise power per bin, VOICEREC_NR_BINS */
static float *voicerec_nr_ps = NULL;                                            /** @brief smoothed periodogram, the tracker feeds on this */
static float *voicerec_nr_prev_h = NULL;                                        /** @brief last gain per bin, decision directed */
static float *voicerec_nr_prev_p = NULL;                                        /** @brief last power per bin, decision directed */

static uint16_t voicerec_nr_fill = 0;                                           /** @brief samples collected into the current hop */
static uint32_t voicerec_nr_frames = 0;                                         /** @brief frames processed since setup */

bool voicerec_nr_setup( void ) {
    if( voicerec_nr_window )
        return( true );
    /**
     * plain malloc/calloc on purpose, CALLOC would land in psram and this runs per sample
     */
    voicerec_nr_window = ( float * )malloc( VOICEREC_NR_SIZE * sizeof( float ) );
    voicerec_nr_cos = ( float * )malloc( VOICEREC_NR_HALF * sizeof( float ) );
    voicerec_nr_sin = ( float * )malloc( VOICEREC_NR_HALF * sizeof( float ) );
    voicerec_nr_re = ( float * )malloc( VOICEREC_NR_SIZE * sizeof( float ) );
    voicerec_nr_im = ( float * )malloc( VOICEREC_NR_SIZE * sizeof( float ) );
    voicerec_nr_hist = ( float * )calloc( VOICEREC_NR_SIZE, sizeof( float ) );
    voicerec_nr_ola = ( float * )calloc( VOICEREC_NR_SIZE, sizeof( float ) );
    voicerec_nr_in = ( float * )calloc( VOICEREC_NR_HOP, sizeof( float ) );
    voicerec_nr_noise = ( float * )calloc( VOICEREC_NR_BINS, sizeof( float ) );
    voicerec_nr_ps = ( float * )calloc( VOICEREC_NR_BINS, sizeof( float ) );
    voicerec_nr_prev_h = ( float * )calloc( VOICEREC_NR_BINS, sizeof( float ) );
    voicerec_nr_prev_p = ( float * )calloc( VOICEREC_NR_BINS, sizeof( float ) );

    if( !voicerec_nr_window || !voicerec_nr_cos || !voicerec_nr_sin || !voicerec_nr_re || !voicerec_nr_im ||
        !voicerec_nr_hist || !voicerec_nr_ola || !voicerec_nr_in ||
        !voicerec_nr_noise || !voicerec_nr_ps || !voicerec_nr_prev_h || !voicerec_nr_prev_p ) {
        log_e("voicerec: nr alloc failed");
        voicerec_nr_free();
        return( false );
    }
    /**
     * periodic hann, not the symmetric one, sqrt at both ends adds up to exactly 1 at hop n/2
     */
    for( uint16_t i = 0 ; i < VOICEREC_NR_SIZE ; i++ )
        voicerec_nr_window[ i ] = sqrtf( 0.5f * ( 1.0f - cosf( 2.0f * ( float )M_PI * ( float )i / ( float )VOICEREC_NR_SIZE ) ) );

    for( uint16_t i = 0 ; i < VOICEREC_NR_HALF ; i++ ) {
        float angle = -2.0f * ( float )M_PI * ( float )i / ( float )VOICEREC_NR_SIZE;
        voicerec_nr_cos[ i ] = cosf( angle );
        voicerec_nr_sin[ i ] = sinf( angle );
    }

    voicerec_nr_fill = 0;
    voicerec_nr_frames = 0;

    return( true );
}

void voicerec_nr_free( void ) {
    if( voicerec_nr_window ) { free( voicerec_nr_window ); voicerec_nr_window = NULL; }
    if( voicerec_nr_cos ) { free( voicerec_nr_cos ); voicerec_nr_cos = NULL; }
    if( voicerec_nr_sin ) { free( voicerec_nr_sin ); voicerec_nr_sin = NULL; }
    if( voicerec_nr_re ) { free( voicerec_nr_re ); voicerec_nr_re = NULL; }
    if( voicerec_nr_im ) { free( voicerec_nr_im ); voicerec_nr_im = NULL; }
    if( voicerec_nr_hist ) { free( voicerec_nr_hist ); voicerec_nr_hist = NULL; }
    if( voicerec_nr_ola ) { free( voicerec_nr_ola ); voicerec_nr_ola = NULL; }
    if( voicerec_nr_in ) { free( voicerec_nr_in ); voicerec_nr_in = NULL; }
    if( voicerec_nr_noise ) { free( voicerec_nr_noise ); voicerec_nr_noise = NULL; }
    if( voicerec_nr_ps ) { free( voicerec_nr_ps ); voicerec_nr_ps = NULL; }
    if( voicerec_nr_prev_h ) { free( voicerec_nr_prev_h ); voicerec_nr_prev_h = NULL; }
    if( voicerec_nr_prev_p ) { free( voicerec_nr_prev_p ); voicerec_nr_prev_p = NULL; }
}

static void voicerec_nr_transform( void ) {
    float *re = voicerec_nr_re;
    float *im = voicerec_nr_im;

    for( uint16_t i = 1, j = 0 ; i < VOICEREC_NR_SIZE ; i++ ) {
        uint16_t bit = VOICEREC_NR_HALF;
        for( ; j & bit ; bit >>= 1 )
            j ^= bit;
        j ^= bit;
        if( i < j ) {
            float tmp = re[ i ]; re[ i ] = re[ j ]; re[ j ] = tmp;
            tmp = im[ i ]; im[ i ] = im[ j ]; im[ j ] = tmp;
        }
    }

    for( uint16_t len = 2 ; len <= VOICEREC_NR_SIZE ; len <<= 1 ) {
        uint16_t half = len >> 1;
        uint16_t step = VOICEREC_NR_SIZE / len;
        for( uint16_t base = 0 ; base < VOICEREC_NR_SIZE ; base += len ) {
            for( uint16_t k = 0 ; k < half ; k++ ) {
                uint16_t a = base + k;
                uint16_t b = a + half;
                float wr = voicerec_nr_cos[ k * step ];
                float wi = voicerec_nr_sin[ k * step ];
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

static void voicerec_nr_frame( void ) {
    for( uint16_t i = 0 ; i < VOICEREC_NR_SIZE ; i++ ) {
        voicerec_nr_re[ i ] = voicerec_nr_hist[ i ] * voicerec_nr_window[ i ];
        voicerec_nr_im[ i ] = 0.0f;
    }

    voicerec_nr_transform();

    for( uint16_t k = 0 ; k < VOICEREC_NR_BINS ; k++ ) {
        float power = voicerec_nr_re[ k ] * voicerec_nr_re[ k ] + voicerec_nr_im[ k ] * voicerec_nr_im[ k ];
        float noise, gamma, prior, h;

        voicerec_nr_ps[ k ] = voicerec_nr_frames
                            ? VOICEREC_NR_SMOOTH * voicerec_nr_ps[ k ] + ( 1.0f - VOICEREC_NR_SMOOTH ) * power
                            : power;
        /**
         * the first frames seed the profile, after that the minimum is tracked so a
         * user who starts talking right away does not poison it for good
         */
        if( voicerec_nr_frames < VOICEREC_NR_LEARN )
            voicerec_nr_noise[ k ] += ( power - voicerec_nr_noise[ k ] ) / ( float )( voicerec_nr_frames + 1 );
        else if( voicerec_nr_ps[ k ] < voicerec_nr_noise[ k ] )
            voicerec_nr_noise[ k ] += ( voicerec_nr_ps[ k ] - voicerec_nr_noise[ k ] ) * VOICEREC_NR_DOWN;
        else
            voicerec_nr_noise[ k ] *= VOICEREC_NR_RISE;

        noise = voicerec_nr_noise[ k ] < 1e-9f ? 1e-9f : voicerec_nr_noise[ k ];
        gamma = power / noise - 1.0f;
        if( gamma < 0.0f )
            gamma = 0.0f;

        prior = VOICEREC_NR_ALPHA * voicerec_nr_prev_h[ k ] * voicerec_nr_prev_h[ k ] * voicerec_nr_prev_p[ k ] / noise
              + ( 1.0f - VOICEREC_NR_ALPHA ) * gamma;
        h = prior / ( 1.0f + prior );
        if( h < VOICEREC_NR_FLOOR )
            h = VOICEREC_NR_FLOOR;

        voicerec_nr_prev_h[ k ] = h;
        voicerec_nr_prev_p[ k ] = power;

        voicerec_nr_re[ k ] *= h;
        voicerec_nr_im[ k ] *= h;
        if( k && k < VOICEREC_NR_HALF ) {
            voicerec_nr_re[ VOICEREC_NR_SIZE - k ] *= h;
            voicerec_nr_im[ VOICEREC_NR_SIZE - k ] *= h;
        }
    }
    /**
     * inverse by conjugation, the result is real so only re is kept
     */
    for( uint16_t i = 0 ; i < VOICEREC_NR_SIZE ; i++ )
        voicerec_nr_im[ i ] = -voicerec_nr_im[ i ];

    voicerec_nr_transform();

    for( uint16_t i = 0 ; i < VOICEREC_NR_SIZE ; i++ )
        voicerec_nr_ola[ i ] += voicerec_nr_re[ i ] / ( float )VOICEREC_NR_SIZE * voicerec_nr_window[ i ];

    voicerec_nr_frames++;
}

void voicerec_nr_process( float *samples, size_t len ) {
    if( !voicerec_nr_window || !samples )
        return;

    for( size_t i = 0 ; i < len ; i++ ) {
        float in = samples[ i ];

        samples[ i ] = voicerec_nr_ola[ voicerec_nr_fill ];
        voicerec_nr_in[ voicerec_nr_fill ] = in;

        if( ++voicerec_nr_fill < VOICEREC_NR_HOP )
            continue;

        voicerec_nr_fill = 0;
        memmove( voicerec_nr_hist, voicerec_nr_hist + VOICEREC_NR_HOP, ( VOICEREC_NR_SIZE - VOICEREC_NR_HOP ) * sizeof( float ) );
        memcpy( voicerec_nr_hist + VOICEREC_NR_SIZE - VOICEREC_NR_HOP, voicerec_nr_in, VOICEREC_NR_HOP * sizeof( float ) );
        memmove( voicerec_nr_ola, voicerec_nr_ola + VOICEREC_NR_HOP, ( VOICEREC_NR_SIZE - VOICEREC_NR_HOP ) * sizeof( float ) );
        memset( voicerec_nr_ola + VOICEREC_NR_SIZE - VOICEREC_NR_HOP, 0, VOICEREC_NR_HOP * sizeof( float ) );

        voicerec_nr_frame();
    }
}

#else // VOICEREC_NR_ENABLE

bool voicerec_nr_setup( void ) { return( true ); }
void voicerec_nr_free( void ) { }
void voicerec_nr_process( float *samples, size_t len ) { }

#endif // VOICEREC_NR_ENABLE
