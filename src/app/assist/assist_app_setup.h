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
#ifndef _ASSIST_APP_SETUP_H
    #define _ASSIST_APP_SETUP_H

    #include "gui/widget_factory.h"

    #define ASSIST_SETUP_CONT_HEIGHT    37                          /** @brief one setup row, three of them plus the pipeline list fill the tile */
    #define ASSIST_SETUP_LIST_HEIGHT    120                         /** @brief the open pipeline list scrolls instead of running off the tile */
    #define ASSIST_SETUP_PIPELINE_PREFERRED "preferred"             /** @brief first list entry, it stores an empty pipeline and lets home assistant pick */
    #define ASSIST_SETUP_PERIOD         100                         /** @brief gui refresh period in ms */

    void assist_app_setup_setup( uint32_t tile_num );
    /**
     * @brief keyboard, store, save and disconnect, idempotent
     *
     * both the visibility probe and the hibernate callback call it, swiping fires
     * no callback at all and mainbar_jump_back() resolves the callback by the
     * current tile, so neither alone would catch every way out
     */
    void assist_app_setup_leave( void );
    /**
     * @brief switch the tile task, off means the tile owns nothing
     */
    void assist_app_setup_enable( bool enable );

#endif // _ASSIST_APP_SETUP_H
