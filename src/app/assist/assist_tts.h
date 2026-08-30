/****************************************************************************
 *   Aug 30 12:00:00 2026
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
#ifndef _ASSIST_TTS_H
    #define _ASSIST_TTS_H

    #include <stdint.h>

    #define ASSIST_TTS_MAX_BYTES        ( 512 * 1024 )              /** @brief about 11 s of wav, longer answers stay text only */
    #define ASSIST_TTS_TASK_STACK       6144                        /** @brief HTTPClient plus String is not slim */
    #define ASSIST_TTS_URL_LEN          256                         /** @brief the url from home assistant plus host and port */

    typedef enum {
        ASSIST_TTS_OFF = 0,
        ASSIST_TTS_LOADING,                                         /** @brief the load task owns the buffer */
        ASSIST_TTS_READY,                                           /** @brief the gui thread owns the buffer from here on */
        ASSIST_TTS_SPEAKING,
        ASSIST_TTS_ERROR
    } assist_tts_state_t;

    /**
     * @brief register the standby handler, called once from assist_app_setup()
     */
    void assist_tts_setup( void );
    /**
     * @brief start loading the answer, returns at once
     *
     * @param   url             path relative to the host or an absolute url
     */
    bool assist_tts_fetch( const char *url );
    /**
     * @brief gui thread only, starts the playback and frees the buffer when it ended
     */
    void assist_tts_update( void );
    /**
     * @brief stop the playback and drop a running load, returns at once
     */
    void assist_tts_stop( void );
    assist_tts_state_t assist_tts_get_state( void );

#endif // _ASSIST_TTS_H
