/****************************************************************************
 *   Sep 04 20:00:00 2026
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
#include "gui/keyboard.h"
#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

#include "gui/mainbar/note_tile/note_config.h"
#include "gui/mainbar/note_tile/note_edit.h"
#include "gui/mainbar/note_tile/note_tile.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#else
    #include <Arduino.h>
#endif

static bool note_edit_init = false;
static uint32_t note_edit_tile_num;
static lv_obj_t *note_edit_textarea = NULL;
static int32_t note_edit_entry = -1;                /** @brief note under edit, below zero is a new one */

static void note_edit_exit_event_cb( lv_obj_t *obj, lv_event_t event );
static void note_edit_trash_event_cb( lv_obj_t *obj, lv_event_t event );
static void note_edit_textarea_event_cb( lv_obj_t *obj, lv_event_t event );
static void note_edit_hibernate_cb( void );

void note_edit_setup( void ) {
    if ( note_edit_init )
        return;

    note_edit_tile_num = mainbar_add_app_tile( 1, 1, "note edit" );

    lv_obj_t *tile = mainbar_get_tile_obj( note_edit_tile_num );

    lv_obj_t *header = wf_add_settings_header( tile, "note", note_edit_exit_event_cb );
    lv_obj_align( header, tile, LV_ALIGN_IN_TOP_LEFT, 0, STATUSBAR_HEIGHT + THEME_ICON_PADDING );
    wf_add_trash_button( header, note_edit_trash_event_cb, SYSTEM_ICON_STYLE );

    note_edit_textarea = lv_textarea_create( tile, NULL );
    lv_textarea_set_text( note_edit_textarea, "" );
    lv_textarea_set_one_line( note_edit_textarea, false );
    lv_textarea_set_cursor_hidden( note_edit_textarea, true );
    lv_textarea_set_max_length( note_edit_textarea, NOTE_TEXT_MAX - 1 );
    lv_obj_set_size( note_edit_textarea, lv_disp_get_hor_res( NULL ) - THEME_ICON_PADDING * 2,
                                         lv_disp_get_ver_res( NULL ) / 2 );
    lv_obj_align( note_edit_textarea, header, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_ICON_PADDING );
    lv_obj_set_event_cb( note_edit_textarea, note_edit_textarea_event_cb );

    mainbar_add_tile_hibernate_cb( note_edit_tile_num, note_edit_hibernate_cb );

    note_edit_init = true;
}

void note_edit_open( int32_t entry ) {
    if ( !note_edit_init )
        return;

    note_entry_t *note = entry >= 0 ? note_config_get( entry ) : NULL;

    note_edit_entry = note ? entry : -1;
    lv_textarea_set_text( note_edit_textarea, note ? note->text : "" );

    mainbar_jump_to_tilenumber( note_edit_tile_num, LV_ANIM_ON );

    if ( !note )
        keyboard_set_textarea( note_edit_textarea );
}

/**
 * @brief an emptied text note is dropped, an audio note keeps its name
 */
static void note_edit_save( void ) {
    const char *text = lv_textarea_get_text( note_edit_textarea );
    note_entry_t *note = note_edit_entry >= 0 ? note_config_get( note_edit_entry ) : NULL;

    if ( note ) {
        if ( text[ 0 ] != '\0' )
            note_config_set_text( note_edit_entry, text );
        else if ( note->kind == NOTE_KIND_TEXT )
            note_config_remove( note_edit_entry );
    }
    else if ( text[ 0 ] != '\0' )
        note_config_add_text( text );
}

static void note_edit_leave( void ) {
    keyboard_hide();
    note_edit_entry = -1;
    note_tile_refresh();
    mainbar_jump_back();
}

static void note_edit_exit_event_cb( lv_obj_t *obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):   note_edit_save();
                                    note_edit_leave();
                                    break;
    }
}

static void note_edit_trash_event_cb( lv_obj_t *obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):   if ( note_edit_entry >= 0 )
                                        note_config_remove( note_edit_entry );
                                    note_edit_leave();
                                    break;
    }
}

static void note_edit_textarea_event_cb( lv_obj_t *obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):   keyboard_set_textarea( obj );
                                    break;
    }
}

static void note_edit_hibernate_cb( void ) {
    keyboard_hide();
}
