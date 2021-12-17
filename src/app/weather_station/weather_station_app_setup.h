/****************************************************************************
 *   June 16 23:15:00 2021
 *   Copyright  2021  Dirk Sarodnick
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
#ifndef _WEATHER_STATION_APP_SETUP_H
    #define _WEATHER_STATION_APP_SETUP_H

    #include <TTGO.h>

    #define WEATHER_STATION_APP_INFO_LOG        log_i

    void weather_station_app_setup_setup( uint32_t tile_num );

#endif // _WEATHER_STATION_APP_SETUP_H