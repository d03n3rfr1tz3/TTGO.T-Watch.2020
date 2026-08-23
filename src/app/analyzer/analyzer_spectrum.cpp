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
#include <string.h>

#include "analyzer_app.h"
#include "analyzer_canvas.h"
#include "analyzer_dsp.h"
#include "analyzer_spectrum.h"

#include "gui/mainbar/mainbar.h"
#include "hardware/micctl.h"

static const char *analyzer_spectrum_axis_text[] = { "100", "400", "1.6k", "8k" };
static const lv_coord_t analyzer_spectrum_axis_x[] = { ANALYZER_SPECTRUM_BAR_LEFT,
                                                       ANALYZER_SPECTRUM_BAR_LEFT + 6 * ANALYZER_SPECTRUM_BAR_PITCH,
                                                       ANALYZER_SPECTRUM_BAR_LEFT + 12 * ANALYZER_SPECTRUM_BAR_PITCH,
                                                       ANALYZER_SPECTRUM_BAR_LEFT + 19 * ANALYZER_SPECTRUM_BAR_PITCH };

static lv_obj_t *analyzer_spectrum_tile = NULL;
static lv_obj_t *analyzer_spectrum_canvas = NULL;
static uint32_t analyzer_spectrum_updates = 0;
static uint16_t analyzer_spectrum_bin[ ANALYZER_SPECTRUM_BANDS + 1 ];
static lv_color_t analyzer_spectrum_gradient[ ANALYZER_CANVAS_HEIGHT ];
static lv_coord_t analyzer_spectrum_height[ ANALYZER_SPECTRUM_BANDS ];
static lv_coord_t analyzer_spectrum_peak[ ANALYZER_SPECTRUM_BANDS ];

static void analyzer_spectrum_draw_bars( void );

void analyzer_spectrum_setup( uint32_t tile_num ) {

    analyzer_spectrum_tile = mainbar_get_tile_obj( tile_num );

    analyzer_spectrum_canvas = analyzer_canvas_create( analyzer_spectrum_tile );
    lv_obj_align( analyzer_spectrum_canvas, analyzer_spectrum_tile, LV_ALIGN_IN_TOP_MID, 0, ANALYZER_APP_CANVAS_Y );

    analyzer_app_add_axis( analyzer_spectrum_tile, analyzer_spectrum_axis_text, analyzer_spectrum_axis_x,
                           sizeof( analyzer_spectrum_axis_x ) / sizeof( analyzer_spectrum_axis_x[ 0 ] ),
                           ANALYZER_APP_AXIS_Y );

    analyzer_app_add_footer( analyzer_spectrum_tile );

    lv_tileview_add_element( analyzer_spectrum_tile, analyzer_spectrum_canvas );

    for( int i = 0 ; i <= ANALYZER_SPECTRUM_BANDS ; i++ ) {
        float frequency = ANALYZER_SPECTRUM_FREQ_MIN * powf( ANALYZER_SPECTRUM_FREQ_MAX / ANALYZER_SPECTRUM_FREQ_MIN, ( float )i / ( float )ANALYZER_SPECTRUM_BANDS );
        uint16_t bin = ( uint16_t )( frequency / ANALYZER_BIN_WIDTH + 0.5f );

        if( i && bin <= analyzer_spectrum_bin[ i - 1 ] )
            bin = analyzer_spectrum_bin[ i - 1 ] + 1;
        if( bin > ANALYZER_FFT_BINS )
            bin = ANALYZER_FFT_BINS;
        if( i < ANALYZER_SPECTRUM_BANDS && bin > ANALYZER_FFT_BINS - 1 )
            bin = ANALYZER_FFT_BINS - 1;

        analyzer_spectrum_bin[ i ] = bin;
    }

    for( int y = 0 ; y < ANALYZER_CANVAS_HEIGHT ; y++ ) {
        float spl = ANALYZER_CANVAS_SPL_MAX - ( ANALYZER_CANVAS_SPL_MAX - ANALYZER_CANVAS_SPL_MIN ) * ( float )y / ( float )( ANALYZER_CANVAS_HEIGHT - 1 );
        analyzer_spectrum_gradient[ y ] = analyzer_canvas_heat_spl( spl );
    }
}

void analyzer_spectrum_enter( void ) {
    analyzer_spectrum_updates = 0;
    memset( analyzer_spectrum_height, 0, sizeof( analyzer_spectrum_height ) );
    memset( analyzer_spectrum_peak, 0, sizeof( analyzer_spectrum_peak ) );

    analyzer_canvas_alloc( analyzer_spectrum_canvas );
}

void analyzer_spectrum_leave( void ) {
    analyzer_canvas_free( analyzer_spectrum_canvas );
}

void analyzer_spectrum_update( void ) {
    if( analyzer_canvas_ready( analyzer_spectrum_canvas ) )
        analyzer_spectrum_draw_bars();

    analyzer_spectrum_updates++;
}

static void analyzer_spectrum_fill( lv_coord_t x, lv_coord_t from, lv_coord_t to, lv_coord_t height ) {
    lv_color_t floor = analyzer_canvas_floor_color();

    for( lv_coord_t y = from ; y < to ; y++ ) {
        lv_color_t color = y >= ANALYZER_CANVAS_HEIGHT - height ? analyzer_spectrum_gradient[ y ] : floor;
        lv_color_t *row = analyzer_canvas_row( analyzer_spectrum_canvas, y ) + x;

        for( lv_coord_t i = 0 ; i < ANALYZER_SPECTRUM_BAR_WIDTH ; i++ )
            row[ i ] = color;
    }
}

static void analyzer_spectrum_draw_bars( void ) {
    const float *magnitudes = analyzer_dsp_get_magnitudes();
    bool decay = !( analyzer_spectrum_updates % ANALYZER_SPECTRUM_PEAK_HOLD );
    lv_coord_t top = ANALYZER_CANVAS_HEIGHT;
    lv_coord_t bottom = 0;

    for( int band = 0 ; band < ANALYZER_SPECTRUM_BANDS ; band++ ) {
        uint16_t from = analyzer_spectrum_bin[ band ];
        uint16_t to = analyzer_spectrum_bin[ band + 1 ];
        float value = magnitudes[ from ];

        for( uint16_t bin = from + 1 ; bin < to ; bin++ )
            if( magnitudes[ bin ] > value )
                value = magnitudes[ bin ];

        float level = ( micctl_dbfs_to_spl( value ) - ANALYZER_CANVAS_SPL_MIN ) / ( ANALYZER_CANVAS_SPL_MAX - ANALYZER_CANVAS_SPL_MIN );
        lv_coord_t height = ( lv_coord_t )( level * ANALYZER_CANVAS_HEIGHT + 0.5f );

        if( height < 0 )
            height = 0;
        if( height > ANALYZER_CANVAS_HEIGHT )
            height = ANALYZER_CANVAS_HEIGHT;

        lv_coord_t x = ANALYZER_SPECTRUM_BAR_LEFT + band * ANALYZER_SPECTRUM_BAR_PITCH;
        lv_coord_t old_height = analyzer_spectrum_height[ band ];
        lv_coord_t old_peak = analyzer_spectrum_peak[ band ];
        lv_coord_t peak = old_peak;

        if( height > peak )
            peak = height;
        else if( decay && peak )
            peak--;

        lv_coord_t peak_y = ANALYZER_CANVAS_HEIGHT - peak;
        bool redraw_peak = peak && peak != old_peak;

        if( height != old_height ) {
            lv_coord_t edge_from = ANALYZER_CANVAS_HEIGHT - ( height > old_height ? height : old_height );
            lv_coord_t edge_to = ANALYZER_CANVAS_HEIGHT - ( height > old_height ? old_height : height );

            analyzer_spectrum_fill( x, edge_from, edge_to, height );

            if( edge_from < top )
                top = edge_from;
            if( edge_to > bottom )
                bottom = edge_to;

            if( peak && peak_y >= edge_from && peak_y < edge_to )
                redraw_peak = true;
        }

        if( old_peak && old_peak != peak ) {
            lv_coord_t y = ANALYZER_CANVAS_HEIGHT - old_peak;

            analyzer_spectrum_fill( x, y, y + 1, height );

            if( y < top )
                top = y;
            if( y + 1 > bottom )
                bottom = y + 1;
        }

        if( redraw_peak ) {
            lv_color_t *row = analyzer_canvas_row( analyzer_spectrum_canvas, peak_y ) + x;

            for( lv_coord_t i = 0 ; i < ANALYZER_SPECTRUM_BAR_WIDTH ; i++ )
                row[ i ] = ANALYZER_SPECTRUM_PEAK_COLOR;

            if( peak_y < top )
                top = peak_y;
            if( peak_y + 1 > bottom )
                bottom = peak_y + 1;
        }

        analyzer_spectrum_height[ band ] = height;
        analyzer_spectrum_peak[ band ] = peak;
    }

    if( bottom > top )
        analyzer_canvas_invalidate_rows( analyzer_spectrum_canvas, top, bottom - top );
}
