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
#include "config.h"

#include "voicerec_app.h"
#include "voicerec_app_main.h"
#include "voicerec_app_list.h"
#include "voicerec_recorder.h"

#include "gui/mainbar/mainbar.h"
#include "gui/mainbar/note_tile/note_tile.h"
#include "gui/app.h"
#include "hardware/micctl.h"

uint32_t voicerec_app_main_tile_num;

icon_t *voicerec_app = NULL;

LV_IMG_DECLARE(voicerec_app_64px);
#if defined( BIG_THEME )
    LV_IMG_DECLARE(voicerec_mic_96px);
    #define voicerec_mic_img    voicerec_mic_96px
#else
    LV_IMG_DECLARE(voicerec_mic_32px);
    #define voicerec_mic_img    voicerec_mic_32px
#endif

static void enter_voicerec_app_event_cb( lv_obj_t * obj, lv_event_t event );
static void enter_voicerec_from_note_event_cb( lv_obj_t * obj, lv_event_t event );

static int registed = app_autocall_function( &voicerec_app_setup, APP_PRIO( APP_GROUP_AUDIO, 2 ) );          /** @brief app autocall function */

void voicerec_app_setup( void ) {
    /*
     * check if app already registered for autocall
     */
    if( !registed ) {
        return;
    }
    /*
     * without a microphone there is nothing to record
     */
    if( !micctl_get_available() ) {
        return;
    }

    voicerec_app_main_tile_num = mainbar_add_app_tile( VOICEREC_APP_TILES, 1, "voicerec app" );
    voicerec_app = app_register( "voice\nrec", &voicerec_app_64px, enter_voicerec_app_event_cb );

    voicerec_recorder_setup();
    voicerec_app_main_setup( voicerec_app_main_tile_num + VOICEREC_APP_MAIN_TILE );
    voicerec_app_list_setup( voicerec_app_main_tile_num + VOICEREC_APP_LIST_TILE );

    note_tile_register_source( "voice", &voicerec_mic_img, enter_voicerec_from_note_event_cb );
}

uint32_t voicerec_app_get_app_main_tile_num( void ) {
    return( voicerec_app_main_tile_num );
}

void voicerec_app_slide( int index ) {
    if( index < 0 || index >= VOICEREC_APP_TILES )
        return;

    mainbar_slide_to_tilenumber( voicerec_app_main_tile_num + index, LV_ANIM_ON );
}

static void enter_voicerec_app_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       app_hide_indicator( voicerec_app );
                                        mainbar_jump_to_tilenumber( voicerec_app_main_tile_num, LV_ANIM_OFF, true );
                                        break;
    }
}

static void enter_voicerec_from_note_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       voicerec_app_main_set_from_note();
                                        mainbar_jump_to_tilenumber( voicerec_app_main_tile_num, LV_ANIM_OFF, true );
                                        break;
    }
}
