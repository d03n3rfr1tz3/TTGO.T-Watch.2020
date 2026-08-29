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
#ifndef _ANALYZER_CANVAS_H
    #define _ANALYZER_CANVAS_H

    #define ANALYZER_CANVAS_WIDTH       240         /** @brief canvas width, matches the display */
    #define ANALYZER_CANVAS_HEIGHT      170         /** @brief canvas height, from the top edge down to the axis row */
    /*
     * these are levels per frequency bin, not broadband levels - noise like signal
     * spreads over all bins and lands about 10*log10( bins ) lower than the readout
     */
    #define ANALYZER_CANVAS_SPL_MIN     25.0f       /** @brief lower end of the color scale in dB SPL */
    #define ANALYZER_CANVAS_SPL_MAX     110.0f      /** @brief upper end of the color scale in dB SPL, purple */

    /**
     * @brief create a canvas without a buffer, it is allocated on activate
     *
     * @param   parent      parent object
     *
     * @return  the canvas object
     */
    lv_obj_t * analyzer_canvas_create( lv_obj_t *parent );
    /**
     * @brief allocate the image buffer and attach it to the canvas
     *
     * @return  true if success, false if failed
     */
    bool analyzer_canvas_alloc( lv_obj_t *canvas );
    /**
     * @brief free the image buffer, the canvas object stays
     */
    void analyzer_canvas_free( lv_obj_t *canvas );
    /**
     * @brief check if the canvas has a buffer
     */
    bool analyzer_canvas_ready( lv_obj_t *canvas );
    /**
     * @brief raw pointer to one row, ANALYZER_CANVAS_WIDTH pixels wide
     *
     * lv_canvas_set_px() would do the same but is slow for so many pixels
     *
     * @param   y           row, not checked
     */
    lv_color_t * analyzer_canvas_row( lv_obj_t *canvas, lv_coord_t y );
    /**
     * @brief tell lvgl which rows changed
     *
     * @param   y           first row
     * @param   count       number of rows
     */
    void analyzer_canvas_invalidate_rows( lv_obj_t *canvas, lv_coord_t y, lv_coord_t count );
    /**
     * @brief tell lvgl which rectangle changed
     */
    void analyzer_canvas_invalidate_area( lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height );
    /**
     * @brief map a level to a color, dark blue over green and yellow to red and purple
     *
     * @param   db          level in dBFS, converted to dB SPL internally
     */
    lv_color_t analyzer_canvas_heat( float db );
    /**
     * @brief map a level to a color
     *
     * @param   spl         level in dB SPL
     */
    lv_color_t analyzer_canvas_heat_spl( float spl );
    /**
     * @brief the color of the lowest level, used to clear the canvas
     */
    lv_color_t analyzer_canvas_floor_color( void );

#endif // _ANALYZER_CANVAS_H
