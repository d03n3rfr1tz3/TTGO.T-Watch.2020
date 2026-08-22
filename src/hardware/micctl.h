/****************************************************************************
 *   Aug 20 22:14:00 2026
 *   Copyright  2026  Dirk Sarodnick
 *   Email: dirk.sarodnick@googlemail.com
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
#ifndef _MICCTL_H
    #define _MICCTL_H

    #include "callback.h"

    #define MICCTL_START                _BV(0)      /** @brief event mask for mic started, callback arg is (uint32_t*) sample rate */
    #define MICCTL_STOP                 _BV(1)      /** @brief event mask for mic stopped, callback arg is NULL */
    #define MICCTL_START_REQUEST        _BV(2)      /** @brief event mask for a start taking back a pending stop, callback arg is (uint32_t*) sample rate */
    #define MICCTL_STOP_REQUEST         _BV(3)      /** @brief event mask for a deferred stop, callback arg is NULL */

    #define MICCTL_DEFAULT_SAMPLE_RATE  16000       /** @brief default sample rate in hz */
    #define MICCTL_STOP_HOLD_TIME       500         /** @brief hold time in ms before a requested stop is served */

    /**
     * @brief setup the microphone
     */
    void micctl_setup( void );
    /**
     * @brief check if a microphone is available
     * @return true if available
     */
    bool micctl_get_available( void );
    /**
     * @brief start recording, takes back a pending stop
     *
     * does nothing if the microphone already runs at that sample rate
     *
     * @param   sample_rate     sample rate in hz
     *
     * @return  true if the microphone is running
     */
    bool micctl_start( uint32_t sample_rate = MICCTL_DEFAULT_SAMPLE_RATE );
    /**
     * @brief request to stop recording, served after MICCTL_STOP_HOLD_TIME
     *
     * deferred so a tile change does not tear the microphone down, standby stops at once
     */
    void micctl_stop( void );
    /**
     * @brief check if the microphone is recording
     * @return true if running
     */
    bool micctl_is_running( void );
    /**
     * @brief read mono samples from the microphone
     *
     * @param   buffer          target buffer
     * @param   max_samples     size of the target buffer in samples
     * @param   timeout_ms      0 does not block, use it from the gui thread
     *
     * @return  number of samples read, not bytes
     */
    size_t micctl_read( int16_t *buffer, size_t max_samples, uint32_t timeout_ms );
    /**
     * @brief registers a callback function which is called on a corresponding event
     *
     * @param   event           possible values: MICCTL_START, MICCTL_STOP, MICCTL_START_REQUEST, MICCTL_STOP_REQUEST
     * @param   callback_func   pointer to the callback function
     * @param   id              programm id
     *
     * @return  true if success, false if failed
     */
    bool micctl_register_cb( EventBits_t event, CALLBACK_FUNC callback_func, const char *id );

#endif // _MICCTL_H
