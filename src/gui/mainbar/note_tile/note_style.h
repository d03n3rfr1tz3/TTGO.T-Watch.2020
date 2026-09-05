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
#ifndef _NOTE_STYLE_H
    #define _NOTE_STYLE_H

    #include "gui/widget_styles.h"

    #define NOTE_PAPER_VARIANTS     3       /** @brief number of paper shades, cycled by note index */

    /**
     * @brief build the post-it styles from the current theme, call once from note_tile_setup()
     *
     * a theme change takes effect after the next start, like on the neighbour tiles.
     */
    void note_style_setup( void );
    /**
     * @brief   get the paper style for a note
     *
     * @param   index   note index, shades repeat every NOTE_PAPER_VARIANTS
     */
    lv_style_t *note_style_get_paper( int32_t index );
    /**
     * @brief   style for the tape strip, an alpha only image colored by image_recolor
     */
    lv_style_t *note_style_get_tape( void );
    /**
     * @brief   style for the note text
     */
    lv_style_t *note_style_get_text( void );

#endif // _NOTE_STYLE_H
