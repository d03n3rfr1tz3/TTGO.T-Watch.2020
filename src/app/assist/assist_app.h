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
#ifndef _ASSIST_APP_H
    #define _ASSIST_APP_H

    #include "gui/widget_factory.h"

    #define ASSIST_SETUP_TILES          2                           /** @brief setup and pairing, horizontally adjacent */

    void assist_app_setup( void );
    uint32_t assist_app_get_app_main_tile_num( void );
    uint32_t assist_app_get_setup_tile_num( void );
    uint32_t assist_app_get_pair_tile_num( void );

#endif // _ASSIST_APP_H
