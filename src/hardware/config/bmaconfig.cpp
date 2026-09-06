/****************************************************************************
 *   Tu May 22 21:23:51 2020
 *   Copyright  2020  Dirk Brosswick
 *   Email: dirk.brosswick@googlemail.com
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
#include "bmaconfig.h"

bma_config_t::bma_config_t() : BaseJsonConfig(BMA_JSON_COFIG_FILE) {
    for (int i = 0 ; i < BMA_CONFIG_NUM ; i++)
        enable[i]=true;

    axis[ BMA_AXIS_SWAP_XY ] = BMA_SWAP_XY_DEFAULT;
    axis[ BMA_AXIS_INVERT_X ] = BMA_INVERT_X_DEFAULT;
    axis[ BMA_AXIS_INVERT_Y ] = BMA_INVERT_Y_DEFAULT;
}

bool bma_config_t::onSave(JsonDocument& doc) {
    doc["stepcounter"] = enable[ BMA_STEPCOUNTER ];
    doc["doubleclick"] = enable[ BMA_DOUBLECLICK ];
    doc["tilt"] = enable[ BMA_TILT ];
    doc["daily_stepcounter"] = enable[ BMA_DAILY_STEPCOUNTER ];
    doc["swap_xy"] = axis[ BMA_AXIS_SWAP_XY ];
    doc["invert_x"] = axis[ BMA_AXIS_INVERT_X ];
    doc["invert_y"] = axis[ BMA_AXIS_INVERT_Y ];

    return true;
}

bool bma_config_t::onLoad(JsonDocument& doc) {
    enable[ BMA_STEPCOUNTER ] = doc["stepcounter"] | true;
    enable[ BMA_DOUBLECLICK ] = doc["doubleclick"] | true;
    enable[ BMA_TILT ] = doc["tilt"] | false;
    enable[ BMA_DAILY_STEPCOUNTER ] = doc["daily_stepcounter"] | false;
    axis[ BMA_AXIS_SWAP_XY ] = doc["swap_xy"] | BMA_SWAP_XY_DEFAULT;
    axis[ BMA_AXIS_INVERT_X ] = doc["invert_x"] | BMA_INVERT_X_DEFAULT;
    axis[ BMA_AXIS_INVERT_Y ] = doc["invert_y"] | BMA_INVERT_Y_DEFAULT;

    return true;
}

bool bma_config_t::onDefault( void ) {
    return true;
}