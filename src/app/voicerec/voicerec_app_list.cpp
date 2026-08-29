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

#include <stdlib.h>
#include <string.h>
#include <FS.h>
#include <SPIFFS.h>

#include "voicerec_app.h"
#include "voicerec_app_list.h"
#include "voicerec_recorder.h"

#include "gui/keyboard.h"
#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "hardware/sound.h"

typedef struct {
    char path[ VOICEREC_PATH_MAX ];                                 /** @brief "/rec/xxx.wav" */
    char date[ VOICEREC_ICRD_SIZE ];                                /** @brief "YYYY-MM-DD HH:MM:SS", empty if unknown */
    uint32_t seconds;                                               /** @brief take length */
} voicerec_entry_t;

static lv_obj_t *voicerec_app_list_tile = NULL;
static lv_obj_t *voicerec_list = NULL;
static lv_obj_t *voicerec_list_hint = NULL;
static lv_obj_t *voicerec_rename_ta = NULL;                         /** @brief hidden, the keyboard writes the new name in here */
static lv_style_t voicerec_list_style;
static lv_task_t *voicerec_app_list_task = NULL;
static bool voicerec_list_visible = false;
static bool voicerec_list_pending = false;

static voicerec_entry_t voicerec_entry_table[ VOICEREC_LIST_MAX ];
static int voicerec_entrys = 0;
static int voicerec_selected = -1;
static lv_obj_t *voicerec_selected_btn = NULL;
static char voicerec_delete_path[ VOICEREC_PATH_MAX ] = "";
static char voicerec_rename_path[ VOICEREC_PATH_MAX ] = "";         /** @brief empty means no rename is pending */
static bool voicerec_long_pressed = false;                          /** @brief a click still follows a long press, this swallows it */

static lv_event_cb_t voicerec_default_msgbox_cb = NULL;

static void voicerec_app_list_activate_cb( void );
static void voicerec_app_list_hibernate_cb( void );
static void voicerec_app_list_lv_task( lv_task_t * task );
static void voicerec_app_list_build( void );
static void voicerec_app_list_select( lv_obj_t *btn, int index );
static void voicerec_app_list_entry_event_cb( lv_obj_t * obj, lv_event_t event );
static void voicerec_app_list_rename_cb( lv_obj_t * obj, lv_event_t event );
static void voicerec_app_list_trash_event_cb( lv_obj_t * obj, lv_event_t event );
static void voicerec_app_list_delete_cb( lv_obj_t * obj, lv_event_t event );
static void voicerec_app_list_exit_event_cb( lv_obj_t * obj, lv_event_t event );
static void voicerec_app_list_left_event_cb( lv_obj_t * obj, lv_event_t event );

void voicerec_app_list_setup( uint32_t tile_num ) {
    for( int i = 0 ; i < VOICEREC_APP_TILES ; i++ ) {
        mainbar_add_tile_activate_cb( voicerec_app_get_app_main_tile_num() + i, voicerec_app_list_activate_cb );
        mainbar_add_tile_hibernate_cb( voicerec_app_get_app_main_tile_num() + i, voicerec_app_list_hibernate_cb );
    }

    voicerec_app_list_tile = mainbar_get_tile_obj( tile_num );

    voicerec_list = lv_list_create( voicerec_app_list_tile, NULL );
    lv_obj_set_size( voicerec_list, lv_disp_get_hor_res( NULL ), lv_disp_get_ver_res( NULL ) - STATUSBAR_HEIGHT - THEME_ICON_SIZE - 2 * THEME_ICON_PADDING );
    lv_style_init( &voicerec_list_style );
    lv_style_set_border_width( &voicerec_list_style, LV_OBJ_PART_MAIN, 0 );
    lv_style_set_radius( &voicerec_list_style, LV_OBJ_PART_MAIN, 0 );
    lv_obj_add_style( voicerec_list, LV_OBJ_PART_MAIN, &voicerec_list_style );
    lv_obj_align( voicerec_list, voicerec_app_list_tile, LV_ALIGN_IN_TOP_MID, 0, STATUSBAR_HEIGHT );

    voicerec_list_hint = wf_add_label( voicerec_app_list_tile, "no recordings" );
    lv_label_set_align( voicerec_list_hint, LV_LABEL_ALIGN_CENTER );
    lv_obj_align( voicerec_list_hint, voicerec_app_list_tile, LV_ALIGN_CENTER, 0, 0 );

    lv_obj_t *exit_btn = wf_add_exit_button( voicerec_app_list_tile, voicerec_app_list_exit_event_cb );
    lv_obj_align( exit_btn, voicerec_app_list_tile, LV_ALIGN_IN_BOTTOM_LEFT, THEME_ICON_PADDING, -THEME_ICON_PADDING );

    lv_obj_t *left_btn = wf_add_left_button( voicerec_app_list_tile, voicerec_app_list_left_event_cb );
    lv_obj_align( left_btn, voicerec_app_list_tile, LV_ALIGN_IN_BOTTOM_MID, 0, -THEME_ICON_PADDING );

    lv_obj_t *trash_btn = wf_add_trash_button( voicerec_app_list_tile, voicerec_app_list_trash_event_cb );
    lv_obj_align( trash_btn, voicerec_app_list_tile, LV_ALIGN_IN_BOTTOM_RIGHT, -THEME_ICON_PADDING, -THEME_ICON_PADDING );

    voicerec_rename_ta = lv_textarea_create( voicerec_app_list_tile, NULL );
    lv_textarea_set_one_line( voicerec_rename_ta, true );
    lv_obj_set_hidden( voicerec_rename_ta, true );
    lv_obj_set_event_cb( voicerec_rename_ta, voicerec_app_list_rename_cb );

    lv_tileview_add_element( voicerec_app_list_tile, voicerec_list_hint );
    lv_tileview_add_element( voicerec_app_list_tile, exit_btn );
    lv_tileview_add_element( voicerec_app_list_tile, left_btn );
    lv_tileview_add_element( voicerec_app_list_tile, trash_btn );

    voicerec_app_list_task = lv_task_create( voicerec_app_list_lv_task, VOICEREC_LIST_PERIOD, LV_TASK_PRIO_OFF, NULL );
}

static void voicerec_app_list_activate_cb( void ) {
    voicerec_list_visible = false;
    lv_task_set_prio( voicerec_app_list_task, LV_TASK_PRIO_MID );
}

static void voicerec_app_list_hibernate_cb( void ) {
    lv_task_set_prio( voicerec_app_list_task, LV_TASK_PRIO_OFF );
    sound_stop_spiffs_wav();
    voicerec_rename_path[ 0 ] = '\0';
    keyboard_hide();
}

static bool voicerec_app_list_is_visible( void ) {
    lv_area_t area;

    lv_obj_get_coords( voicerec_app_list_tile, &area );

    return( abs( area.x1 ) + abs( area.y1 ) < lv_disp_get_hor_res( NULL ) / 2 );
}

static void voicerec_app_list_lv_task( lv_task_t * task ) {
    bool visible = voicerec_app_list_is_visible();

    if( visible != voicerec_list_visible ) {
        voicerec_list_visible = visible;

        if( visible )
            voicerec_app_list_build();
        else
            sound_stop_spiffs_wav();

        return;
    }
    
    if( visible && sound_spiffs_wav_is_running() )
        lv_disp_trig_activity( NULL );

    if( visible && voicerec_list_pending && voicerec_recorder_get_state() == VOICEREC_IDLE )
        voicerec_app_list_build();
}

/**
 * @brief rebuild the date from an automatic filename, "YYMMDD-HHMMSS"
 *
 * @return true if the name matches the pattern
 */
static bool voicerec_app_list_name_date( const char *name, char *date, size_t len ) {
    int year, month, day, hour, minute, second;

    if( sscanf( name, "%2d%2d%2d-%2d%2d%2d", &year, &month, &day, &hour, &minute, &second ) != 6 )
        return( false );

    snprintf( date, len, "%04d-%02d-%02d %02d:%02d:%02d", 2000 + year, month, day, hour, minute, second );
    return( true );
}

static uint32_t voicerec_app_list_get_u32( const uint8_t *header, int offset ) {
    return( ( uint32_t )header[ offset ] | ( ( uint32_t )header[ offset + 1 ] << 8 ) | ( ( uint32_t )header[ offset + 2 ] << 16 ) | ( ( uint32_t )header[ offset + 3 ] << 24 ) );
}

/**
 * @brief strip folder and extension from an entry path
 */
static void voicerec_app_list_entry_name( const char *path, char *name, size_t len ) {
    const char *slash = strrchr( path, '/' );

    snprintf( name, len, "%s", slash ? slash + 1 : path );

    char *dot = strstr( name, ".wav" );
    if( dot )
        *dot = '\0';
}

static void voicerec_app_list_entry_text( voicerec_entry_t *entry, char *text, size_t len ) {
    char name[ VOICEREC_NAME_MAX + 1 ] = "";
    char stamp[ VOICEREC_ICRD_SIZE ] = "";
    int year, month, day, hour, minute, second;

    voicerec_app_list_entry_name( entry->path, name, sizeof( name ) );

    if( voicerec_app_list_name_date( name, stamp, sizeof( stamp ) ) )
        snprintf( name, sizeof( name ), "%ds", ( int )entry->seconds );

    if( sscanf( entry->date, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second ) == 6 )
        snprintf( text, len, "%02d.%02d. %02d:%02d %s", day, month, hour, minute, name );
    else
        snprintf( text, len, "%s", name );
}

static void voicerec_app_list_read_entries( void ) {
    voicerec_entrys = 0;

    fs::File root = SPIFFS.open( VOICEREC_DIR );
    if( !root )
        return;

    fs::File file = root.openNextFile();
    while( file && voicerec_entrys < VOICEREC_LIST_MAX ) {
        const char *path = file.name();
        uint8_t header[ VOICEREC_HEADER_SIZE ];

        if( strstr( path, ".wav" ) && file.size() > VOICEREC_HEADER_SIZE &&
            file.read( header, sizeof( header ) ) == sizeof( header ) ) {

            voicerec_entry_t *entry = &voicerec_entry_table[ voicerec_entrys ];
            const char *slash = strrchr( path, '/' );
            uint32_t byte_rate = voicerec_app_list_get_u32( header, 28 );
            uint32_t data_offset = VOICEREC_HEADER_SIZE;

            snprintf( entry->path, sizeof( entry->path ), "%s", path );
            entry->date[ 0 ] = '\0';

            if( !memcmp( header + 36, "LIST", 4 ) ) {
                strncpy( entry->date, ( const char * )header + VOICEREC_ICRD_OFFSET, VOICEREC_ICRD_SIZE - 1 );
                entry->date[ VOICEREC_ICRD_SIZE - 1 ] = '\0';
            }
            else {
                data_offset = 44;
                voicerec_app_list_name_date( slash ? slash + 1 : path, entry->date, sizeof( entry->date ) );
            }

            entry->seconds = byte_rate ? ( file.size() - data_offset ) / byte_rate : 0;
            voicerec_entrys++;
        }

        file = root.openNextFile();
    }
}

static void voicerec_app_list_build( void ) {
    voicerec_list_pending = voicerec_recorder_get_state() != VOICEREC_IDLE;
    if( voicerec_list_pending )
        return;

    voicerec_app_list_select( NULL, -1 );
    while( lv_list_remove( voicerec_list, 0 ) );

    voicerec_app_list_read_entries();
    for( int i = 1 ; i < voicerec_entrys ; i++ ) {
        voicerec_entry_t entry = voicerec_entry_table[ i ];
        int j = i - 1;
        while( j >= 0 && strcmp( voicerec_entry_table[ j ].date, entry.date ) < 0 ) {
            voicerec_entry_table[ j + 1 ] = voicerec_entry_table[ j ];
            j--;
        }
        voicerec_entry_table[ j + 1 ] = entry;
    }

    for( int i = 0 ; i < voicerec_entrys ; i++ ) {
        char text[ VOICEREC_PATH_MAX + VOICEREC_ICRD_SIZE ];

        voicerec_app_list_entry_text( &voicerec_entry_table[ i ], text, sizeof( text ) );

        lv_obj_t *list_btn = lv_list_add_btn( voicerec_list, NULL, text );
        lv_obj_set_user_data( list_btn, ( lv_obj_user_data_t )( intptr_t )i );
        lv_obj_set_event_cb( list_btn, voicerec_app_list_entry_event_cb );
        lv_label_set_long_mode( lv_list_get_btn_label( list_btn ), LV_LABEL_LONG_DOT );
    }

    lv_obj_set_hidden( voicerec_list_hint, voicerec_entrys > 0 );
    lv_obj_set_hidden( voicerec_list, voicerec_entrys == 0 );
}

static void voicerec_app_list_select( lv_obj_t *btn, int index ) {
    if( voicerec_selected_btn )
        lv_btn_set_state( voicerec_selected_btn, LV_BTN_STATE_RELEASED );

    voicerec_selected_btn = btn;
    voicerec_selected = index;

    if( btn )
        lv_btn_set_state( btn, LV_BTN_STATE_CHECKED_RELEASED );
}

static void voicerec_app_list_entry_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       {
                                            if( voicerec_long_pressed )
                                                return;
                                            if( voicerec_recorder_get_state() != VOICEREC_IDLE )
                                                return;

                                            int index = ( int )( intptr_t )lv_obj_get_user_data( obj );
                                            if( index < 0 || index >= voicerec_entrys )
                                                return;

                                            if( index == voicerec_selected && sound_spiffs_wav_is_running() ) {
                                                sound_stop_spiffs_wav();
                                                return;
                                            }

                                            sound_stop_spiffs_wav();
                                            sound_play_spiffs_wav( voicerec_entry_table[ index ].path, SOUND_TYPE_FOREGROUND );
                                            voicerec_app_list_select( obj, index );
                                            break;
                                        }
        case( LV_EVENT_LONG_PRESSED ):  {
                                            char name[ VOICEREC_NAME_MAX + 1 ] = "";

                                            if( voicerec_recorder_get_state() != VOICEREC_IDLE )
                                                return;

                                            int index = ( int )( intptr_t )lv_obj_get_user_data( obj );
                                            if( index < 0 || index >= voicerec_entrys )
                                                return;

                                            voicerec_long_pressed = true;
                                            voicerec_rename_path[ 0 ] = '\0';

                                            voicerec_app_list_entry_name( voicerec_entry_table[ index ].path, name, sizeof( name ) );
                                            lv_textarea_set_text( voicerec_rename_ta, name );

                                            snprintf( voicerec_rename_path, sizeof( voicerec_rename_path ), "%s", voicerec_entry_table[ index ].path );
                                            voicerec_app_list_select( obj, index );
                                            keyboard_set_textarea( voicerec_rename_ta );
                                            break;
                                        }
        case( LV_EVENT_RELEASED ):      voicerec_long_pressed = false;
                                        break;
    }
}

static void voicerec_app_list_rename_cb( lv_obj_t * obj, lv_event_t event ) {
    static const char *btns[] = { "Ok", "" };
    char name[ VOICEREC_NAME_MAX + 1 ] = "";
    char path[ VOICEREC_PATH_MAX ] = "";
    size_t out = 0;

    if( event != LV_EVENT_VALUE_CHANGED )
        return;
    if( !voicerec_rename_path[ 0 ] || voicerec_recorder_get_state() != VOICEREC_IDLE )
        return;

    const char *text = lv_textarea_get_text( obj );
    while( *text == ' ' )
        text++;

    for( ; *text && out < VOICEREC_NAME_MAX ; text++ )
        if( *text != '/' && ( uint8_t )*text >= 0x20 )
            name[ out++ ] = *text;

    while( out && name[ out - 1 ] == ' ' )
        out--;
    name[ out ] = '\0';

    if( !out )
        return;

    snprintf( path, sizeof( path ), "%s/%s.wav", VOICEREC_DIR, name );
    if( !strcmp( path, voicerec_rename_path ) ) {
        voicerec_rename_path[ 0 ] = '\0';
        return;
    }

    if( SPIFFS.exists( path ) ) {
        lv_obj_t *mbox = lv_msgbox_create( lv_scr_act(), NULL );
        lv_msgbox_set_text( mbox, "Name already taken" );
        lv_msgbox_add_btns( mbox, btns );
        lv_obj_set_width( mbox, 200 );
        lv_obj_align( mbox, NULL, LV_ALIGN_CENTER, 0, 0 );
        voicerec_rename_path[ 0 ] = '\0';
        return;
    }

    sound_stop_spiffs_wav();

    if( !SPIFFS.rename( voicerec_rename_path, path ) )
        log_e("voicerec: rename to %s failed", path );

    voicerec_rename_path[ 0 ] = '\0';
    voicerec_app_list_build();
}

static void voicerec_app_list_trash_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       {
                                            static const char *btns[] = { "Yes", "No", "" };

                                            if( voicerec_recorder_get_state() != VOICEREC_IDLE )
                                                return;
                                            if( voicerec_selected < 0 || voicerec_selected >= voicerec_entrys )
                                                return;

                                            snprintf( voicerec_delete_path, sizeof( voicerec_delete_path ), "%s", voicerec_entry_table[ voicerec_selected ].path );

                                            lv_obj_t *mbox = lv_msgbox_create( lv_scr_act(), NULL );
                                            lv_msgbox_set_text( mbox, "Delete recording?" );
                                            lv_msgbox_add_btns( mbox, btns );
                                            lv_obj_set_width( mbox, 200 );
                                            voicerec_default_msgbox_cb = lv_obj_get_event_cb( mbox );
                                            lv_obj_set_event_cb( mbox, voicerec_app_list_delete_cb );
                                            lv_obj_align( mbox, NULL, LV_ALIGN_CENTER, 0, 0 );
                                            break;
                                        }
    }
}

static void voicerec_app_list_delete_cb( lv_obj_t * obj, lv_event_t event ) {
    if( event == LV_EVENT_VALUE_CHANGED ) {
        if( !strcmp( lv_msgbox_get_active_btn_text( obj ), "Yes" ) ) {
            sound_stop_spiffs_wav();
            SPIFFS.remove( voicerec_delete_path );
            voicerec_app_list_build();
        }
    }

    voicerec_default_msgbox_cb( obj, event );
}

static void voicerec_app_list_left_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       voicerec_app_slide( VOICEREC_APP_MAIN_TILE );
                                        break;
    }
}

static void voicerec_app_list_exit_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_jump_back();
                                        break;
    }
}
