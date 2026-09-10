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

#pragma once

#include "utils/basejsonconfig.h"

#define G2048_JSON_CONFIG_FILE "/g2048.json"

class g2048_config_t : public BaseJsonConfig {
    public:
    g2048_config_t();
    uint32_t best_score = 0;                /** @brief highest score ever reached */
    uint32_t score = 0;                     /** @brief score of the stored game */
    char board[17] = "";                    /** @brief 16 exponents, '0' is empty, 'a' is 10 */

    protected:
    virtual bool onLoad(JsonDocument& document);
    virtual bool onSave(JsonDocument& document);
    virtual bool onDefault( void );
    virtual size_t getJsonBufferSize() { return 512; }
};
