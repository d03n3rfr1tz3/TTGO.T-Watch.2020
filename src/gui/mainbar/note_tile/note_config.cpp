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

#include <string.h>

#include "note_config.h"

static note_config_t note_config;
static bool note_config_init = false;

static void note_config_sort( void );
static bool note_config_before( note_entry_t *a, note_entry_t *b );

note_config_t::note_config_t() : BaseJsonConfig( NOTE_JSON_CONFIG_FILE ) {}

bool note_config_t::onSave( JsonDocument& doc ) {
    JsonArray notes = doc.createNestedArray("notes");

    for ( int32_t i = 0 ; i < entrys ; i++ ) {
        JsonObject note = notes.createNestedObject();
        note["kind"] = (int)entry[ i ].kind;
        note["text"] = entry[ i ].text;
        note["path"] = entry[ i ].path;
        note["created"] = (uint32_t)entry[ i ].created;
        note["done_at"] = (uint32_t)entry[ i ].done_at;
    }

    return( true );
}

bool note_config_t::onLoad( JsonDocument& doc ) {
    entrys = 0;

    if ( !doc["notes"].is<JsonArray>() )
        return( true );

    for ( JsonObject note : doc["notes"].as<JsonArray>() ) {
        if ( entrys >= NOTE_MAX ) {
            log_e("note: more notes stored than slots available, dropping the rest");
            break;
        }

        note_entry_t *e = &entry[ entrys ];
        memset( e, 0, sizeof( note_entry_t ) );
        e->kind = ( note["kind"] | 0 ) == NOTE_KIND_AUDIO ? NOTE_KIND_AUDIO : NOTE_KIND_TEXT;
        strncpy( e->text, note["text"] | "", sizeof( e->text ) - 1 );
        strncpy( e->path, note["path"] | "", sizeof( e->path ) - 1 );
        e->created = note["created"] | 0;
        e->done_at = note["done_at"] | 0;
        entrys++;
    }

    return( true );
}

bool note_config_t::onDefault( void ) {
    entrys = 0;

    return( true );
}

/**
 * @brief open notes first, newest on top
 */
static bool note_config_before( note_entry_t *a, note_entry_t *b ) {
    if ( ( a->done_at == 0 ) != ( b->done_at == 0 ) )
        return( a->done_at == 0 );

    return( a->created > b->created );
}

static void note_config_sort( void ) {
    for ( int32_t i = 1 ; i < note_config.entrys ; i++ ) {
        note_entry_t tmp = note_config.entry[ i ];
        int32_t j = i;

        while ( j > 0 && note_config_before( &tmp, &note_config.entry[ j - 1 ] ) ) {
            note_config.entry[ j ] = note_config.entry[ j - 1 ];
            j--;
        }

        note_config.entry[ j ] = tmp;
    }
}

/**
 * @brief make room at the top for a new note
 */
static note_entry_t *note_config_insert( void ) {
    if ( note_config.entrys >= NOTE_MAX )
        return( NULL );

    for ( int32_t i = note_config.entrys ; i > 0 ; i-- )
        note_config.entry[ i ] = note_config.entry[ i - 1 ];

    note_config.entrys++;
    memset( &note_config.entry[ 0 ], 0, sizeof( note_entry_t ) );
    note_config.entry[ 0 ].created = time( NULL );

    return( &note_config.entry[ 0 ] );
}

void note_config_setup( void ) {
    if ( note_config_init )
        return;

    note_config.load();
    note_config_sort();
    note_config_init = true;

    log_i("note: %d note(s) loaded", note_config.entrys );
}

int32_t note_config_get_entrys( void ) {
    return( note_config.entrys );
}

int32_t note_config_get_open_entrys( void ) {
    int32_t open = 0;

    for ( int32_t i = 0 ; i < note_config.entrys ; i++ ) {
        if ( note_config.entry[ i ].done_at == 0 )
            open++;
    }

    return( open );
}

note_entry_t *note_config_get( int32_t entry ) {
    if ( entry < 0 || entry >= note_config.entrys )
        return( NULL );

    return( &note_config.entry[ entry ] );
}

int32_t note_config_add_text( const char *text ) {
    note_entry_t *e = note_config_insert();

    if ( !e ) {
        log_e("note: no slot left for a text note");
        return( -1 );
    }

    e->kind = NOTE_KIND_TEXT;
    strncpy( e->text, text ? text : "", sizeof( e->text ) - 1 );
    note_config.save();

    return( 0 );
}

int32_t note_config_add_audio( const char *path, const char *name ) {
    if ( !path || !*path )
        return( -1 );

    note_entry_t *e = note_config_insert();

    if ( !e ) {
        log_e("note: no slot left for an audio note");
        return( -1 );
    }

    e->kind = NOTE_KIND_AUDIO;
    strncpy( e->path, path, sizeof( e->path ) - 1 );
    strncpy( e->text, name ? name : path, sizeof( e->text ) - 1 );
    note_config.save();

    return( 0 );
}

bool note_config_set_text( int32_t entry, const char *text ) {
    note_entry_t *e = note_config_get( entry );

    if ( !e )
        return( false );

    strncpy( e->text, text ? text : "", sizeof( e->text ) - 1 );
    e->text[ sizeof( e->text ) - 1 ] = '\0';
    note_config.save();

    return( true );
}

bool note_config_toggle_done( int32_t entry ) {
    note_entry_t *e = note_config_get( entry );

    if ( !e )
        return( false );

    e->done_at = e->done_at ? 0 : time( NULL );
    note_config_sort();
    note_config.save();

    return( true );
}

bool note_config_remove( int32_t entry ) {
    if ( entry < 0 || entry >= note_config.entrys )
        return( false );

    for ( int32_t i = entry ; i < note_config.entrys - 1 ; i++ )
        note_config.entry[ i ] = note_config.entry[ i + 1 ];

    note_config.entrys--;
    note_config.save();

    return( true );
}

/**
 * @brief the sort keeps checked notes at the end, so the last one is the one shown lowest
 */
bool note_config_drop_oldest_done( void ) {
    for ( int32_t i = note_config.entrys - 1 ; i >= 0 ; i-- ) {
        if ( note_config.entry[ i ].done_at == 0 )
            continue;

        return( note_config_remove( i ) );
    }

    return( false );
}

bool note_config_expire( void ) {
    time_t now = time( NULL );
    bool dropped = false;

    for ( int32_t i = note_config.entrys - 1 ; i >= 0 ; i-- ) {
        note_entry_t *e = &note_config.entry[ i ];

        if ( e->done_at == 0 || now - e->done_at <= NOTE_DONE_GRACE )
            continue;

        for ( int32_t j = i ; j < note_config.entrys - 1 ; j++ )
            note_config.entry[ j ] = note_config.entry[ j + 1 ];

        note_config.entrys--;
        dropped = true;
    }

    if ( dropped )
        note_config.save();

    return( dropped );
}
