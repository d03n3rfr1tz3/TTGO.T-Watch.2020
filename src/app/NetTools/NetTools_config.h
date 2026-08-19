/****************************************************************************
 *  NetTools_config.h
 *  Copyright  2020  David Stewart / NorthernDIY
 *  Email: genericsoftwaredeveloper@gmail.com
 *
 *  Based on the work of Dirk Brosswick,  sharandac / My-TTGO-Watch  Example_App"
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
#ifndef _NETTOOLS_CONFIG_H
    #define _NETTOOLS_CONFIG_H

    #include "utils/basejsonconfig.h"

    #define NETTOOLS_JSON_CONFIG_FILE   "/NetTools.json"
    #define NETTOOLS_TARGETS            3
    #define NETTOOLS_NAME_LEN           13
    #define NETTOOLS_MAC_LEN            18      /** @brief "AA:BB:CC:DD:EE:FF" plus terminator */

    /**
     * @brief a single wake on lan target
     */
    typedef struct {
        char name[ NETTOOLS_NAME_LEN ] = "";
        char mac[ NETTOOLS_MAC_LEN ] = "";
    } nettools_target_t;

    /**
     * @brief NetTools config structure
     */
    class nettools_config_t : public BaseJsonConfig {
        public:
        nettools_config_t();
        nettools_target_t target[ NETTOOLS_TARGETS ];

        protected:
        ////////////// Available for overloading: //////////////
        virtual bool onLoad(JsonDocument& document);
        virtual bool onSave(JsonDocument& document);
        virtual bool onDefault( void );
        virtual size_t getJsonBufferSize() { return 1000; }
    } ;

    /**
     * @brief check a mac string against the lengths WakeOnLan::stringToArray accepts
     *
     * @param   mac     mac address string
     *
     * @return  true if a magic packet can be built from it
     */
    bool nettools_mac_valid( const char *mac );

#endif // _NETTOOLS_CONFIG_H
