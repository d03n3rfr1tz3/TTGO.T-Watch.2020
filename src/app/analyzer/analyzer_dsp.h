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
#ifndef _ANALYZER_DSP_H
    #define _ANALYZER_DSP_H

    #include <stddef.h>
    #include <stdint.h>

    #include "analyzer_fft.h"

    #define ANALYZER_SAMPLE_RATE        16000                                                                   /** @brief sample rate in hz */
    #define ANALYZER_BLOCK_SAMPLES      256                                                                     /** @brief samples per read, independent of the transform length */
    #define ANALYZER_MAX_BLOCKS         8                                                                       /** @brief blocks per update, keeps the dma buffers drained */
    #define ANALYZER_CAPTURE_SAMPLES    1024                                                                    /** @brief rolling window of the newest samples, dc free */
    #define ANALYZER_TASK_PERIOD        50                                                                      /** @brief display period in ms */
    #define ANALYZER_BIN_WIDTH          ( ( float )ANALYZER_SAMPLE_RATE / ( float )ANALYZER_FFT_SIZE )           /** @brief bin width in hz */
    #define ANALYZER_DB_FLOOR           ANALYZER_FFT_DB_FLOOR                                                   /** @brief lowest displayed level in dBFS */
    #define ANALYZER_PEAK_FIRST_FREQ    100                                                                   /** @brief the spm1423 is specified from here up */
    #define ANALYZER_PEAK_FIRST_BIN     ( ( ANALYZER_PEAK_FIRST_FREQ * ANALYZER_FFT_SIZE + ANALYZER_SAMPLE_RATE / 2 ) / ANALYZER_SAMPLE_RATE )   /** @brief first bin of the peak search, nearest bin to the limit */

    /**
     * @brief allocate the buffers and start the microphone, reference counted
     *
     * the app calls this once while any of its tiles is on screen
     *
     * @return  true if the microphone is running
     */
    bool analyzer_dsp_start( void );
    /**
     * @brief release one reference, frees the buffers and stops the microphone at zero
     */
    void analyzer_dsp_stop( void );
    /**
     * @brief check if capturing
     * @return true if running
     */
    bool analyzer_dsp_is_running( void );
    /**
     * @brief drain the dma buffers into the capture window
     *
     * @param   with_fft    run the transform, the time domain tiles do not need it
     *
     * @return  true if new samples arrived
     */
    bool analyzer_dsp_update( bool with_fft );
    /**
     * @brief magnitudes of the last transform in dBFS, ANALYZER_FFT_BINS values
     */
    const float * analyzer_dsp_get_magnitudes( void );
    /**
     * @brief the capture window, oldest sample first, ANALYZER_CAPTURE_SAMPLES values
     */
    const int16_t * analyzer_dsp_get_samples( void );
    /**
     * @brief number of valid samples at the end of the capture window
     */
    size_t analyzer_dsp_get_sample_count( void );
    /**
     * @brief peak level of the newest block in dBFS
     */
    float analyzer_dsp_get_peak_db( void );
    /**
     * @brief strongest bin of the last transform
     */
    uint16_t analyzer_dsp_get_peak_bin( void );
    /**
     * @brief frequency of the strongest bin in hz
     */
    float analyzer_dsp_get_peak_frequency( void );

#endif // _ANALYZER_DSP_H
