/****************************************************************************
 *  NetTools_config.cpp
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
#include "config.h"
#include "NetTools_config.h"

#include <string.h>

nettools_config_t::nettools_config_t() : BaseJsonConfig( NETTOOLS_JSON_CONFIG_FILE ) {}

bool nettools_mac_valid( const char *mac ) {
    if ( !mac )
        return( false );
    /* WakeOnLan::stringToArray only accepts these three notations */
    size_t len = strlen( mac );
    return( len == 12 || len == 14 || len == 17 );
}

bool nettools_config_t::onSave( JsonDocument& doc ) {
    for ( int i = 0 ; i < NETTOOLS_TARGETS ; i++ ) {
        doc["targets"][ i ]["name"] = target[ i ].name;
        doc["targets"][ i ]["mac"] = target[ i ].mac;
    }

    return( true );
}

bool nettools_config_t::onLoad( JsonDocument& doc ) {
    if ( doc["targets"].is<JsonArray>() ) {
        for ( int i = 0 ; i < NETTOOLS_TARGETS ; i++ ) {
            strncpy( target[ i ].name, doc["targets"][ i ]["name"] | "", sizeof( target[ i ].name ) - 1 );
            strncpy( target[ i ].mac, doc["targets"][ i ]["mac"] | "", sizeof( target[ i ].mac ) - 1 );
        }
    }
    else if ( doc.containsKey( "mac_address" ) ) {
        /* migrate the single target of the pre 2026 layout, but only if it could ever have been sent */
        const char *mac = doc["mac_address"] | "";
        if ( nettools_mac_valid( mac ) ) {
            strncpy( target[ 0 ].name, "WakePC", sizeof( target[ 0 ].name ) - 1 );
            strncpy( target[ 0 ].mac, mac, sizeof( target[ 0 ].mac ) - 1 );
        }
        else {
            log_w("NetTools: dropping unusable legacy mac_address \"%s\"", mac );
        }
    }

    return( true );
}

bool nettools_config_t::onDefault( void ) {
    return( true );
}
