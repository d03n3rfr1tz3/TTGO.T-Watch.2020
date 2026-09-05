/****************************************************************************
 *   Sep 04 20:00:00 2026
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

#include "note_style.h"

#define NOTE_PAPER_YELLOW       0xf4e56a
#define NOTE_PAPER_PINK         0xf6a6bb
#define NOTE_PAPER_GREEN        0xb7e58a
#define NOTE_PAPER_NIGHT        LV_OPA_30   /** @brief brightness taken off the paper in a dark theme */

#define NOTE_GLUE_DARKEN        LV_OPA_20   /** @brief darker top zone, the glue band of a real post-it */
#define NOTE_SHADOW_MIX         160         /** @brief paper share in the hard shadow edge */
#define NOTE_SHADOW_WIDTH       2
#define NOTE_TAPE_OPA           LV_OPA_30

static bool note_style_init = false;
static lv_style_t note_paper_style[ NOTE_PAPER_VARIANTS ];
static lv_style_t note_tape_style;
static lv_style_t note_text_style;

static const uint32_t note_paper_color[ NOTE_PAPER_VARIANTS ] = { NOTE_PAPER_YELLOW, NOTE_PAPER_PINK, NOTE_PAPER_GREEN };

void note_style_setup( void ) {
    if ( note_style_init )
        return;

    lv_color_t theme_ink = LV_COLOR_WHITE;
    _lv_style_get_color( ws_get_mainbar_style(), LV_STYLE_TEXT_COLOR, &theme_ink );

    bool dark = lv_color_brightness( theme_ink ) > 128;

    for ( int32_t i = 0 ; i < NOTE_PAPER_VARIANTS ; i++ ) {
        lv_color_t paper = lv_color_hex( note_paper_color[ i ] );

        if ( dark )
            paper = lv_color_darken( paper, NOTE_PAPER_NIGHT );

        lv_style_copy( &note_paper_style[ i ], ws_get_mainbar_style() );
        lv_style_set_radius( &note_paper_style[ i ], LV_OBJ_PART_MAIN, 0 );
        lv_style_set_bg_opa( &note_paper_style[ i ], LV_OBJ_PART_MAIN, LV_OPA_COVER );
        lv_style_set_bg_color( &note_paper_style[ i ], LV_OBJ_PART_MAIN, lv_color_darken( paper, NOTE_GLUE_DARKEN ) );
        lv_style_set_bg_grad_color( &note_paper_style[ i ], LV_OBJ_PART_MAIN, paper );
        lv_style_set_bg_grad_dir( &note_paper_style[ i ], LV_OBJ_PART_MAIN, LV_GRAD_DIR_VER );
        lv_style_set_border_width( &note_paper_style[ i ], LV_OBJ_PART_MAIN, NOTE_SHADOW_WIDTH );
        lv_style_set_border_side( &note_paper_style[ i ], LV_OBJ_PART_MAIN, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT );
        lv_style_set_border_color( &note_paper_style[ i ], LV_OBJ_PART_MAIN, lv_color_mix( paper, LV_COLOR_BLACK, NOTE_SHADOW_MIX ) );
        lv_style_set_border_opa( &note_paper_style[ i ], LV_OBJ_PART_MAIN, LV_OPA_COVER );
        lv_style_set_text_color( &note_paper_style[ i ], LV_OBJ_PART_MAIN, LV_COLOR_BLACK );
    }

    lv_style_copy( &note_text_style, ws_get_mainbar_style() );
    lv_style_set_text_color( &note_text_style, LV_OBJ_PART_MAIN, LV_COLOR_BLACK );
    lv_style_set_text_opa( &note_text_style, LV_OBJ_PART_MAIN, LV_OPA_COVER );

    lv_style_copy( &note_tape_style, ws_get_mainbar_style() );
    lv_style_set_image_recolor( &note_tape_style, LV_OBJ_PART_MAIN, theme_ink );
    lv_style_set_image_recolor_opa( &note_tape_style, LV_OBJ_PART_MAIN, LV_OPA_TRANSP );
    lv_style_set_image_opa( &note_tape_style, LV_OBJ_PART_MAIN, NOTE_TAPE_OPA );

    note_style_init = true;

    log_i("note: post-it styles built, paper %s", dark ? "damped for the night" : "bright" );
}

lv_style_t *note_style_get_paper( int32_t index ) {
    if ( index < 0 )
        index = 0;

    return( &note_paper_style[ index % NOTE_PAPER_VARIANTS ] );
}

lv_style_t *note_style_get_tape( void ) {
    return( &note_tape_style );
}

lv_style_t *note_style_get_text( void ) {
    return( &note_text_style );
}
