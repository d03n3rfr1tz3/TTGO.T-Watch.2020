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
#ifndef _NOTE_EDIT_H
    #define _NOTE_EDIT_H

    #include "config.h"
    #ifdef LV_LVGL_H_INCLUDE_SIMPLE
        #include "lv_core/lv_obj.h"
    #else
        #include "lvgl/src/lv_core/lv_obj.h"
    #endif

    /**
     * @brief setup the note editor app tile, called from note_tile_setup()
     */
    void note_edit_setup( void );
    /**
     * @brief   open the editor
     *
     * an audio note only edits its display name, never its path.
     *
     * @param   entry   note to edit, below zero starts a new text note
     */
    void note_edit_open( int32_t entry );

#endif // _NOTE_EDIT_H
