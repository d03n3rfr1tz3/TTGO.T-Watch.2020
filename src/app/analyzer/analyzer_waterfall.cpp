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

#include "analyzer_app.h"
#include "analyzer_canvas.h"
#include "analyzer_dsp.h"
#include "analyzer_waterfall.h"

#include "gui/mainbar/mainbar.h"

static const char *analyzer_waterfall_axis_text[] = { "60", "250", "1k", "4k", "8k" };
static const lv_coord_t analyzer_waterfall_axis_x[] = { 0, 70, 137, 205, 239 };

static lv_obj_t *analyzer_waterfall_tile = NULL;
static lv_obj_t *analyzer_waterfall_canvas = NULL;
static lv_coord_t analyzer_waterfall_cursor = 0;
static uint16_t analyzer_waterfall_bin[ ANALYZER_CANVAS_WIDTH + 1 ];

static void analyzer_waterfall_draw_row( void );

void analyzer_waterfall_setup( uint32_t tile_num ) {

    analyzer_waterfall_tile = mainbar_get_tile_obj( tile_num );

    analyzer_waterfall_canvas = analyzer_canvas_create( analyzer_waterfall_tile );
    lv_obj_align( analyzer_waterfall_canvas, analyzer_waterfall_tile, LV_ALIGN_IN_TOP_MID, 0, ANALYZER_APP_CANVAS_Y );

    analyzer_app_add_axis( analyzer_waterfall_tile, analyzer_waterfall_axis_text, analyzer_waterfall_axis_x,
                           sizeof( analyzer_waterfall_axis_x ) / sizeof( analyzer_waterfall_axis_x[ 0 ] ),
                           ANALYZER_APP_AXIS_Y );

    analyzer_app_add_footer( analyzer_waterfall_tile );

    lv_tileview_add_element( analyzer_waterfall_tile, analyzer_waterfall_canvas );

    for( int i = 0 ; i <= ANALYZER_CANVAS_WIDTH ; i++ ) {
        float frequency = ANALYZER_WATERFALL_FREQ_MIN * powf( ANALYZER_WATERFALL_FREQ_MAX / ANALYZER_WATERFALL_FREQ_MIN, ( float )i / ( float )ANALYZER_CANVAS_WIDTH );
        uint16_t bin = ( uint16_t )( frequency / ANALYZER_BIN_WIDTH + 0.5f );

        if( bin > ANALYZER_FFT_BINS )
            bin = ANALYZER_FFT_BINS;
        if( i < ANALYZER_CANVAS_WIDTH && bin > ANALYZER_FFT_BINS - 1 )
            bin = ANALYZER_FFT_BINS - 1;

        analyzer_waterfall_bin[ i ] = bin;
    }
}

void analyzer_waterfall_enter( void ) {
    analyzer_waterfall_cursor = 0;

    analyzer_canvas_alloc( analyzer_waterfall_canvas );
}

void analyzer_waterfall_leave( void ) {
    analyzer_canvas_free( analyzer_waterfall_canvas );
}

void analyzer_waterfall_update( void ) {
    if( analyzer_canvas_ready( analyzer_waterfall_canvas ) )
        analyzer_waterfall_draw_row();
}

static void analyzer_waterfall_draw_row( void ) {
    const float *magnitudes = analyzer_dsp_get_magnitudes();
    lv_color_t *row = analyzer_canvas_row( analyzer_waterfall_canvas, analyzer_waterfall_cursor );
    lv_coord_t marker = ( analyzer_waterfall_cursor + 1 ) % ANALYZER_CANVAS_HEIGHT;

    for( int x = 0 ; x < ANALYZER_CANVAS_WIDTH ; x++ ) {
        uint16_t from = analyzer_waterfall_bin[ x ];
        uint16_t to = analyzer_waterfall_bin[ x + 1 ];
        float value = magnitudes[ from ];

        for( uint16_t bin = from + 1 ; bin < to ; bin++ )
            if( magnitudes[ bin ] > value )
                value = magnitudes[ bin ];

        row[ x ] = analyzer_canvas_heat( value );
    }

    row = analyzer_canvas_row( analyzer_waterfall_canvas, marker );
    for( int x = 0 ; x < ANALYZER_CANVAS_WIDTH ; x++ )
        row[ x ] = ANALYZER_WATERFALL_MARKER_COLOR;

    analyzer_canvas_invalidate_rows( analyzer_waterfall_canvas, analyzer_waterfall_cursor, 1 );
    analyzer_canvas_invalidate_rows( analyzer_waterfall_canvas, marker, 1 );

    analyzer_waterfall_cursor = marker;
}
