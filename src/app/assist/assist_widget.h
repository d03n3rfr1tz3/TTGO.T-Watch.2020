/****************************************************************************
 *   Sep 6 12:00:00 2026
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
#ifndef _ASSIST_WIDGET_H
    #define _ASSIST_WIDGET_H

    #include <stdint.h>

    #define ASSIST_WIDGET_PERIOD        100                         /** @brief session driver period in ms */
    #define ASSIST_WIDGET_LABEL         "assist"                    /** @brief idle label, the widget label fits about six chars */

    /**
     * @brief register the standby handler and the icon, called once from assist_app_setup()
     */
    void assist_widget_setup( void );
    /**
     * @brief add or remove the widget icon
     *
     * @param   enable      true adds the icon, false removes it and ends a running session
     *
     * @return  false if no widget slot was free
     */
    bool assist_widget_enable( bool enable );
    /**
     * @brief true while the icon sits on the main tile
     */
    bool assist_widget_active( void );

#endif // _ASSIST_WIDGET_H
