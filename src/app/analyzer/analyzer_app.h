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
#ifndef _ANALYZER_APP_H
    #define _ANALYZER_APP_H

    #include "analyzer_canvas.h"

    #include "gui/widget_factory.h"

    #define ANALYZER_APP_HEADER_Y       THEME_PADDING                                               /** @brief readout inside the canvas, top center */
    #define ANALYZER_APP_CANVAS_Y       0                                                           /** @brief canvas starts at the top edge */
    #define ANALYZER_APP_AXIS_Y         ( ANALYZER_APP_CANVAS_Y + ANALYZER_CANVAS_HEIGHT + THEME_PADDING )   /** @brief axis right below the canvas */
    #define ANALYZER_APP_TILES          4                                                           /** @brief waterfall, spectrum, scope and tone */
    #define ANALYZER_APP_IDLE_PERIOD    250                                                         /** @brief poll period in ms while the app is off screen */

    void analyzer_app_setup( void );
    uint32_t analyzer_app_get_app_main_tile_num( void );
    /**
     * @brief add the shared button row
     *
     * @param   tile    tile object
     */
    void analyzer_app_add_footer( lv_obj_t *tile );
    /**
     * @brief add the frequency axis labels below a canvas
     *
     * @param   tile    tile object
     * @param   text    labels, centered on their position and kept inside the canvas
     * @param   x       position of every label, canvas relative
     * @param   count   number of labels
     * @param   y       top of the label row
     */
    void analyzer_app_add_axis( lv_obj_t *tile, const char **text, const lv_coord_t *x, int count, lv_coord_t y );

#endif // _ANALYZER_APP_H
