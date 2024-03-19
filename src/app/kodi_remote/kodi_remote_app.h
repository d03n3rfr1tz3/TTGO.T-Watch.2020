/****************************************************************************
 *   June 14 02:01:00 2021
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
#ifndef _KODI_REMOTE_H
    #define _KODI_REMOTE_H

    #include "gui/icon.h"
    #include "kodi_remote_config.h"

    typedef struct {
        volatile bool changed = false;
        volatile bool success = false;
        volatile uint32_t kodi_remote_id = 0;
        volatile int16_t kodi_remote_videoplayer_id = 0;
        volatile int16_t kodi_remote_audioplayer_id = 0;
        volatile int16_t kodi_remote_pictureplayer_id = 0;
        volatile char* artist = nullptr;
        volatile char* title = nullptr;
    } kodi_remote_result_t;

    void kodi_remote_app_setup( void );
    uint32_t kodi_remote_app_get_app_setup_tile_num( void );
    uint32_t kodi_remote_app_get_app_main_tile_num( void );

    void kodi_remote_app_set_indicator(icon_indicator_t indicator);
    void kodi_remote_app_hide_indicator();

    kodi_remote_config_t *kodi_remote_get_config( void );
    void kodi_remote_save_config( void );
    void kodi_remote_load_config( void );

#endif // _KODI_REMOTE_H