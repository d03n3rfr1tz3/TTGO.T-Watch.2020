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
#include "config.h"
#include "voicerec_config.h"

voicerec_config_t::voicerec_config_t() : BaseJsonConfig( VOICEREC_JSON_CONFIG_FILE ) {}

bool voicerec_config_t::onSave( JsonDocument& doc ) {
    doc["low_quality"] = low_quality;
    doc["gain"] = gain;

    return( true );
}

bool voicerec_config_t::onLoad( JsonDocument& doc ) {
    low_quality = doc["low_quality"] | false;
    gain = doc["gain"] | VOICEREC_GAIN_DEFAULT;

    if( gain >= VOICEREC_GAIN_COUNT )
        gain = VOICEREC_GAIN_DEFAULT;

    return( true );
}

bool voicerec_config_t::onDefault( void ) {
    low_quality = false;
    gain = VOICEREC_GAIN_DEFAULT;

    return( true );
}
