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
#ifndef _PING_APP_MAIN_H
    #define _PING_APP_MAIN_H

    #define PING_TARGET_LEN         64
    #define PING_RESULT_LEN         512
    #define PING_LINE_LEN           64

    #define PING_COUNT              4
    #define PING_SIZE               32
    #define PING_TIMEOUT            1
    #define PING_INTERVAL           1

    #define PING_TRACE_MAX_HOP      20
    #define PING_TRACE_TIMEOUT      1
    #define PING_TRACE_ID           0x5754

    #define PING_PORT_TIMEOUT       1000

    typedef enum {
        PING_TOOL_PING = 0,
        PING_TOOL_TRACE,
        PING_TOOL_PORT
    } ping_tool_t;

    void ping_app_main_setup( uint32_t tile_num );

#endif // _PING_APP_MAIN_H
