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
#ifndef _ASSIST_PAIR_H
    #define _ASSIST_PAIR_H

    #include <stdint.h>

    #define ASSIST_PAIR_PORT            8124                        /** @brief the listener home assistant redirects the phone to */
    #define ASSIST_PAIR_TIMEOUT         120000                      /** @brief ms to scan and confirm */
    #define ASSIST_PAIR_WS_TIMEOUT      15000                       /** @brief ms per attempt to get the long lived token */
    #define ASSIST_PAIR_URL_LEN         224                         /** @brief a longer one would not fit the qr code */
    #define ASSIST_PAIR_CLIENT_ID_LEN   32
    #define ASSIST_PAIR_CODE_LEN        128
    #define ASSIST_PAIR_MESSAGE_LEN     32
    #define ASSIST_PAIR_NAME_TRIES      4                           /** @brief a taken client name is the likeliest failure */
    #define ASSIST_PAIR_TASK_STACK      6144
    #define ASSIST_PAIR_PERIOD          100

    typedef enum {
        ASSIST_PAIR_OFF = 0,
        ASSIST_PAIR_WAIT,
        ASSIST_PAIR_EXCHANGE,
        ASSIST_PAIR_TOKEN,
        ASSIST_PAIR_DONE,
        ASSIST_PAIR_ERROR
    } assist_pair_state_t;

    /**
     * @brief needs wifi and a host, returns at once, the result shows up in the state
     */
    bool assist_pair_start( void );
    /**
     * @brief asks the task to end, returns at once
     */
    void assist_pair_stop( void );
    /**
     * @brief OFF, WAIT, EXCHANGE, TOKEN, DONE, ERROR
     */
    assist_pair_state_t assist_pair_get_state( void );
    /**
     * @brief the authorize url for the qr code, empty until the state is WAIT
     */
    const char *assist_pair_get_url( void );
    /**
     * @brief what to show for the current state
     */
    const char *assist_pair_get_message( void );

#endif // _ASSIST_PAIR_H
