/****************************************************************************
 *   Aug 28 20:00:00 2026
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
#ifndef _VOICEREC_NR_H
    #define _VOICEREC_NR_H

    #include <stdint.h>
    #include <stddef.h>

    #define VOICEREC_NR_ENABLE                                      /** @brief comment out to bypass the stage completely */

    #define VOICEREC_NR_SIZE            256                         /** @brief stft length, 16 ms at 16 khz */
    #define VOICEREC_NR_HOP             128                         /** @brief 50 % overlap, sqrt hann adds up exactly at n/2 */
    #define VOICEREC_NR_HALF            ( VOICEREC_NR_SIZE / 2 )
    #define VOICEREC_NR_BINS            ( VOICEREC_NR_SIZE / 2 + 1 )
    #define VOICEREC_NR_FLOOR           0.1259f                     /** @brief -18 db, the clamp is what keeps it from bubbling */
    #define VOICEREC_NR_ALPHA           0.98f                       /** @brief decision directed smoothing */
    #define VOICEREC_NR_LEARN           8                           /** @brief frames that seed the noise profile */
    #define VOICEREC_NR_SMOOTH          0.8f                        /** @brief periodogram smoothing, without it the tracker reads 7-10 db low */
    #define VOICEREC_NR_DOWN            0.05f                       /** @brief rate the noise estimate follows downwards */
    #define VOICEREC_NR_RISE            1.002f                      /** @brief rate it may climb, speech must not drag it up */

    #ifdef VOICEREC_NR_ENABLE
        #define VOICEREC_NR_DELAY       VOICEREC_NR_SIZE            /** @brief samples held back, has to be flushed at stop */
    #else
        #define VOICEREC_NR_DELAY       0
    #endif

    /**
     * @brief allocate window, twiddles and the tracker state
     *
     * everything lives in internal ram, this runs per sample in the reader task
     *
     * @return  true if success, false if failed
     */
    bool voicerec_nr_setup( void );
    /**
     * @brief free everything allocated by voicerec_nr_setup()
     */
    void voicerec_nr_free( void );
    /**
     * @brief denoise a block in place, delayed by VOICEREC_NR_DELAY samples
     *
     * @param   samples     block to process, any length
     * @param   len         number of samples
     */
    void voicerec_nr_process( float *samples, size_t len );

#endif // _VOICEREC_NR_H
