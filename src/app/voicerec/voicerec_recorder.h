/****************************************************************************
 *   Aug 25 20:00:00 2026
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
#ifndef _VOICEREC_RECORDER_H
    #define _VOICEREC_RECORDER_H

    #include <stdint.h>

    #define VOICEREC_SAMPLE_RATE        16000                       /** @brief mono capture rate, assist compatible */
    #define VOICEREC_DIR                "/rec"                      /** @brief spiffs is flat, this is just a name prefix */
    #define VOICEREC_MAX_SECONDS        10                          /** @brief hard cap, the reader stops itself */
    #define VOICEREC_RING_SIZE          ( 384 * 1024 )              /** @brief ~3s headroom against spiffs garbage collection */
    #define VOICEREC_BLOCK_SAMPLES      512                         /** @brief samples per micctl_read() */
    #define VOICEREC_WRITE_CHUNK        4096                        /** @brief bytes per spiffs write */
    #define VOICEREC_RESERVE_BYTES      ( 256 * 1024 )              /** @brief keep spiffs from running dry */
    #define VOICEREC_JOIN_TIMEOUT       2000                        /** @brief max wait for both tasks in the standby path */
    #define VOICEREC_HEADER_SIZE        84                          /** @brief riff + fmt + list/info/icrd + data */
    #define VOICEREC_PATCH_RIFF         4                           /** @brief offset of ChunkSize */
    #define VOICEREC_PATCH_DATA         80                          /** @brief offset of Subchunk2Size */
    #define VOICEREC_ICRD_OFFSET        56                          /** @brief offset of the creation date text */
    #define VOICEREC_ICRD_SIZE          20                          /** @brief "YYYY-MM-DD HH:MM:SS" plus terminator */
    #define VOICEREC_NAME_MAX           22                          /** @brief spiffs: 31 - "/rec/" - ".wav" */
    #define VOICEREC_PATH_MAX           32                          /** @brief CONFIG_SPIFFS_OBJ_NAME_LEN */
    #define VOICEREC_DB_FLOOR           -60.0f                      /** @brief lower end of the level readout */
    #define VOICEREC_HP_B0              0.9329322f                  /** @brief 250 hz butterworth highpass at 16 khz, the speaker radiates nothing below it */
    #define VOICEREC_HP_B1              -1.8658643f
    #define VOICEREC_HP_B2              0.9329322f
    #define VOICEREC_HP_A1              -1.8613611f
    #define VOICEREC_HP_A2              0.8703675f
    #define VOICEREC_LIM_CEIL           8192.0f                     /** @brief -12 dBFS, the headroom SOUND_TONE_AMPLITUDE assumes */
    #define VOICEREC_LIM_LOOKAHEAD      160                         /** @brief 10 ms at 16 khz, the gain is in place before the peak arrives */
    #define VOICEREC_LIM_RELEASE        0.9993f                     /** @brief peak envelope decay after the hold, ~90 ms */
    #define VOICEREC_LIM_ATTACK         0.05f                       /** @brief gain is down within ~4 ms, the lookahead is 10 ms */
    #define VOICEREC_LIM_RECOVER        0.0005f                     /** @brief gain returns over ~125 ms, faster pumps audibly */
    #define VOICEREC_GAIN_OPTIONS       "off\n+18 dB\n+30 dB\n+36 dB\n+42 dB"    /** @brief dropdown entries, one per gain table slot */
    #define VOICEREC_GAIN_COUNT         5                           /** @brief gain table size */
    #define VOICEREC_GAIN_DEFAULT       3                           /** @brief +36 dB, speech at a few cm lands around -20 dbfs */
    #define VOICEREC_WRITER_TICK        20                          /** @brief writer poll period in ms */
    #define VOICEREC_SPACE_CACHE        1000                        /** @brief min age of the cached spiffs free space in ms */

    typedef enum {
        VOICEREC_IDLE = 0,                                          /** @brief ready to record */
        VOICEREC_RECORDING,                                         /** @brief reader and writer are running */
        VOICEREC_FINALIZING,                                        /** @brief ring is drained and the header patched */
        VOICEREC_ERROR                                              /** @brief last take failed, start() clears it */
    } voicerec_state_t;

    typedef struct {
        float delay[ VOICEREC_LIM_LOOKAHEAD ];                      /** @brief the signal waits here while the gain catches up */
        float env;                                                  /** @brief peak envelope, instant attack */
        float gain;                                                 /** @brief smoothed gain reduction */
        uint32_t hold;                                              /** @brief samples the envelope stays at its peak */
        uint32_t pos;
    } voicerec_limiter_t;

    /**
     * @brief setup the recorder engine, registers the standby handler
     */
    void voicerec_recorder_setup( void );
    /**
     * @brief start a take into a new timestamped file
     *
     * @param   low_quality     true for unsigned 8 bit, false for signed 16 bit
     * @param   gain            makeup gain factor, see voicerec_recorder_get_gain()
     *
     * @return  true if reader and writer are up
     */
    bool voicerec_recorder_start( bool low_quality, float gain );
    /**
     * @brief makeup gain factor of a VOICEREC_GAIN_OPTIONS entry
     *
     * @param   index           entry index, out of range falls back to the default
     */
    float voicerec_recorder_get_gain( uint8_t index );
    /**
     * @brief request a stop, returns at once
     */
    void voicerec_recorder_stop( void );
    voicerec_state_t voicerec_recorder_get_state( void );
    uint32_t voicerec_recorder_get_seconds( void );
    /**
     * @brief level of the last block in dBFS
     */
    float voicerec_recorder_get_level_db( void );
    bool voicerec_recorder_get_disk_full( void );
    /**
     * @brief free spiffs space expressed in recordable seconds
     *
     * @param   low_quality     format the estimate is based on
     */
    uint32_t voicerec_recorder_get_remaining_seconds( bool low_quality );
    /**
     * @brief path of the current or last take, empty if there was none
     */
    const char *voicerec_recorder_get_filename( void );

#endif // _VOICEREC_RECORDER_H
