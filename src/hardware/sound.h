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
#ifndef _SOUND_H
    #define _SOUND_H

    #include "callback.h"
    #include "hardware/config/soundconfig.h"
    #include "utils/io.h"
    
    #define SOUNDCTL_ENABLED           _BV(0)         /** @brief event mask for sound enabled/disable, callback arg is (bool*) */
    #define SOUNDCTL_VOLUME            _BV(1)         /** @brief event mask for sound volume change, callback arg is (uint8_t*)  */

    /**
     * @brief how a sound is treated inside the silence timeframe
     */
    typedef enum {
        SOUND_TYPE_FOREGROUND = 0,                  /** @brief alarm, find my watch or a sound the user asked for, plays inside the silence timeframe */
        SOUND_TYPE_BACKGROUND                       /** @brief notification, game or system sound, stays silent inside the silence timeframe */
    } sound_type_t;

    /**
     * @brief pick a random mp3 file from SPIFFS
     *
     * @param   filename    takes the SPIFFS path of the picked file
     * @param   len         size of the filename buffer
     *
     * @return  true if a file was found
     */
    bool sound_get_random_spiffs_mp3( char *filename, size_t len );
    /**
     * @brief play mp3 file from SPIFFS by path/filename
     *
     * @param   filename        the SPIFFS path to the file to be played
     * @param   sound_type      SOUND_TYPE_FOREGROUND ignores the silence timeframe, SOUND_TYPE_BACKGROUND respects it
     */
    void sound_play_spiffs_mp3( const char *filename, sound_type_t sound_type );
    /**
     * @brief play wave sound from PROGMEM
     *
     * To transform an file to *data use: `xxd -i inout.wav > output.c`
     *
     * @param   data            data from PROGMEM as array
     * @param   len             data array length
     * @param   sound_type      SOUND_TYPE_FOREGROUND ignores the silence timeframe, SOUND_TYPE_BACKGROUND respects it
     */
    void sound_play_progmem_wav( const void *data, uint32_t len, sound_type_t sound_type );
    /**
     * @brief play a RTTTL ringtone/jingle, no audio file needed
     *
     * @param   song            RTTTL string, e.g. "win:d=16,o=6,b=200:c,e,g,8c7"
     * @param   sound_type      SOUND_TYPE_FOREGROUND ignores the silence timeframe, SOUND_TYPE_BACKGROUND respects it
     */
    void sound_play_rtttl( const char *song, sound_type_t sound_type );
    /**
     * @brief play a sine tone of a given frequency, ends by itself after 30s
     *
     * @param   frequency       tone frequency in Hz, up to 8000
     * @param   sound_type      SOUND_TYPE_FOREGROUND ignores the silence timeframe, SOUND_TYPE_BACKGROUND respects it
     */
    void sound_play_tone( uint16_t frequency, sound_type_t sound_type );
    /**
     * @brief stop a running tone
     */
    void sound_stop_tone( void );
    /**
     * @brief check if a tone is playing
     *
     * @return true if a tone is playing
     */
    bool sound_tone_is_running( void );
    /**
     * @brief play a wave file from SPIFFS by path/filename
     *
     * @param   filename        the SPIFFS path to the file to be played
     * @param   sound_type      SOUND_TYPE_FOREGROUND ignores the silence timeframe, SOUND_TYPE_BACKGROUND respects it
     */
    void sound_play_spiffs_wav( const char *filename, sound_type_t sound_type );
    /**
     * @brief stop a running wave file from SPIFFS
     */
    void sound_stop_spiffs_wav( void );
    /**
     * @brief check if a wave file from SPIFFS is playing
     *
     * @return true if a wave file is playing
     */
    bool sound_spiffs_wav_is_running( void );
    /**
     * @brief setup sound
     */
    void sound_setup( void );
    /**
     * @brief check if sound available
     * @return true if available
     */
    bool sound_get_available( void );
    /**
     * @brief put sound output to standby (disable)
     */
    void sound_standby( void );
    /**
     * @brief wakeup sound output
     */
    void sound_wakeup( void );
    /**
     * @brief enable or disable the power output for AXP202_LDO3
     * 
     * @param enable = true sets the AXP202_LDO3 power output to high false to low
     */
    void sound_set_enabled( bool enabled = true );
    /**
     * @brief sound loop
     */
    void sound_loop( void );
    /**
     * @brief speak
     *  
     * @param   str             the text to be spoken
     * @param   sound_type      SOUND_TYPE_FOREGROUND ignores the silence timeframe, SOUND_TYPE_BACKGROUND respects it
     */
    void sound_speak( const char *str, sound_type_t sound_type );
    /**
     * @brief save config for sound to spiffs
     */
    void sound_save_config( void );
    /**
     * @brief read config for sound from spiffs
     */
    void sound_read_config( void );
    /**
     * @brief get the sound enabled value
     * 
     * @return true if sound is enabled, false if not
     */
    bool sound_get_enabled_config( void );
    /**
     * @brief   set the sound enabled configuration
     * 
     * @param   enable    true = enabled, false = disabled
     */
    void sound_set_enabled_config( bool enable );
    /**
     * @brief get the current volume configuration
     * 
     * @return volume value between 0-100
     */
    uint8_t sound_get_volume_config( void );
    /**
     * @brief set the current volume configuration
     * 
     * @param volume from 0-100
     */
    void sound_set_volume_config( uint8_t volume );
    /**
     * @brief get the silence timeframe enabled value
     *
     * @return true if sound is silenced inside the timeframe, false if not
     */
    bool sound_get_silence_config( void );
    /**
     * @brief set the silence timeframe enabled value
     *
     * @param enable    true = silence inside the timeframe, false = never silence
     */
    void sound_set_silence_config( bool enable );
    /**
     * @brief get the silence timeframe start time
     *
     * @param hour      takes the start hour, 0-23
     * @param minute    takes the start minute, 0-59
     */
    void sound_get_silence_start_config( int *hour, int *minute );
    /**
     * @brief set the silence timeframe start time
     *
     * @param hour      start hour, 0-23
     * @param minute    start minute, 0-59
     */
    void sound_set_silence_start_config( int hour, int minute );
    /**
     * @brief get the silence timeframe end time
     *
     * @param hour      takes the end hour, 0-23
     * @param minute    takes the end minute, 0-59
     */
    void sound_get_silence_end_config( int *hour, int *minute );
    /**
     * @brief set the silence timeframe end time
     *
     * @param hour      end hour, 0-23
     * @param minute    end minute, 0-59
     */
    void sound_set_silence_end_config( int hour, int minute );
    /**
     * @brief registers a callback function which is called on a corresponding event
     * 
     * @param   event           possible values: SOUNDCTL_ENABLED, SOUNDCTL_VOLUME
     * @param   callback_func    pointer to the callback function 
     * @param   id              programm id
     * 
     * @return  true if success, false if failed
     */
    bool sound_register_cb( EventBits_t event, CALLBACK_FUNC callback_func, const char *id );

#endif // _SOUND_H
