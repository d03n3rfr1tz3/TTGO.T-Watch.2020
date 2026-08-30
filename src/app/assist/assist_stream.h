/****************************************************************************
 *   Aug 30 20:00:00 2026
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
#ifndef _ASSIST_STREAM_H
    #define _ASSIST_STREAM_H

    #include <stdint.h>

    #define ASSIST_STREAM_RING_SIZE     ( 64 * 1024 )               /** @brief ~2s headroom against a stalling wifi, not against spiffs */
    #define ASSIST_STREAM_BLOCK_SAMPLES 512                         /** @brief samples per micctl_read() */
    #define ASSIST_STREAM_MAX_SECONDS   15                          /** @brief hard cap, the reader stops itself */
    #define ASSIST_STREAM_JOIN_TIMEOUT  2000                        /** @brief max wait for both tasks in the standby path */
    #define ASSIST_STREAM_SENDER_TICK   20                          /** @brief sender poll period in ms */
    #define ASSIST_STREAM_DB_FLOOR      -60.0f                      /** @brief lower end of the level readout */
    #define ASSIST_HP_B0                0.9329322f                  /** @brief 250 hz butterworth highpass at 16 khz */
    #define ASSIST_HP_B1                -1.8658643f
    #define ASSIST_HP_B2                0.9329322f
    #define ASSIST_HP_A1                -1.8613611f
    #define ASSIST_HP_A2                0.8703675f
    #define ASSIST_LIM_CEIL             8192.0f                     /** @brief -12 dBFS */
    #define ASSIST_LIM_LOOKAHEAD        160                         /** @brief 10 ms at 16 khz, the gain is in place before the peak arrives */
    #define ASSIST_LIM_RELEASE          0.9993f                     /** @brief peak envelope decay after the hold, ~90 ms */
    #define ASSIST_LIM_ATTACK           0.05f                       /** @brief gain is down within ~4 ms, the lookahead is 10 ms */
    #define ASSIST_LIM_RECOVER          0.0005f                     /** @brief gain returns over ~125 ms, faster pumps audibly */

    typedef enum {
        ASSIST_STREAM_IDLE = 0,
        ASSIST_STREAM_RECORDING,                                    /** @brief the mic is open, both tasks are running */
        ASSIST_STREAM_SENDING,                                      /** @brief capture ended, the ring is still on its way out */
        ASSIST_STREAM_ERROR                                         /** @brief last session failed, start() clears it */
    } assist_stream_state_t;

    typedef struct {
        float delay[ ASSIST_LIM_LOOKAHEAD ];                        /** @brief the signal waits here while the gain catches up */
        float env;                                                  /** @brief peak envelope, instant attack */
        float gain;                                                 /** @brief smoothed gain reduction */
        uint32_t hold;                                              /** @brief samples the envelope stays at its peak */
        uint32_t pos;
    } assist_limiter_t;

    /**
     * @brief setup the streaming engine, registers the standby handler
     */
    void assist_stream_setup( void );
    /**
     * @brief open the mic and start the run, returns at once
     *
     * the reader captures from here on, the sender issues the run command and waits
     * for the handler id, so the beginning of the first word is not lost
     *
     * @param   pipeline        pipeline id, empty lets home assistant pick its preferred one
     * @param   gain_index      entry of ASSIST_GAIN_OPTIONS
     */
    bool assist_stream_start( const char *pipeline, uint8_t gain_index );
    /**
     * @brief end of speech, the rest of the ring is still sent, returns at once
     */
    void assist_stream_stop( void );
    /**
     * @brief drop the session, the run is forgotten and home assistant times out on its own
     */
    void assist_stream_abort( void );
    assist_stream_state_t assist_stream_get_state( void );
    /**
     * @brief level of the last block in dBFS
     */
    float assist_stream_get_level_db( void );
    uint32_t assist_stream_get_seconds( void );

#endif // _ASSIST_STREAM_H
