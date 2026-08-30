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

#include "assist_qr.h"

#include "gui/qr_encoder/qrcodegen.h"
#include "utils/alloc.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#else
    #include <Arduino.h>
#endif

#define ASSIST_QR_BUFFER_LEN    qrcodegen_BUFFER_LEN_FOR_VERSION( ASSIST_QR_VERSION_MAX )
#define ASSIST_QR_BUF_SIZE      LV_CANVAS_BUF_SIZE_TRUE_COLOR( ASSIST_QR_SIZE, ASSIST_QR_SIZE )

static bool assist_qr_ready( lv_obj_t *qr );
static lv_color_t *assist_qr_row( lv_obj_t *qr, lv_coord_t y );

lv_obj_t *assist_qr_create( lv_obj_t *parent ) {
    lv_obj_t *canvas = lv_canvas_create( parent, NULL );

    lv_obj_set_size( canvas, ASSIST_QR_SIZE, ASSIST_QR_SIZE );
    lv_obj_set_click( canvas, false );
    lv_obj_set_hidden( canvas, true );
    lv_obj_set_style_local_image_recolor_opa( canvas, LV_IMG_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP );

    return( canvas );
}

bool assist_qr_alloc( lv_obj_t *qr ) {
    if( !qr )
        return( false );

    if( assist_qr_ready( qr ) )
        return( true );

    void *buffer = MALLOC( ASSIST_QR_BUF_SIZE );
    if( !buffer ) {
        log_e("assist: no memory for the qr canvas");
        return( false );
    }

    lv_canvas_set_buffer( qr, buffer, ASSIST_QR_SIZE, ASSIST_QR_SIZE, LV_IMG_CF_TRUE_COLOR );
    assist_qr_clear( qr );
    lv_obj_set_hidden( qr, false );

    return( true );
}

void assist_qr_free( lv_obj_t *qr ) {
    if( !assist_qr_ready( qr ) )
        return;

    lv_img_dsc_t *img = lv_canvas_get_img( qr );

    lv_obj_set_hidden( qr, true );
    lv_img_cache_invalidate_src( img );
    free( ( void * )img->data );
    img->data = NULL;
}

void assist_qr_clear( lv_obj_t *qr ) {
    if( !assist_qr_ready( qr ) )
        return;

    lv_img_dsc_t *img = lv_canvas_get_img( qr );

    memset( ( void * )img->data, 0xff, ASSIST_QR_BUF_SIZE );
    lv_obj_invalidate( qr );
}

bool assist_qr_update( lv_obj_t *qr, const char *text ) {
    uint8_t code[ ASSIST_QR_BUFFER_LEN ];
    uint8_t temp[ ASSIST_QR_BUFFER_LEN ];
    uint32_t len = text ? strlen( text ) : 0;

    if( !assist_qr_ready( qr ) )
        return( false );

    assist_qr_clear( qr );

    if( !len || len > sizeof( temp ) )
        return( false );

    memcpy( temp, text, len );

    if( !qrcodegen_encodeBinary( temp, len, code, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, ASSIST_QR_VERSION_MAX, qrcodegen_Mask_AUTO, true ) ) {
        log_e("assist: %d bytes do not fit into a version %d code", len, ASSIST_QR_VERSION_MAX );
        return( false );
    }

    int modules = qrcodegen_getSize( code );
    int scale = ASSIST_QR_SIZE / modules;
    int margin = ( ASSIST_QR_SIZE - modules * scale ) / 2;

    for( int y = 0 ; y < modules ; y++ ) {
        for( int x = 0 ; x < modules ; x++ ) {
            if( !qrcodegen_getModule( code, x, y ) )
                continue;

            for( int dy = 0 ; dy < scale ; dy++ )
                memset( assist_qr_row( qr, margin + y * scale + dy ) + margin + x * scale, 0x00, scale * sizeof( lv_color_t ) );
        }
    }

    lv_obj_invalidate( qr );
    log_i("assist: qr with %d modules at scale %d", modules, scale );

    return( true );
}

static bool assist_qr_ready( lv_obj_t *qr ) {
    if( !qr )
        return( false );

    lv_img_dsc_t *img = lv_canvas_get_img( qr );

    return( img && img->data );
}

static lv_color_t *assist_qr_row( lv_obj_t *qr, lv_coord_t y ) {
    lv_img_dsc_t *img = lv_canvas_get_img( qr );

    return( ( lv_color_t * )img->data + ( uint32_t )y * ASSIST_QR_SIZE );
}
