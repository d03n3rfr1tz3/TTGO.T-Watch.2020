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
#ifndef _VOICEREC_APP_LIST_H
    #define _VOICEREC_APP_LIST_H

    #include "gui/widget_factory.h"

    #define VOICEREC_LIST_MAX           32                          /** @brief max entries shown */
    #define VOICEREC_LIST_PERIOD        200                         /** @brief tile visibility poll period in ms */

    void voicerec_app_list_setup( uint32_t tile_num );

#endif // _VOICEREC_APP_LIST_H
