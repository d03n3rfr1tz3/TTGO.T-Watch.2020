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
#ifndef _ASSIST_APP_MAIN_H
    #define _ASSIST_APP_MAIN_H

    #include "gui/widget_factory.h"

    #define ASSIST_APP_MAIN_PERIOD      100                         /** @brief gui refresh period in ms */
    #define ASSIST_SPL_FLOOR            30.0f                       /** @brief lower end of the level bar in dB SPL, same scale as the soundmeter */
    #define ASSIST_SPL_CEIL             110.0f                      /** @brief upper end of the level bar in dB SPL */
    #define ASSIST_BAR_HEIGHT           8                           /** @brief level bar height, the answer page needs the rest */
    #define ASSIST_TIGHT_PADDING        ( THEME_PADDING / 2 )       /** @brief the answer page gets every pixel the layout can spare */
    #define ASSIST_ROW_PADDING          4                           /** @brief vertical padding of the two setting rows, the theme would spend 14 */
    #define ASSIST_SWITCH_WIDTH         40
    #define ASSIST_SWITCH_HEIGHT        20

    void assist_app_main_setup( uint32_t tile_num );
    /**
     * @brief speak the answer instead of showing it only, follows the system sound setting
     */
    bool assist_app_main_get_speak( void );

#endif // _ASSIST_APP_MAIN_H
