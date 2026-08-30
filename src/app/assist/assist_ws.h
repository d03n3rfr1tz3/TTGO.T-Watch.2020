/****************************************************************************
 *   Aug 29 20:00:00 2026
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
#ifndef _ASSIST_WS_H
    #define _ASSIST_WS_H

    #include <stdint.h>

    #define ASSIST_WS_PATH              "/api/websocket"
    #define ASSIST_WS_TIMEOUT           8000                        /** @brief ms without an answer before the connection counts as dead */
    #define ASSIST_WS_SEND_TIMEOUT      2000
    #define ASSIST_WS_BUFFER_SIZE       8192                        /** @brief reassembly buffer, a chunked message has to fit in here */
    #define ASSIST_WS_FRAME_SIZE        2048                        /** @brief buffer of the websocket client, chunks arrive in this size */
    #define ASSIST_WS_JSON_SIZE         2048                        /** @brief enough with the filter below, not without it */
    #define ASSIST_WS_LIST_JSON_SIZE    4096                        /** @brief second pass, only for the pipeline list */
    #define ASSIST_WS_MESSAGE_LEN       32
    #define ASSIST_WS_TASK_STACK        8192
    #define ASSIST_WS_TASK_PRIO         3
    #define ASSIST_WS_ID_TOKEN          1                           /** @brief message id of the long lived token request */
    #define ASSIST_WS_ID_PIPELINES      2                           /** @brief message id of the pipeline list request */
    #define ASSIST_WS_ID_RUN            3                           /** @brief first pipeline run, home assistant wants increasing ids per connection */
    #define ASSIST_WS_RUN_TIMEOUT       35000                       /** @brief ms without run-end, a bit above the timeout home assistant gets */
    #define ASSIST_WS_HA_TIMEOUT        30                          /** @brief seconds home assistant gives its own run */
    #define ASSIST_WS_AUDIO_FRAME       1024                        /** @brief pcm bytes per binary frame, plus the handler byte it stays below the frame size */
    #define ASSIST_WS_TRANSCRIPT_LEN    128
    #define ASSIST_WS_ANSWER_LEN        256
    #define ASSIST_WS_CONV_ID_LEN       64                          /** @brief a conversation id is an ulid, 26 chars */
    #define ASSIST_WS_CLIENT_NAME_LEN   48                          /** @brief has to be unique per user in home assistant */
    #define ASSIST_WS_TOKEN_LIFESPAN    3650                        /** @brief days, ten years spares us any refresh logic */
    #define ASSIST_WS_PIPELINE_MAX      8                           /** @brief entries taken from the list, the rest is dropped */
    #define ASSIST_WS_PIPELINE_NAME_LEN 24                          /** @brief one dropdown entry, longer names are cut */

    typedef enum {
        ASSIST_WS_OFF = 0,
        ASSIST_WS_CONNECTING,
        ASSIST_WS_AUTH,
        ASSIST_WS_READY,
        ASSIST_WS_ERROR
    } assist_ws_state_t;

    typedef enum {
        ASSIST_RUN_OFF = 0,
        ASSIST_RUN_STARTING,                                        /** @brief run sent, waiting for run-start */
        ASSIST_RUN_LISTENING,                                       /** @brief the handler id is known, audio may flow */
        ASSIST_RUN_THINKING,                                        /** @brief no more audio, waiting for the answer */
        ASSIST_RUN_DONE,
        ASSIST_RUN_FAILED
    } assist_ws_run_t;

    /**
     * @brief register the standby handler, called once from assist_app_setup()
     */
    void assist_ws_setup( void );
    /**
     * @brief init and start, returns at once, the result shows up in the state
     */
    bool assist_ws_connect( const char *token );
    /**
     * @brief returns at once, the socket is torn down in a one shot task
     */
    void assist_ws_disconnect( void );
    /**
     * @brief gui thread only, it enforces the connect timeout
     */
    assist_ws_state_t assist_ws_get_state( void );
    /**
     * @brief what to show for the current state, empty while off
     */
    const char *assist_ws_get_message( void );
    /**
     * @brief ask for a long lived token on the next auth_ok, call before assist_ws_connect()
     */
    void assist_ws_request_token( const char *client_name );
    /**
     * @brief the issued long lived token, empty until it arrived
     */
    const char *assist_ws_get_issued_token( void );
    /**
     * @brief true once after a new pipeline list arrived, gui thread only
     */
    bool assist_ws_take_pipelines( void );
    /**
     * @brief the pipeline names, newline separated, without the preferred entry
     */
    const char *assist_ws_get_pipeline_options( void );
    uint8_t assist_ws_get_pipeline_count( void );
    /**
     * @brief the id belonging to an option, empty when out of range
     */
    const char *assist_ws_get_pipeline_id( uint8_t index );
    /**
     * @brief start a pipeline run, transcript and answer show up in the getters below
     *
     * every run gets its own id, events carrying another one are dropped
     *
     * @param   pipeline        pipeline id, empty lets home assistant pick its preferred one
     */
    bool assist_ws_run( const char *pipeline );
    /**
     * @brief forget the run, late events are dropped by their id and home assistant times out on its own
     */
    void assist_ws_run_reset( void );
    /**
     * @brief plain read, safe from any task
     */
    assist_ws_run_t assist_ws_get_run( void );
    /**
     * @brief send one pcm block, the handler byte is put in front here
     *
     * @param   pcm             signed 16 bit little endian mono samples
     * @param   len             bytes, at most ASSIST_WS_AUDIO_FRAME
     */
    bool assist_ws_send_audio( const void *pcm, uint32_t len );
    /**
     * @brief the lone handler byte, it tells home assistant that the audio ended
     */
    bool assist_ws_send_audio_end( void );
    /**
     * @brief true once after transcript or answer changed, gui thread only
     */
    bool assist_ws_take_text( void );
    const char *assist_ws_get_transcript( void );
    const char *assist_ws_get_answer( void );

#endif // _ASSIST_WS_H
