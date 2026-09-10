/****************************************************************************
 *   September 09 12:00:00 2026
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
#include <Arduino.h>

#include "g2048_config.h"

g2048_config_t::g2048_config_t() : BaseJsonConfig( G2048_JSON_CONFIG_FILE ) {
}

bool g2048_config_t::onSave(JsonDocument& doc) {
    doc["best_score"] = best_score;
    doc["score"] = score;
    doc["board"] = board;
    return true;
}

bool g2048_config_t::onLoad(JsonDocument& doc) {
    best_score = doc["best_score"] | 0;
    score = doc["score"] | 0;
    strncpy( board, doc["board"] | "", sizeof( board ) );
    board[ sizeof( board ) - 1 ] = '\0';
    return true;
}

bool g2048_config_t::onDefault( void ) {
    best_score = 0;
    score = 0;
    board[0] = '\0';
    return true;
}
