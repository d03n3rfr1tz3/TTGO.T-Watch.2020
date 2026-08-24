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
#ifndef _ANALYZER_SCOPE_H
    #define _ANALYZER_SCOPE_H

    #include "analyzer_canvas.h"

    #define ANALYZER_SCOPE_SAMPLES_PER_PX   2                                                               /** @brief time base, 240 px cover 30 ms at 16 khz */
    #define ANALYZER_SCOPE_SPAN             ( ANALYZER_CANVAS_WIDTH * ANALYZER_SCOPE_SAMPLES_PER_PX )        /** @brief samples on screen */
    #define ANALYZER_SCOPE_ZERO_Y           ( ANALYZER_CANVAS_HEIGHT / 2 )                                   /** @brief the zero line */
    #define ANALYZER_SCOPE_GRID_PITCH       80                                                              /** @brief vertical grid every 10 ms */
    #define ANALYZER_SCOPE_TRIGGER_DIV      4                                                               /** @brief trigger threshold as a fraction of the window peak */
    #define ANALYZER_SCOPE_TRIGGER_FLOOR    64                                                              /** @brief lowest threshold, keeps noise from triggering */
    #define ANALYZER_SCOPE_SCALE_DECAY      0.97f                                                           /** @brief scale falls back per frame, halves in about a second */
    #define ANALYZER_SCOPE_SCALE_MIN        256.0f                                                          /** @brief smallest full scale amplitude */
    #define ANALYZER_SCOPE_GRID_COLOR       LV_COLOR_MAKE( 0x18, 0x22, 0x44 )                                /** @brief the time grid */
    #define ANALYZER_SCOPE_ZERO_COLOR       LV_COLOR_MAKE( 0x30, 0x3c, 0x60 )                                /** @brief the zero line */

    void analyzer_scope_setup( uint32_t tile_num );
    void analyzer_scope_enter( void );
    void analyzer_scope_leave( void );
    void analyzer_scope_update( void );

#endif // _ANALYZER_SCOPE_H
