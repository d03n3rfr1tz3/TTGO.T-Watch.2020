/****************************************************************************
 *   Copyright  2026  Dirk Sarodnick
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
#ifndef _BLEALARM_H
    #define _BLEALARM_H

    #include <stdint.h>
    #include <stdbool.h>

    #include "hardware/rtcctl.h"
    #include "utils/basejsonconfig.h"

    #define BLEALARM_CONFIG_FILE        "/blealarm.json"
    /**
     * @brief gadgetbridge holds ten alarm slots for a bangle.js device
     */
    #define BLEALARM_MAX_ALARMS         RTCCTL_MAX_EXT_ALARMS

    /**
     * @brief an alarm term received from the phone, there is no ui for it
     */
    typedef struct {
        uint8_t hour;                   /** @brief alarm hour */
        uint8_t minute;                 /** @brief alarm minute */
        uint8_t week_days;              /** @brief bit mask starting from sunday, no bit means every day */
        bool once;                      /** @brief drop the term after it has fired */
    } blealarm_alarm_t;

    class blealarm_config_t : public BaseJsonConfig {
        public:
        blealarm_config_t();
        uint8_t count;                                  /** @brief number of used slots */
        blealarm_alarm_t alarm[ BLEALARM_MAX_ALARMS ];  /** @brief alarm terms */

        protected:
        ////////////// Available for overloading: //////////////
        virtual bool onLoad(JsonDocument& document);
        virtual bool onSave(JsonDocument& document);
        virtual bool onDefault( void );
        virtual size_t getJsonBufferSize() { return 2000; }
    };

    /**
     * @brief setup the gadgetbridge alarm receiver
     */
    void blealarm_setup( void );

#endif // _BLEALARM_H
