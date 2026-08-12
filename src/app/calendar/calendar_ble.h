/****************************************************************************
 *   Copyright  2026  Dirk Sarodnick
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
#ifndef _CALENDAR_BLE_H
    #define _CALENDAR_BLE_H

    #include <stdint.h>
    #include <stdbool.h>
    #include <time.h>

    /**
     * @brief number of events we keep, the phone sends its whole lookahead window without a limit
     */
    #define CALENDAR_BLE_MAX_EVENTS     64
    /**
     * @brief a calendar event received from gadgetbridge, read only, never stored in calendar.db
     */
    typedef struct {
        bool        used;                   /** @brief slot is taken */
        bool        reminded;               /** @brief reminder already fired */
        bool        allday;                 /** @brief all day event, no reminder */
        int64_t     id;                     /** @brief android event id, does not fit into int32 */
        time_t      start;                  /** @brief start time, epoch seconds */
        uint32_t    duration;               /** @brief duration in seconds */
        char        title[ 64 ];
        char        location[ 64 ];
        char        calname[ 32 ];
        char        description[ 160 ];
    } calendar_ble_event_t;
    /**
     * @brief setup the gadgetbridge calendar receiver
     */
    void calendar_ble_setup( void );
    /**
     * @brief collect all events of a day, sorted by start time
     *
     * @param   year        year
     * @param   month       month, 1-12
     * @param   day         day of month
     * @param   slots       target table for the slot numbers
     * @param   max         size of the target table
     *
     * @return  number of events written into slots
     */
    int calendar_ble_get_day_events( int year, int month, int day, int *slots, int max );
    /**
     * @brief check if a day has at least one event
     *
     * @return  true if the day has an event
     */
    bool calendar_ble_has_day( int year, int month, int day );
    /**
     * @brief get an event by its slot number
     *
     * @return  pointer to the event or NULL if the slot is out of range or free
     */
    const calendar_ble_event_t *calendar_ble_get_event( int slot );

#endif // _CALENDAR_BLE_H
