/****************************************************************************
 *  NetTools_sniff.h
 *  Copyright  2026  Dirk Sarodnick
 *
 *  Passive broadcast listener, see NetTools_sniff.cpp
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
#ifndef _NETTOOLS_SNIFF_H
    #define _NETTOOLS_SNIFF_H

    #include "NetTools_config.h"

    #define NETTOOLS_SNIFF_ENTRIES  32

    typedef enum {
        SNIFF_WOL = 0,
        SNIFF_NETBIOS,
        SNIFF_DHCP,
        SNIFF_ARP
    } sniff_kind_t;

    typedef struct {
        sniff_kind_t    kind;
        char            mac[ NETTOOLS_MAC_LEN ];        /** @brief "" if unknown */
        char            host[ NETTOOLS_NAME_LEN ];      /** @brief "" if unknown */
        uint32_t        ip;                             /** @brief network byte order, 0 if unknown */
        uint32_t        last_seen;
        uint16_t        count;
    } nettools_sniff_entry_t;

    void NetTools_sniff_setup( uint32_t tile_num );
    void NetTools_sniff_arm( void );

#endif // _NETTOOLS_SNIFF_H
