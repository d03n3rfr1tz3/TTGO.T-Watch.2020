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
#ifndef _ANALYZER_FFT_H
    #define _ANALYZER_FFT_H

    #include <stdint.h>

    #define ANALYZER_FFT_SIZE           512                                     /** @brief fft length in samples, must be a power of two */
    #define ANALYZER_FFT_BINS           ( ANALYZER_FFT_SIZE / 2 )               /** @brief usable bins, bin 0 is dc */
    #define ANALYZER_FFT_DB_FLOOR       -90.0f                                  /** @brief lowest reported magnitude in dBFS */

    /**
     * @brief allocate the window, the twiddle table and the working buffers
     *
     * everything lives in internal ram, the fft is the hottest path in this app
     *
     * @return  true if success, false if failed
     */
    bool analyzer_fft_setup( void );
    /**
     * @brief free everything allocated by analyzer_fft_setup()
     */
    void analyzer_fft_free( void );
    /**
     * @brief transform one block and return the magnitudes in dBFS
     *
     * @param   samples     ANALYZER_FFT_SIZE samples, dc offset already removed
     * @param   mag_db      target for ANALYZER_FFT_BINS magnitudes in dBFS
     */
    void analyzer_fft_magnitudes( const int16_t *samples, float *mag_db );

#endif // _ANALYZER_FFT_H
