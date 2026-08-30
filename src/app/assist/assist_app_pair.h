/****************************************************************************
 *   Aug 30 12:00:00 2026
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
#ifndef _ASSIST_APP_PAIR_H
    #define _ASSIST_APP_PAIR_H

    #include "gui/widget_factory.h"

    #define ASSIST_PAIR_QR_OFFSET       12                          /** @brief top margin of the canvas, the same below it up to the label */

    void assist_app_pair_setup( uint32_t tile_num );
    /**
     * @brief switch the tile task, the setup tile owns it, both tiles are entered through it
     */
    void assist_app_pair_enable( bool enable );

#endif // _ASSIST_APP_PAIR_H
