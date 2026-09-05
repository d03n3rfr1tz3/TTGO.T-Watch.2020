/****************************************************************************
 *   Tu May 22 21:23:51 2020
 *   Copyright  2020  Dirk Brosswick
 *   Email: dirk.brosswick@googlemail.com
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
#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

#include "gui/mainbar/note_tile/note_tile.h"
#include "gui/mainbar/note_tile/note_config.h"
#include "gui/mainbar/note_tile/note_edit.h"
#include "gui/mainbar/note_tile/note_style.h"
#include "gui/mainbar/setup_tile/setup_tile.h"
#include "hardware/sound.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#else
    #include <Arduino.h>
    #include <FS.h>
    #include <SPIFFS.h>
#endif

#define NOTE_CELL_MAX           8                                       /** @brief cap for the static cell table */
#define NOTE_GRID_MAX           4                                       /** @brief most columns or rows to try */
#define NOTE_TAPE_OVERHANG      THEME_PADDING                           /** @brief how far the tape sticks onto the ground */
#define NOTE_TEXT_Y             ( THEME_PADDING + 2 )                   /** @brief keeps the glue zone free */
#define NOTE_CHECK_EXT          ( THEME_PADDING * 2 )                   /** @brief grows the check to button size without taking the space */
#define NOTE_CHECK_OPA          LV_OPA_70                               /** @brief an open note only hints at its check */
#define NOTE_TASK_PERIOD        1000
#define NOTE_TASK_IDLE_PERIOD   2000

/**
 * @brief one post-it, the tape strip is a sibling because lvgl 7 clips children hard
 */
typedef struct {
    lv_obj_t *paper;
    lv_obj_t *tape;
    lv_obj_t *label;
    lv_obj_t *play_btn;
    lv_obj_t *check;
} note_slot_t;

/**
 * @brief an additional create button, registered from an app
 */
typedef struct {
    const char *name;
    lv_obj_t *btn;
} note_source_t;

static bool notetile_init = false;

static lv_obj_t *note_cont = NULL;
static lv_obj_t *notelabel = NULL;
static lv_obj_t *note_footer = NULL;
static lv_obj_t *note_add_btn = NULL;
uint32_t note_tile_num;

static lv_style_t *style;
static lv_style_t notestyle;

static note_slot_t note_slot_table[ NOTE_CELL_MAX ];
static note_source_t note_source_table[ NOTE_SOURCE_MAX ];
static int32_t note_sources = 0;
static int32_t note_tile_visible = 0;                                   /** @brief cells for notes, computed from the display */
static int32_t note_tile_playing = -1;
static lv_coord_t note_cell_w = 0;
static lv_coord_t note_cell_h = 0;
static lv_coord_t note_footer_x = 0;
static lv_coord_t note_footer_y = 0;
static lv_task_t *note_tile_task = NULL;

LV_FONT_DECLARE(Ubuntu_72px);
LV_IMG_DECLARE(note_tape_48px);

static bool note_tile_button_event_cb( EventBits_t event, void *arg );
static void note_tile_paper_event_cb( lv_obj_t *obj, lv_event_t event );
static void note_tile_check_event_cb( lv_obj_t *obj, lv_event_t event );
static void note_tile_play_event_cb( lv_obj_t *obj, lv_event_t event );
static void note_tile_add_event_cb( lv_obj_t *obj, lv_event_t event );
static void note_tile_activate_cb( void );
static void note_tile_lv_task( lv_task_t *task );

/**
 * @brief the tile is swiped in, no callback fires for that, so probe the position
 */
static bool note_tile_is_visible( void ) {
    lv_area_t area;

    lv_obj_get_coords( note_cont, &area );

    return( abs( area.x1 ) + abs( area.y1 ) < lv_disp_get_hor_res( NULL ) / 2 );
}

static bool note_tile_file_exists( const char *path ) {
#ifdef NATIVE_64BIT
    return( true );
#else
    return( SPIFFS.exists( path ) );
#endif
}

/**
 * @brief one line out of a note, line breaks would waste the preview
 */
static void note_tile_preview( char *dest, size_t len, const char *src ) {
    size_t i = 0;

    while ( src[ i ] && i < len - 1 ) {
        dest[ i ] = ( src[ i ] == '\n' || src[ i ] == '\r' || src[ i ] == '\t' ) ? ' ' : src[ i ];
        i++;
    }

    dest[ i ] = '\0';
}

static void note_tile_stop_playback( void ) {
    if ( note_tile_playing < 0 )
        return;

    sound_stop_spiffs_wav();
    note_tile_playing = -1;
}

/**
 * @brief   center the footer buttons in their cell
 *
 * a pretty layout stacks its rows from the top, so the container has to be as tall as its
 * content and gets centered by hand. a source registered later makes it grow, hence the helper.
 */
static void note_tile_align_footer( void ) {
    lv_obj_align( note_footer, note_cont, LV_ALIGN_IN_TOP_LEFT,
                  note_footer_x,
                  note_footer_y + ( note_cell_h - lv_obj_get_height( note_footer ) ) / 2 );
}

/**
 * @brief   free a cell for a new note
 *
 * checking a note off gives its place back at once, the grace period is there to take it back,
 * not to block the plus button.
 */
static bool note_tile_make_room( void ) {
    while ( note_config_get_entrys() >= note_tile_visible ) {
        if ( !note_config_drop_oldest_done() )
            return( false );
    }

    return( true );
}

/**
 * @brief plus buttons stay visible when all slots are taken, they just go dim
 */
static void note_tile_set_full( bool full ) {
    lv_obj_t *btn_table[ NOTE_SOURCE_MAX + 1 ] = { note_add_btn };

    for ( int32_t i = 0 ; i < note_sources ; i++ )
        btn_table[ i + 1 ] = note_source_table[ i ].btn;

    for ( int32_t i = 0 ; i < note_sources + 1 ; i++ ) {
        if ( !btn_table[ i ] )
            continue;

        lv_obj_set_state( btn_table[ i ], full ? LV_STATE_DISABLED : LV_STATE_DEFAULT );
        lv_obj_set_style_local_opa_scale( btn_table[ i ], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, full ? LV_OPA_40 : LV_OPA_COVER );
        lv_obj_set_click( btn_table[ i ], !full );
    }
}

void note_tile_refresh( void ) {
    if ( !notetile_init )
        return;

    int32_t entrys = note_config_get_entrys();
    char preview[ 128 ] = "";

    for ( int32_t i = 0 ; i < note_tile_visible ; i++ ) {
        note_slot_t *slot = &note_slot_table[ i ];
        note_entry_t *entry = i < entrys ? note_config_get( i ) : NULL;

        if ( !entry ) {
            lv_obj_set_hidden( slot->paper, true );
            lv_obj_set_hidden( slot->tape, true );
            continue;
        }

        bool audio = entry->kind == NOTE_KIND_AUDIO;
        bool missing = audio && !note_tile_file_exists( entry->path );
        bool done = entry->done_at != 0;
        bool play = audio && !missing;
        lv_coord_t line_h = lv_font_get_line_height( lv_obj_get_style_text_font( slot->label, LV_LABEL_PART_MAIN ) );
        lv_coord_t text_y = play ? NOTE_TEXT_Y + THEME_ICON_SIZE + THEME_PADDING / 2 : NOTE_TEXT_Y;
        int32_t lines = ( note_cell_h - text_y - THEME_PADDING - line_h ) / line_h;

        if ( lines < 1 )
            lines = 1;

        lv_obj_set_hidden( slot->paper, false );
        lv_obj_set_hidden( slot->tape, false );
        lv_obj_set_hidden( slot->play_btn, !play );

        if ( play ) {
            bool playing = note_tile_playing == i && sound_spiffs_wav_is_running();
            lv_img_set_src( lv_obj_get_child( slot->play_btn, NULL ), playing ? &wf_get_stop_img() : &wf_get_play_img() );
        }

        note_tile_preview( preview, sizeof( preview ), missing ? "missing" : entry->text );
        lv_obj_set_size( slot->label, note_cell_w - 2 * THEME_PADDING, lines * line_h );
        lv_label_set_text( slot->label, preview );
        lv_obj_align( slot->label, slot->paper, LV_ALIGN_IN_TOP_LEFT, THEME_PADDING, text_y );
        lv_obj_set_style_local_text_opa( slot->check, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, done ? LV_OPA_COVER : NOTE_CHECK_OPA );
        lv_obj_set_style_local_opa_scale( slot->paper, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, done ? LV_OPA_50 : LV_OPA_COVER );
        lv_obj_set_style_local_opa_scale( slot->tape, LV_IMG_PART_MAIN, LV_STATE_DEFAULT, done ? LV_OPA_50 : LV_OPA_COVER );
    }

    note_tile_set_full( note_config_get_open_entrys() >= note_tile_visible );
}

bool note_tile_register_source( const char *name, const lv_img_dsc_t *icon, lv_event_cb_t event_cb ) {
    if ( !notetile_init ) {
        log_e("note tile not initialized");
        return( false );
    }

    if ( note_sources >= NOTE_SOURCE_MAX || !icon || !event_cb ) {
        log_e("note: no source slot left for \"%s\"", name ? name : "" );
        return( false );
    }

    note_source_table[ note_sources ].name = name;
    note_source_table[ note_sources ].btn = wf_add_image_button( note_footer, *icon, event_cb, SYSTEM_ICON_STYLE );
    mainbar_add_slide_element( note_source_table[ note_sources ].btn );
    note_sources++;

    note_tile_align_footer();
    note_tile_refresh();
    log_i("note: source \"%s\" registered", name ? name : "" );

    return( true );
}

bool note_tile_add_audio_note( const char *path, const char *name ) {
    if ( !note_tile_make_room() ) {
        log_e("note: all slots taken, \"%s\" stays where it is", path ? path : "" );
        return( false );
    }

    if ( note_config_add_audio( path, name ) < 0 )
        return( false );

    note_tile_refresh();

    return( true );
}

/**
 * @brief   fit the squarest grid of cells into the tile, the last cell is the footer
 *
 * a note is a square piece of paper, so the cells are scored by how close to square they are.
 * a cell has to hold the plus button plus one registered source side by side, that lower bound
 * is what keeps a 240 px display at two columns.
 */
static void note_tile_build_grid( int32_t *columns, int32_t *rows ) {
    lv_coord_t usable_w = lv_disp_get_hor_res( NULL ) - 2 * THEME_PADDING;
    lv_coord_t usable_h = lv_disp_get_ver_res( NULL ) - STATUSBAR_HEIGHT - 2 * THEME_PADDING;
    lv_coord_t min_w = 2 * THEME_ICON_SIZE + THEME_PADDING;
    lv_coord_t best = -1;

    *columns = 1;
    *rows = 2;
    note_cell_w = usable_w;
    note_cell_h = ( usable_h - THEME_PADDING ) / 2;

    for ( int32_t cols = 2 ; cols <= NOTE_GRID_MAX ; cols++ ) {
        for ( int32_t r = 2 ; r <= NOTE_GRID_MAX ; r++ ) {
            if ( cols * r > NOTE_CELL_MAX )
                continue;

            lv_coord_t w = ( usable_w - THEME_PADDING * ( cols - 1 ) ) / cols;
            lv_coord_t h = ( usable_h - THEME_PADDING * ( r - 1 ) ) / r;

            if ( w < min_w || h < THEME_ICON_SIZE )
                continue;

            lv_coord_t score = abs( w - h );

            if ( best >= 0 && score >= best )
                continue;

            best = score;
            *columns = cols;
            *rows = r;
            note_cell_w = w;
            note_cell_h = h;
        }
    }
}

void note_tile_setup( void ) {

    if ( notetile_init ) {
        log_e("note tile already init");
        return;
    }

    #if defined( M5PAPER )
        note_tile_num = mainbar_add_tile( 0, 3, "note tile", ws_get_mainbar_style() );
        note_cont = mainbar_get_tile_obj( note_tile_num );
    #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 ) || defined( M5CORE2 )
        note_tile_num = mainbar_add_tile( 0, 1, "note tile", ws_get_mainbar_style() );
        note_cont = mainbar_get_tile_obj( note_tile_num );
    #elif defined( LILYGO_WATCH_2021 )
        note_tile_num = mainbar_add_tile( 0, 1, "note tile", ws_get_mainbar_style() );
        note_cont = mainbar_get_tile_obj( note_tile_num );
    #else
        note_tile_num = mainbar_add_tile( 0, 1, "note tile", ws_get_mainbar_style() );
        note_cont = mainbar_get_tile_obj( note_tile_num );
        #warning "no note tiles setup"
    #endif
    style = ws_get_mainbar_style();

    lv_style_copy( &notestyle, style);
    lv_style_set_text_opa( &notestyle, LV_OBJ_PART_MAIN, LV_OPA_30);
    lv_style_set_text_font( &notestyle, LV_STATE_DEFAULT, &Ubuntu_72px);

    notelabel = lv_label_create( note_cont, NULL);
    lv_label_set_text( notelabel, "note");
    lv_obj_reset_style_list( notelabel, LV_OBJ_PART_MAIN );
    lv_obj_add_style( notelabel, LV_OBJ_PART_MAIN, &notestyle );
    lv_obj_align( notelabel, NULL, LV_ALIGN_CENTER, 0, 0);

    note_config_setup();
    note_style_setup();

    int32_t columns = 0;
    int32_t rows = 0;

    note_tile_build_grid( &columns, &rows );

    note_tile_visible = columns * rows - 1;

    if ( note_tile_visible > NOTE_MAX )
        note_tile_visible = NOTE_MAX;
    else if ( note_tile_visible < 1 )
        note_tile_visible = 1;

    lv_coord_t usable_w = lv_disp_get_hor_res( NULL ) - 2 * THEME_PADDING;
    lv_coord_t usable_h = lv_disp_get_ver_res( NULL ) - STATUSBAR_HEIGHT - 2 * THEME_PADDING;
    lv_coord_t block_w = columns * note_cell_w + ( columns - 1 ) * THEME_PADDING;
    lv_coord_t block_h = rows * note_cell_h + ( rows - 1 ) * THEME_PADDING;
    lv_coord_t left = THEME_PADDING + ( usable_w - block_w ) / 2;
    lv_coord_t top = STATUSBAR_HEIGHT + THEME_PADDING + ( usable_h - block_h ) / 2;

    for ( int32_t i = 0 ; i < note_tile_visible ; i++ ) {
        note_slot_t *slot = &note_slot_table[ i ];
        lv_coord_t x = left + ( i % columns ) * ( note_cell_w + THEME_PADDING );
        lv_coord_t y = top + ( i / columns ) * ( note_cell_h + THEME_PADDING );

        slot->paper = wf_add_container( note_cont, LV_LAYOUT_OFF, LV_FIT_NONE, LV_FIT_NONE, false, note_style_get_paper( i ) );
        lv_obj_set_size( slot->paper, note_cell_w, note_cell_h );
        lv_obj_align( slot->paper, note_cont, LV_ALIGN_IN_TOP_LEFT, x, y );
        lv_obj_set_click( slot->paper, true );
        lv_obj_set_event_cb( slot->paper, note_tile_paper_event_cb );
        lv_obj_set_user_data( slot->paper, ( lv_obj_user_data_t )( intptr_t )i );
        mainbar_add_slide_element( slot->paper );

        slot->tape = lv_img_create( note_cont, NULL );
        lv_img_set_src( slot->tape, &note_tape_48px );
        lv_obj_reset_style_list( slot->tape, LV_OBJ_PART_MAIN );
        lv_obj_add_style( slot->tape, LV_OBJ_PART_MAIN, note_style_get_tape() );
        lv_obj_align( slot->tape, slot->paper, LV_ALIGN_IN_TOP_MID, 0, -NOTE_TAPE_OVERHANG );
        lv_obj_set_click( slot->tape, false );

        slot->play_btn = wf_add_play_button( slot->paper, note_tile_play_event_cb, SYSTEM_ICON_STYLE );
        lv_obj_align( slot->play_btn, slot->paper, LV_ALIGN_IN_TOP_MID, 0, NOTE_TEXT_Y );
        lv_obj_set_user_data( slot->play_btn, ( lv_obj_user_data_t )( intptr_t )i );
        mainbar_add_slide_element( slot->play_btn );

        slot->check = lv_label_create( slot->paper, NULL );
        lv_label_set_text( slot->check, LV_SYMBOL_OK );
        lv_obj_reset_style_list( slot->check, LV_OBJ_PART_MAIN );
        lv_obj_add_style( slot->check, LV_OBJ_PART_MAIN, note_style_get_text() );
        lv_obj_align( slot->check, slot->paper, LV_ALIGN_IN_BOTTOM_RIGHT, -THEME_PADDING, -THEME_PADDING );
        lv_obj_set_ext_click_area( slot->check, NOTE_CHECK_EXT, NOTE_CHECK_EXT, NOTE_CHECK_EXT, NOTE_CHECK_EXT );
        lv_obj_set_click( slot->check, true );
        lv_obj_set_event_cb( slot->check, note_tile_check_event_cb );
        lv_obj_set_user_data( slot->check, ( lv_obj_user_data_t )( intptr_t )i );
        mainbar_add_slide_element( slot->check );

        slot->label = lv_label_create( slot->paper, NULL );
        lv_label_set_long_mode( slot->label, LV_LABEL_LONG_DOT );
        lv_obj_reset_style_list( slot->label, LV_OBJ_PART_MAIN );
        lv_obj_add_style( slot->label, LV_OBJ_PART_MAIN, note_style_get_text() );
        lv_obj_set_click( slot->label, false );

        lv_obj_set_hidden( slot->paper, true );
        lv_obj_set_hidden( slot->tape, true );
    }

    note_footer_x = left + ( note_tile_visible % columns ) * ( note_cell_w + THEME_PADDING );
    note_footer_y = top + ( note_tile_visible / columns ) * ( note_cell_h + THEME_PADDING );

    note_footer = wf_add_container( note_cont, LV_LAYOUT_PRETTY_MID, LV_FIT_NONE, LV_FIT_TIGHT, false, ws_get_mainbar_style() );
    lv_obj_set_width( note_footer, note_cell_w );

    mainbar_add_slide_element( note_footer );

    note_add_btn = wf_add_add_button( note_footer, note_tile_add_event_cb, SYSTEM_ICON_STYLE );
    mainbar_add_slide_element( note_add_btn );

    note_tile_align_footer();

    mainbar_add_tile_button_cb( note_tile_num, note_tile_button_event_cb );
    mainbar_add_tile_activate_cb( note_tile_num, note_tile_activate_cb );

    note_edit_setup();

    note_tile_task = lv_task_create( note_tile_lv_task, NOTE_TASK_IDLE_PERIOD, LV_TASK_PRIO_MID, NULL );

    notetile_init = true;

    note_tile_refresh();

    log_i("note: %dx%d grid, %d note(s) at %dx%d px", columns, rows, note_tile_visible, note_cell_w, note_cell_h );
}

static void note_tile_paper_event_cb( lv_obj_t *obj, lv_event_t event ) {
    if ( event != LV_EVENT_CLICKED )
        return;

    note_edit_open( ( int32_t )( intptr_t )lv_obj_get_user_data( obj ) );
}

static void note_tile_check_event_cb( lv_obj_t *obj, lv_event_t event ) {
    if ( event != LV_EVENT_CLICKED )
        return;

    int32_t index = ( int32_t )( intptr_t )lv_obj_get_user_data( obj );
    note_tile_stop_playback();
    note_config_toggle_done( index );
    note_tile_refresh();
}

static void note_tile_play_event_cb( lv_obj_t *obj, lv_event_t event ) {
    if ( event != LV_EVENT_CLICKED )
        return;

    int32_t index = ( int32_t )( intptr_t )lv_obj_get_user_data( obj );
    note_entry_t *entry = note_config_get( index );

    if ( !entry || entry->kind != NOTE_KIND_AUDIO )
        return;

    if ( index == note_tile_playing && sound_spiffs_wav_is_running() ) {
        note_tile_stop_playback();
        note_tile_refresh();
        return;
    }

    note_tile_stop_playback();
    sound_play_spiffs_wav( entry->path, SOUND_TYPE_FOREGROUND );
    note_tile_playing = index;
    note_tile_refresh();
}

static void note_tile_add_event_cb( lv_obj_t *obj, lv_event_t event ) {
    if ( event != LV_EVENT_CLICKED )
        return;

    if ( !note_tile_make_room() )
        return;

    note_edit_open( -1 );
}

static void note_tile_activate_cb( void ) {
    note_tile_refresh();
}

static void note_tile_lv_task( lv_task_t *task ) {
    bool visible = note_tile_is_visible();

    lv_task_set_period( task, visible ? NOTE_TASK_PERIOD : NOTE_TASK_IDLE_PERIOD );

    if ( !visible ) {
        note_tile_stop_playback();
        return;
    }

    if ( note_config_expire() ) {
        note_tile_stop_playback();
        note_tile_refresh();
        return;
    }

    if ( note_tile_playing >= 0 ) {
        if ( !sound_spiffs_wav_is_running() ) {
            note_tile_playing = -1;
            note_tile_refresh();
        }
        else
            lv_disp_trig_activity( NULL );
    }
}

static bool note_tile_button_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case BUTTON_LEFT:   mainbar_jump_to_tilenumber( setup_get_tile_num(), LV_ANIM_OFF );
                            mainbar_clear_history();
                            break;
    }
    return( true );
}

uint32_t note_tile_get_tile_num( void ) {
    /*
     * check if maintile alread initialized
     */
    if ( !notetile_init ) {
        log_e("maintile not initialized");
        while( true );
    }

    return( note_tile_num );
}
