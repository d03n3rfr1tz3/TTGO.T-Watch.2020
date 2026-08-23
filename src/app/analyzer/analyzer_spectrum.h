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
#ifndef _ANALYZER_SPECTRUM_H
    #define _ANALYZER_SPECTRUM_H

    #include "analyzer_canvas.h"

    #define ANALYZER_SPECTRUM_BANDS         19                          /** @brief number of third octave bands */
    #define ANALYZER_SPECTRUM_FREQ_MIN      100.0f                      /** @brief lowest band edge in hz, the spm1423 limit */
    #define ANALYZER_SPECTRUM_FREQ_MAX      8000.0f                     /** @brief highest band edge, the nyquist limit of the sample rate */
    #define ANALYZER_SPECTRUM_BAR_PITCH     12                          /** @brief bar plus gap in px */
    #define ANALYZER_SPECTRUM_BAR_WIDTH     11                          /** @brief bar width in px */
    #define ANALYZER_SPECTRUM_BAR_LEFT      ( ( ANALYZER_CANVAS_WIDTH - ANALYZER_SPECTRUM_BANDS * ANALYZER_SPECTRUM_BAR_PITCH ) / 2 )    /** @brief left margin of the first bar */
    #define ANALYZER_SPECTRUM_PEAK_HOLD     2                           /** @brief frames per pixel of peak decay */
    #define ANALYZER_SPECTRUM_PEAK_COLOR    LV_COLOR_MAKE( 0xf0, 0xf0, 0xf0 )    /** @brief the decaying peak marker */

    void analyzer_spectrum_setup( uint32_t tile_num );
    void analyzer_spectrum_enter( void );
    void analyzer_spectrum_leave( void );
    void analyzer_spectrum_update( void );

#endif // _ANALYZER_SPECTRUM_H
