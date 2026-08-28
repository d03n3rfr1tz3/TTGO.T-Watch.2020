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
#ifndef _VOICEREC_CONFIG_H
    #define _VOICEREC_CONFIG_H

    #include "utils/basejsonconfig.h"
    #include "voicerec_recorder.h"

    #define VOICEREC_JSON_CONFIG_FILE   "/voicerec.json"

    /**
     * @brief voicerec config structure
     */
    class voicerec_config_t : public BaseJsonConfig {
        public:
        voicerec_config_t();
        bool low_quality = false;                                   /** @brief unsigned 8 bit instead of signed 16 bit */
        uint8_t gain = VOICEREC_GAIN_DEFAULT;                       /** @brief index into VOICEREC_GAIN_OPTIONS */

        protected:
        ////////////// Available for overloading: //////////////
        virtual bool onLoad(JsonDocument& document);
        virtual bool onSave(JsonDocument& document);
        virtual bool onDefault( void );
        virtual size_t getJsonBufferSize() { return 200; }
    } ;

#endif // _VOICEREC_CONFIG_H
