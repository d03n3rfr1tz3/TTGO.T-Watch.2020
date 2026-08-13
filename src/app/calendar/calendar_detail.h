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
#ifndef _CALENDAR_DETAIL_H
    #define _CALENDAR_DETAIL_H

    /**
     * @brief setup calendar detail tile, shows a gadgetbridge event read only
     */
    void calendar_detail_setup( void );
    /**
     * @brief get calendar detail tile number
     *
     * @return  calendar detail tile number
     */
    uint32_t calendar_detail_get_tile( void );
    /**
     * @brief show an event from the ble store
     *
     * @param   slot    slot number from blecalendar_get_day_events()
     */
    void calendar_detail_show( int slot );

#endif // _CALENDAR_DETAIL_H
