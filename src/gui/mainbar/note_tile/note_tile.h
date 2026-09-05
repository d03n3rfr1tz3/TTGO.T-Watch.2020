/****************************************************************************
 *   Tu May 22 21:23:51 2020
 *   Copyright  2020  Dirk Brosswick
 *   Email: dirk.brosswick@googlemail.com
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
#ifndef _NOTE_TILE_H
    #define _NOTE_TILE_H

    #include "config.h"
    #ifdef LV_LVGL_H_INCLUDE_SIMPLE
        #include "lv_core/lv_obj.h"
    #else
        #include "lvgl/src/lv_core/lv_obj.h"
    #endif

    #define NOTE_SOURCE_MAX     2       /** @brief room for create buttons in the footer */

    /**
     * @brief setup note tile
     */
    void note_tile_setup( void );
    /**
     * @brief get the tile number for the note tile
     *
     * @return  tile number
     */
    uint32_t note_tile_get_tile_num( void );
    /**
     * @brief redraw the notes from the stored config
     */
    void note_tile_refresh( void );
    /**
     * @brief   register an additional create button on the note tile
     *
     * apps call this from their setup, the note tile knows nothing about them. registering is
     * optional in every sense: without it the tile just shows the plain plus button.
     *
     * @param   name        source name, for logging
     * @param   icon        system icon in the current theme size, must differ from the plus button
     * @param   event_cb    called when the button is pressed
     *
     * @return  true if registered, false if no slot is left
     */
    bool note_tile_register_source( const char *name, const lv_img_dsc_t *icon, lv_event_cb_t event_cb );
    /**
     * @brief   add a note referencing an existing audio file
     *
     * the note holds a reference, not the file. checking the note off drops the note and leaves
     * the recording where it is.
     *
     * @param   path    file to play, for example "/rec/2026-09-04_0712.wav"
     * @param   name    display name
     *
     * @return  true if the note was added, false if all slots are taken
     */
    bool note_tile_add_audio_note( const char *path, const char *name );

#endif // _NOTE_TILE_H
