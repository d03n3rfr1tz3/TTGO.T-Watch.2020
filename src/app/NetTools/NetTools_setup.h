/****************************************************************************
 *  NetTools_setup.h
 *  Copyright  2020  David Stewart / NorthernDIY
 *  Email: genericsoftwaredeveloper@gmail.com
 *
 *  Requires Libraries:
 *      WakeOnLan by a7md0      https://github.com/a7md0/WakeOnLan
 *
 *  Based on the work of Dirk Brosswick,  sharandac / My-TTGO-Watch  Example_App"
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
#ifndef _NETTOOLS_SETUP_H
    #define _NETTOOLS_SETUP_H

    void NetTools_setup_setup( uint32_t tile_num );

    /**
     * @brief put a sniffed host into the first unused wake on lan slot
     *
     * @param   mac     mac address string, must pass nettools_mac_valid()
     * @param   host    host name or NULL, used as the button label
     *
     * @return  true if a free slot was found
     */
    bool NetTools_setup_add_target( const char *mac, const char *host );

#endif // _NETTOOLS_SETUP_H
