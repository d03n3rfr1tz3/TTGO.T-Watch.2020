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
#ifndef _ASSIST_CONFIG_H
    #define _ASSIST_CONFIG_H

    #include "utils/basejsonconfig.h"

    #define ASSIST_JSON_CONFIG_FILE     "/assist.json"
    #define ASSIST_HOST_LEN             64
    #define ASSIST_TOKEN_LEN            256                         /** @brief a long lived access token is about 180 chars */
    #define ASSIST_PIPELINE_LEN         64
    #define ASSIST_PORT_DEFAULT         8123
    #define ASSIST_GAIN_OPTIONS         "off\n+18 dB\n+30 dB\n+36 dB\n+42 dB"    /** @brief dropdown entries, one per gain table slot */
    #define ASSIST_GAIN_COUNT           5                           /** @brief gain table size */
    #define ASSIST_GAIN_DEFAULT         3                           /** @brief +36 dB, speech at a few cm lands around -20 dbfs */

    /**
     * @brief assist config structure
     */
    class assist_config_t : public BaseJsonConfig {
        public:
        assist_config_t();
        char host[ ASSIST_HOST_LEN ] = "";                          /** @brief home assistant host name or ip */
        uint16_t port = ASSIST_PORT_DEFAULT;
        char token[ ASSIST_TOKEN_LEN ] = "";                        /** @brief long lived access token */
        char pipeline[ ASSIST_PIPELINE_LEN ] = "";                  /** @brief empty selects the preferred pipeline, json only */
        uint8_t gain = ASSIST_GAIN_DEFAULT;                         /** @brief index into ASSIST_GAIN_OPTIONS */
        bool widget = false;                                        /** @brief show the widget icon on the main tile */

        protected:
        ////////////// Available for overloading: //////////////
        virtual bool onLoad(JsonDocument& document);
        virtual bool onSave(JsonDocument& document);
        virtual bool onDefault( void );
        virtual size_t getJsonBufferSize() { return 1024; }
    } ;

    assist_config_t *assist_get_config( void );
    /**
     * @brief mark the config as changed, the next assist_config_save_dirty() writes it
     */
    void assist_config_set_dirty( void );
    /**
     * @brief write /assist.json, but only if something changed
     */
    void assist_config_save_dirty( void );

#endif // _ASSIST_CONFIG_H
