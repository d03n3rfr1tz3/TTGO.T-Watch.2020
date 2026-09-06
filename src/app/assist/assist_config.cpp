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
#include "config.h"
#include "assist_config.h"

static assist_config_t assist_config;
static bool assist_config_dirty = false;

assist_config_t *assist_get_config( void ) {
    return( &assist_config );
}

void assist_config_set_dirty( void ) {
    assist_config_dirty = true;
}

void assist_config_save_dirty( void ) {
    if( !assist_config_dirty )
        return;

    assist_config.save();
    assist_config_dirty = false;
}

assist_config_t::assist_config_t() : BaseJsonConfig( ASSIST_JSON_CONFIG_FILE ) {}

bool assist_config_t::onSave( JsonDocument& doc ) {
    doc["host"] = host;
    doc["port"] = port;
    doc["token"] = token;
    doc["pipeline"] = pipeline;
    doc["gain"] = gain;
    doc["widget"] = widget;

    return( true );
}

bool assist_config_t::onLoad( JsonDocument& doc ) {
    snprintf( host, sizeof( host ), "%s", doc["host"] | "" );
    port = doc["port"] | ASSIST_PORT_DEFAULT;
    snprintf( token, sizeof( token ), "%s", doc["token"] | "" );
    snprintf( pipeline, sizeof( pipeline ), "%s", doc["pipeline"] | "" );
    gain = doc["gain"] | ASSIST_GAIN_DEFAULT;
    widget = doc["widget"] | false;

    if( gain >= ASSIST_GAIN_COUNT )
        gain = ASSIST_GAIN_DEFAULT;

    return( true );
}

bool assist_config_t::onDefault( void ) {
    host[ 0 ] = '\0';
    port = ASSIST_PORT_DEFAULT;
    token[ 0 ] = '\0';
    pipeline[ 0 ] = '\0';
    gain = ASSIST_GAIN_DEFAULT;
    widget = false;

    return( true );
}
