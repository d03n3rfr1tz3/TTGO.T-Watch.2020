/****************************************************************************
 *   Aug 20 22:14:00 2026
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
#ifndef _SOUNDMETER_APP_MAIN_H
    #define _SOUNDMETER_APP_MAIN_H

    #include "hardware/micctl.h"

    #define SOUNDMETER_SAMPLE_RATE      16000       /** @brief sample rate in hz */
    #define SOUNDMETER_BLOCK_SAMPLES    512         /** @brief samples per read */
    #define SOUNDMETER_MAX_BLOCKS       4           /** @brief blocks per task run, keeps the dma buffers drained */
    #define SOUNDMETER_SPL_FLOOR        30.0f       /** @brief lowest displayed level in dB SPL, a quiet room */
    #define SOUNDMETER_SPL_CEIL         110.0f      /** @brief upper end of the bar in dB SPL */
    #define SOUNDMETER_DB_FLOOR         ( SOUNDMETER_SPL_FLOOR - MICCTL_SPL_OFFSET )     /** @brief the same floor in dBFS */
    #define SOUNDMETER_PEAK_DECAY       1.0f        /** @brief peak hold decay in dB per task run */

    void soundmeter_app_main_setup( uint32_t tile_num );

#endif // _SOUNDMETER_APP_MAIN_H
