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

#include <stdlib.h>

#include "analyzer_app.h"
#include "analyzer_canvas.h"
#include "analyzer_dsp.h"
#include "analyzer_scope.h"

#include "gui/mainbar/mainbar.h"

static const char *analyzer_scope_axis_text[] = { "0", "10ms", "20ms", "30ms" };
static const lv_coord_t analyzer_scope_axis_x[] = { 0, 80, 160, 239 };

static lv_obj_t *analyzer_scope_tile = NULL;
static lv_obj_t *analyzer_scope_canvas = NULL;
static lv_color_t analyzer_scope_floor;
static float analyzer_scope_scale = ANALYZER_SCOPE_SCALE_MIN;                   /** @brief full scale amplitude, follows the peak and falls back */
static bool analyzer_scope_drawn = false;                                       /** @brief true if the arrays hold a trace to erase */
static uint8_t analyzer_scope_top[ ANALYZER_CANVAS_WIDTH ];
static uint8_t analyzer_scope_bottom[ ANALYZER_CANVAS_WIDTH ];

static void analyzer_scope_draw_trace( void );

void analyzer_scope_setup( uint32_t tile_num ) {

    analyzer_scope_tile = mainbar_get_tile_obj( tile_num );

    analyzer_scope_canvas = analyzer_canvas_create( analyzer_scope_tile );
    lv_obj_align( analyzer_scope_canvas, analyzer_scope_tile, LV_ALIGN_IN_TOP_MID, 0, ANALYZER_APP_CANVAS_Y );

    analyzer_app_add_axis( analyzer_scope_tile, analyzer_scope_axis_text, analyzer_scope_axis_x,
                           sizeof( analyzer_scope_axis_x ) / sizeof( analyzer_scope_axis_x[ 0 ] ),
                           ANALYZER_APP_AXIS_Y );

    analyzer_app_add_footer( analyzer_scope_tile );

    lv_tileview_add_element( analyzer_scope_tile, analyzer_scope_canvas );
}

static lv_color_t analyzer_scope_background( lv_coord_t x, lv_coord_t y ) {
    if( y == ANALYZER_SCOPE_ZERO_Y )
        return( ANALYZER_SCOPE_ZERO_COLOR );
    if( !( x % ANALYZER_SCOPE_GRID_PITCH ) )
        return( ANALYZER_SCOPE_GRID_COLOR );

    return( analyzer_scope_floor );
}

void analyzer_scope_enter( void ) {
    analyzer_scope_scale = ANALYZER_SCOPE_SCALE_MIN;
    analyzer_scope_drawn = false;
    analyzer_scope_floor = analyzer_canvas_floor_color();

    if( !analyzer_canvas_alloc( analyzer_scope_canvas ) )
        return;

    for( lv_coord_t y = 0 ; y < ANALYZER_CANVAS_HEIGHT ; y++ ) {
        lv_color_t *row = analyzer_canvas_row( analyzer_scope_canvas, y );

        for( lv_coord_t x = 0 ; x < ANALYZER_CANVAS_WIDTH ; x++ )
            row[ x ] = analyzer_scope_background( x, y );
    }
}

void analyzer_scope_leave( void ) {
    analyzer_canvas_free( analyzer_scope_canvas );
}

void analyzer_scope_update( void ) {
    if( analyzer_canvas_ready( analyzer_scope_canvas ) )
        analyzer_scope_draw_trace();
}

static lv_coord_t analyzer_scope_row_of( int32_t value, float gain ) {
    int32_t y = ANALYZER_SCOPE_ZERO_Y - ( int32_t )( value * gain );

    if( y < 0 )
        y = 0;
    if( y > ANALYZER_CANVAS_HEIGHT - 1 )
        y = ANALYZER_CANVAS_HEIGHT - 1;

    return( ( lv_coord_t )y );
}

static void analyzer_scope_draw_trace( void ) {
    const int16_t *samples = analyzer_dsp_get_samples();

    if( analyzer_dsp_get_sample_count() < ANALYZER_CAPTURE_SAMPLES )
        return;

    int32_t peak = 0;
    for( size_t i = 0 ; i < ANALYZER_CAPTURE_SAMPLES ; i++ ) {
        int32_t value = abs( samples[ i ] );
        if( value > peak )
            peak = value;
    }
    /*
     * rising zero crossing with hysteresis, otherwise every harmonic triggers as well.
     */
    int32_t threshold = peak / ANALYZER_SCOPE_TRIGGER_DIV;
    if( threshold < ANALYZER_SCOPE_TRIGGER_FLOOR )
        threshold = ANALYZER_SCOPE_TRIGGER_FLOOR;

    size_t start = ANALYZER_CAPTURE_SAMPLES - ANALYZER_SCOPE_SPAN;
    bool armed = false;

    for( size_t i = 0 ; i < ANALYZER_CAPTURE_SAMPLES - ANALYZER_SCOPE_SPAN ; i++ ) {
        if( samples[ i ] < -threshold )
            armed = true;
        else if( armed && samples[ i ] >= threshold ) {
            start = i;
            break;
        }
    }

    if( ( float )peak > analyzer_scope_scale )
        analyzer_scope_scale = ( float )peak;
    else
        analyzer_scope_scale *= ANALYZER_SCOPE_SCALE_DECAY;

    if( analyzer_scope_scale < ANALYZER_SCOPE_SCALE_MIN )
        analyzer_scope_scale = ANALYZER_SCOPE_SCALE_MIN;

    float gain = ( float )( ANALYZER_SCOPE_ZERO_Y - 1 ) / analyzer_scope_scale;
    lv_color_t color = analyzer_canvas_heat( analyzer_dsp_get_peak_db() );
    lv_color_t *base = analyzer_canvas_row( analyzer_scope_canvas, 0 );
    lv_coord_t dirty_top = ANALYZER_CANVAS_HEIGHT;
    lv_coord_t dirty_bottom = -1;
    lv_coord_t last_top = 0;
    lv_coord_t last_bottom = 0;

    for( lv_coord_t x = 0 ; x < ANALYZER_CANVAS_WIDTH ; x++ ) {
        const int16_t *block = samples + start + x * ANALYZER_SCOPE_SAMPLES_PER_PX;
        int32_t min = block[ 0 ];
        int32_t max = block[ 0 ];

        for( int i = 1 ; i < ANALYZER_SCOPE_SAMPLES_PER_PX ; i++ ) {
            if( block[ i ] < min )
                min = block[ i ];
            if( block[ i ] > max )
                max = block[ i ];
        }

        lv_coord_t top = analyzer_scope_row_of( max, gain );
        lv_coord_t bottom = analyzer_scope_row_of( min, gain );
        /*
         * stretch up to the neighbour, else steep edges fall apart into single dots
         */
        if( x ) {
            if( top > last_bottom )
                top = last_bottom;
            else if( bottom < last_top )
                bottom = last_top;
        }

        if( analyzer_scope_drawn ) {
            lv_coord_t old_top = analyzer_scope_top[ x ];
            lv_coord_t old_bottom = analyzer_scope_bottom[ x ];

            for( lv_coord_t y = old_top ; y <= old_bottom ; y++ )
                if( y < top || y > bottom )
                    base[ y * ANALYZER_CANVAS_WIDTH + x ] = analyzer_scope_background( x, y );

            if( old_top < dirty_top )
                dirty_top = old_top;
            if( old_bottom > dirty_bottom )
                dirty_bottom = old_bottom;
        }

        for( lv_coord_t y = top ; y <= bottom ; y++ )
            base[ y * ANALYZER_CANVAS_WIDTH + x ] = color;

        if( top < dirty_top )
            dirty_top = top;
        if( bottom > dirty_bottom )
            dirty_bottom = bottom;

        analyzer_scope_top[ x ] = ( uint8_t )top;
        analyzer_scope_bottom[ x ] = ( uint8_t )bottom;
        last_top = top;
        last_bottom = bottom;
    }

    analyzer_scope_drawn = true;

    if( dirty_bottom >= dirty_top )
        analyzer_canvas_invalidate_rows( analyzer_scope_canvas, dirty_top, dirty_bottom - dirty_top + 1 );
}
