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

#include "analyzer_canvas.h"

#include "hardware/micctl.h"
#include "utils/alloc.h"

#define ANALYZER_CANVAS_HEAT_STEPS  128

typedef struct {
    float spl;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} analyzer_canvas_stop_t;

static const analyzer_canvas_stop_t analyzer_canvas_stops[] = {
    {  25.0f, 0x00, 0x02, 0x28 },
    {  32.0f, 0x00, 0x30, 0x38 },
    {  38.0f, 0x10, 0x90, 0x30 },
    {  48.0f, 0x70, 0xd0, 0x20 },
    {  58.0f, 0xf0, 0xe0, 0x20 },
    {  68.0f, 0xf8, 0xa8, 0x10 },
    {  78.0f, 0xf8, 0x60, 0x00 },
    {  88.0f, 0xf0, 0x10, 0x10 },
    { 110.0f, 0xa0, 0x10, 0xd0 },
};

static lv_color_t analyzer_canvas_heat_table[ ANALYZER_CANVAS_HEAT_STEPS ];
static bool analyzer_canvas_heat_ready = false;

static void analyzer_canvas_build_heat_table( void ) {
    int stops = sizeof( analyzer_canvas_stops ) / sizeof( analyzer_canvas_stops[ 0 ] );

    for( int i = 0 ; i < ANALYZER_CANVAS_HEAT_STEPS ; i++ ) {
        float spl = ANALYZER_CANVAS_SPL_MIN + ( ANALYZER_CANVAS_SPL_MAX - ANALYZER_CANVAS_SPL_MIN ) * ( float )i / ( float )( ANALYZER_CANVAS_HEAT_STEPS - 1 );
        int stop = 0;
        while( stop < stops - 2 && spl > analyzer_canvas_stops[ stop + 1 ].spl )
            stop++;

        const analyzer_canvas_stop_t *from = &analyzer_canvas_stops[ stop ];
        const analyzer_canvas_stop_t *to = &analyzer_canvas_stops[ stop + 1 ];
        float mix = ( spl - from->spl ) / ( to->spl - from->spl );

        analyzer_canvas_heat_table[ i ] = lv_color_make( ( uint8_t )( from->red + ( to->red - from->red ) * mix ),
                                                         ( uint8_t )( from->green + ( to->green - from->green ) * mix ),
                                                         ( uint8_t )( from->blue + ( to->blue - from->blue ) * mix ) );
    }

    analyzer_canvas_heat_ready = true;
}

lv_obj_t * analyzer_canvas_create( lv_obj_t *parent ) {
    lv_obj_t *canvas = lv_canvas_create( parent, NULL );

    lv_obj_set_size( canvas, ANALYZER_CANVAS_WIDTH, ANALYZER_CANVAS_HEIGHT );
    lv_obj_set_click( canvas, false );
    lv_obj_set_hidden( canvas, true );

    return( canvas );
}

bool analyzer_canvas_alloc( lv_obj_t *canvas ) {
    if( !canvas )
        return( false );

    if( analyzer_canvas_ready( canvas ) )
        return( true );

    void *buffer = MALLOC( LV_CANVAS_BUF_SIZE_TRUE_COLOR( ANALYZER_CANVAS_WIDTH, ANALYZER_CANVAS_HEIGHT ) );
    if( !buffer ) {
        log_e("analyzer canvas alloc failed");
        return( false );
    }

    lv_canvas_set_buffer( canvas, buffer, ANALYZER_CANVAS_WIDTH, ANALYZER_CANVAS_HEIGHT, LV_IMG_CF_TRUE_COLOR );
    lv_canvas_fill_bg( canvas, analyzer_canvas_floor_color(), LV_OPA_COVER );
    lv_obj_set_hidden( canvas, false );

    return( true );
}

void analyzer_canvas_free( lv_obj_t *canvas ) {
    if( !analyzer_canvas_ready( canvas ) )
        return;

    lv_img_dsc_t *img = lv_canvas_get_img( canvas );

    lv_obj_set_hidden( canvas, true );
    free( ( void * )img->data );
    img->data = NULL;
}

bool analyzer_canvas_ready( lv_obj_t *canvas ) {
    if( !canvas )
        return( false );

    lv_img_dsc_t *img = lv_canvas_get_img( canvas );

    return( img && img->data );
}

lv_color_t * analyzer_canvas_row( lv_obj_t *canvas, lv_coord_t y ) {
    lv_img_dsc_t *img = lv_canvas_get_img( canvas );

    return( ( lv_color_t * )img->data + ( uint32_t )y * ANALYZER_CANVAS_WIDTH );
}

void analyzer_canvas_invalidate_rows( lv_obj_t *canvas, lv_coord_t y, lv_coord_t count ) {
    analyzer_canvas_invalidate_area( canvas, 0, y, ANALYZER_CANVAS_WIDTH, count );
}

void analyzer_canvas_invalidate_area( lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height ) {
    lv_area_t coords;
    lv_area_t area;

    lv_obj_get_coords( canvas, &coords );

    area.x1 = coords.x1 + x;
    area.x2 = area.x1 + width - 1;
    area.y1 = coords.y1 + y;
    area.y2 = area.y1 + height - 1;

    lv_obj_invalidate_area( canvas, &area );
}

lv_color_t analyzer_canvas_heat( float db ) {
    return( analyzer_canvas_heat_spl( micctl_dbfs_to_spl( db ) ) );
}

lv_color_t analyzer_canvas_heat_spl( float spl ) {
    if( !analyzer_canvas_heat_ready )
        analyzer_canvas_build_heat_table();

    float level = ( spl - ANALYZER_CANVAS_SPL_MIN ) / ( ANALYZER_CANVAS_SPL_MAX - ANALYZER_CANVAS_SPL_MIN );

    if( level <= 0.0f )
        return( analyzer_canvas_heat_table[ 0 ] );
    if( level >= 1.0f )
        return( analyzer_canvas_heat_table[ ANALYZER_CANVAS_HEAT_STEPS - 1 ] );

    return( analyzer_canvas_heat_table[ ( int )( level * ( ANALYZER_CANVAS_HEAT_STEPS - 1 ) + 0.5f ) ] );
}

lv_color_t analyzer_canvas_floor_color( void ) {
    if( !analyzer_canvas_heat_ready )
        analyzer_canvas_build_heat_table();

    return( analyzer_canvas_heat_table[ 0 ] );
}
