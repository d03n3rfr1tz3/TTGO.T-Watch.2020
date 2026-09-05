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
#ifndef _NOTE_CONFIG_H
    #define _NOTE_CONFIG_H

    #include <time.h>
    #include "utils/basejsonconfig.h"

    #define NOTE_JSON_CONFIG_FILE   "/note.json"
    #define NOTE_MAX                7       /** @brief one cell less than NOTE_CELL_MAX, what fits on the biggest display */
    #define NOTE_TEXT_MAX           256
    #define NOTE_PATH_MAX           32      /** @brief CONFIG_SPIFFS_OBJ_NAME_LEN */
    #define NOTE_DONE_GRACE         60      /** @brief seconds until a checked note is dropped */

    typedef enum {
        NOTE_KIND_TEXT = 0,
        NOTE_KIND_AUDIO
    } note_kind_t;

    /**
     * @brief a single note
     */
    typedef struct {
        note_kind_t kind;
        char text[ NOTE_TEXT_MAX ];         /** @brief TEXT: the content, AUDIO: the display name */
        char path[ NOTE_PATH_MAX ];         /** @brief AUDIO only, the referenced wav file */
        time_t created;
        time_t done_at;                     /** @brief 0 while open */
    } note_entry_t;

    /**
     * @brief note config structure
     */
    class note_config_t : public BaseJsonConfig {
        public:
        note_config_t();
        int32_t entrys = 0;
        note_entry_t entry[ NOTE_MAX ];

        protected:
        ////////////// Available for overloading: //////////////
        virtual bool onLoad(JsonDocument& document);
        virtual bool onSave(JsonDocument& document);
        virtual bool onDefault( void );
        virtual size_t getJsonBufferSize() { return 4096; }
    } ;

    /**
     * @brief load the notes from spiffs, call once
     */
    void note_config_setup( void );
    /**
     * @brief   get the number of stored notes
     *
     * @return  number of notes, 0 to NOTE_MAX
     */
    int32_t note_config_get_entrys( void );
    /**
     * @brief   get the number of notes still open
     *
     * @return  number of notes without a check
     */
    int32_t note_config_get_open_entrys( void );
    /**
     * @brief   get a note
     *
     * @param   entry   index into the sorted note table
     *
     * @return  pointer to the note or NULL if the index is out of range
     */
    note_entry_t *note_config_get( int32_t entry );
    /**
     * @brief   add a text note, it becomes the topmost entry
     *
     * @param   text    note content
     *
     * @return  index of the new note or -1 if no slot is left
     */
    int32_t note_config_add_text( const char *text );
    /**
     * @brief   add a note referencing an existing audio file
     *
     * @param   path    file to play, stays owned by whoever recorded it
     * @param   name    display name
     *
     * @return  index of the new note or -1 if no slot is left
     */
    int32_t note_config_add_audio( const char *path, const char *name );
    /**
     * @brief   change the text of a note, the order stays untouched
     */
    bool note_config_set_text( int32_t entry, const char *text );
    /**
     * @brief   check or uncheck a note, a checked note sinks below the open ones
     */
    bool note_config_toggle_done( int32_t entry );
    /**
     * @brief   drop a note right away
     */
    bool note_config_remove( int32_t entry );
    /**
     * @brief   drop all notes checked longer ago than NOTE_DONE_GRACE
     *
     * @return  true if something was dropped
     */
    bool note_config_expire( void );
    /**
     * @brief   drop the checked note shown lowest, to free a cell before the grace period ends
     *
     * @return  true if a note was dropped, false if none is checked
     */
    bool note_config_drop_oldest_done( void );

#endif // _NOTE_CONFIG_H
