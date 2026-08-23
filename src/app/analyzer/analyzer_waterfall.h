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
#ifndef _ANALYZER_WATERFALL_H
    #define _ANALYZER_WATERFALL_H

    #include "analyzer_canvas.h"

    #define ANALYZER_WATERFALL_MARKER_COLOR LV_COLOR_MAKE( 0x80, 0x80, 0x80 )    /** @brief the running time edge */
    #define ANALYZER_WATERFALL_FREQ_MIN     60.0f                           /** @brief left edge of the axis in hz */
    #define ANALYZER_WATERFALL_FREQ_MAX     8000.0f                         /** @brief right edge, the nyquist limit of the sample rate */

    void analyzer_waterfall_setup( uint32_t tile_num );
    void analyzer_waterfall_enter( void );
    void analyzer_waterfall_leave( void );
    void analyzer_waterfall_update( void );

#endif // _ANALYZER_WATERFALL_H
