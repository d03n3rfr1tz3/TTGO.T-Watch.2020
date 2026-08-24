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

#include "powermgm.h"
#include "sound.h"
#include "timesync.h"
#include "callback.h"
#include "hardware/config/soundconfig.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#else
    #include <SPIFFS.h>
    /*
    * based on https://github.com/earlephilhower/ESP8266Audio
    */
    #if defined( M5PAPER )
    #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V3 )
        #include "TTGO.h"

        #include "AudioFileSourceSPIFFS.h"
        #include "AudioFileSourcePROGMEM.h"
        #include "AudioFileSourceFunction.h"
        #include "AudioFileSourceID3.h"
        #include "AudioGeneratorMP3.h"
        #include "AudioGeneratorWAV.h"
        #include "AudioGeneratorRTTTL.h"
        #include "AudioOutputI2S.h"
        #include <ESP8266SAM.h>

        #ifndef TWATCH_SOUND_I2S_PORT
            #define TWATCH_SOUND_I2S_PORT   0
        #endif

        AudioFileSourceSPIFFS *spliffs_file;
        AudioOutputI2S *out;
        AudioFileSourceID3 *id3;

        AudioGeneratorMP3 *mp3;
        AudioGeneratorWAV *wav;
        ESP8266SAM *sam;
        AudioFileSourcePROGMEM *progmem_file;
        AudioGeneratorRTTTL *rtttl = NULL;
        AudioFileSourcePROGMEM *rtttl_file = NULL;
        char rtttl_song[ 96 ] = "";
        
        AudioGeneratorWAV *sound_tone = NULL;
        AudioFileSourceFunction *sound_tone_file = NULL;
        static uint16_t sound_tone_hz = 1000;

        #define SOUND_TONE_RATE         32000                                       /** @brief four samples per period at the highest tone */
        #define SOUND_TONE_SECONDS      30.0f                                       /** @brief length of the generated source */
        #define SOUND_TONE_AMPLITUDE    0.25f                                       /** @brief headroom, sound_apply_gain() goes up to 3.5 */
        #define SOUND_TONE_TAIL         1600                                        /** @brief silent samples at the end, longer than the i2s dma ring */
        #define SOUND_TONE_RAMP         160                                         /** @brief fade samples, a hard edge clicks */
        #define SOUND_TONE_SAMPLES      ( ( uint32_t )( SOUND_TONE_SECONDS * SOUND_TONE_RATE ) )
        #define SOUND_TONE_BODY         ( SOUND_TONE_SAMPLES - SOUND_TONE_TAIL )
    #elif defined( LILYGO_WATCH_2020_V2 )
    #elif defined( LILYGO_WATCH_2021 )    
    #elif defined( WT32_SC01 )
    #else
        #warning "no hardware driver for sound"
    #endif
#endif

bool sound_init = false;
bool is_speaking = false;

sound_config_t sound_config;

callback_t *sound_callback = NULL;

bool sound_powermgm_event_cb( EventBits_t event, void *arg );
bool sound_powermgm_loop_cb( EventBits_t event, void *arg );
bool sound_send_event_cb( EventBits_t event, void*arg );
bool sound_is_silenced( void );

#ifdef NATIVE_64BIT
#else
    #if defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V3 )
        /**
         * @brief apply the configured volume to the audio output
         */
        static void sound_apply_gain( void ) {
            // limiting max gain to 3.5 (max gain is 4.0)
            out->SetGain( 3.5f * ( sound_config.volume / 100.0f ) );
        }

        static bool sound_boost = false;
        /**
         * @brief hold the cpu boost, any pm lock also prevents automatic light sleep
         */
        static void sound_boost_take( void ) {
            if ( sound_boost )
                return;
            powermgm_cpu_boost_take();
            sound_boost = true;
        }

        static void sound_boost_give( void ) {
            if ( !sound_boost )
                return;
            powermgm_cpu_boost_give();
            sound_boost = false;
        }
        /**
         * @brief   one sine sample
         * @param   t   time in seconds since the start of the source
         * @return  sample between -1.0 and 1.0
         */
        static float sound_tone_sample( float t ) {
            uint32_t n = ( uint32_t )( t * SOUND_TONE_RATE + 0.5f );

            if ( n >= SOUND_TONE_BODY )
                return( 0.0f );

            float gain = SOUND_TONE_AMPLITUDE;
            if ( n < SOUND_TONE_RAMP )
                gain *= ( float )n / SOUND_TONE_RAMP;
            else if ( n > SOUND_TONE_BODY - SOUND_TONE_RAMP )
                gain *= ( float )( SOUND_TONE_BODY - n ) / SOUND_TONE_RAMP;

            uint32_t phase = ( n % SOUND_TONE_RATE ) * sound_tone_hz % SOUND_TONE_RATE;

            return( gain * sinf( ( float )phase * ( 2.0f * PI / SOUND_TONE_RATE ) ) );
        }
    #endif
#endif

void sound_setup( void ) {
    if ( sound_init )
        return;

    /*
     * read config from SPIFFS
     */
    sound_config.load();
    /*
     * config sound driver and interface
     */
    #ifdef NATIVE_64BIT

    #else
        #if defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V3 )
            /*
            * set sound chip voltage on V1
            */
            #if defined( LILYGO_WATCH_2020_V1 )
                    TTGOClass *ttgo = TTGOClass::getWatch();
                    ttgo->power->setLDO3Mode( AXP202_LDO3_MODE_DCIN );
                    ttgo->power->setLDO3Voltage( 3300 );
            #endif
            /**
             * set sound driver
             */
            out = new AudioOutputI2S( TWATCH_SOUND_I2S_PORT );
            out->SetPinout( TWATCH_DAC_IIS_BCK, TWATCH_DAC_IIS_WS, TWATCH_DAC_IIS_DOUT );
            sound_apply_gain();
            mp3 = new AudioGeneratorMP3();
            wav = new AudioGeneratorWAV();
            sam = new ESP8266SAM;
            sam->SetVoice(sam->VOICE_SAM);
            /*
            * register all powermgm callback functions
            */
            powermgm_register_cb( POWERMGM_SILENCE_WAKEUP | POWERMGM_STANDBY | POWERMGM_WAKEUP, sound_powermgm_event_cb, "powermgm sound" );
            powermgm_register_loop_cb( POWERMGM_STANDBY | POWERMGM_SILENCE_WAKEUP | POWERMGM_WAKEUP, sound_powermgm_loop_cb, "powermgm sound loop" );
            sound_set_enabled( sound_config.enable );

            sound_send_event_cb( SOUNDCTL_ENABLED, (void *)&sound_config.enable );
            sound_send_event_cb( SOUNDCTL_VOLUME, (void *)&sound_config.volume );

            sound_init = true;
        #else
            sound_set_enabled( false );
            sound_init = false;
        #endif
    #endif
}

bool sound_get_available( void ) {
    bool retval = false;

    #ifdef NATIVE_64BIT
    #else
        #if defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V3 )
            retval = true;
        #endif
    #endif

   return( retval );
}

bool sound_powermgm_event_cb( EventBits_t event, void *arg ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return( true );
    }

    switch( event ) {
        case POWERMGM_STANDBY:          sound_set_enabled( false );
                                        log_d("go standby");
                                        break;
        case POWERMGM_WAKEUP:           sound_set_enabled( sound_config.enable );
                                        log_d("go wakeup");
                                        break;
        case POWERMGM_SILENCE_WAKEUP:   sound_set_enabled( sound_config.enable );
                                        log_d("go wakeup");
                                        break;
    }
    return( true );
}

bool sound_powermgm_loop_cb( EventBits_t event, void *arg ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return( true );
    }
#ifdef NATIVE_64BIT

#else
    #if defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V3 )
        if ( sound_config.enable && sound_init ) {
            if ( mp3->isRunning() || wav->isRunning() || ( rtttl && rtttl->isRunning() ) || ( sound_tone && sound_tone->isRunning() ) )
                sound_boost_take();

            if ( mp3->isRunning() && !mp3->loop() ) {
                log_d("stop playing mp3 sound");
                mp3->stop();
            }
            if ( wav->isRunning() && !wav->loop() ) {
                log_d("stop playing wav sound");
                wav->stop();
            }
            if ( rtttl && rtttl->isRunning() && !rtttl->loop() ) {
                log_d("stop playing rtttl sound");
                rtttl->stop();
            }
            if ( sound_tone && sound_tone->isRunning() && !sound_tone->loop() ) {
                log_d("stop playing tone");
                sound_tone->stop();
            }

            if ( !mp3->isRunning() && !wav->isRunning() && !( rtttl && rtttl->isRunning() ) && !( sound_tone && sound_tone->isRunning() ) )
                sound_boost_give();
        }
    #endif
#endif
    return( true );
}

bool sound_register_cb( EventBits_t event, CALLBACK_FUNC callback_func, const char *id ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return( true );
    }

    /*
     * check if an callback table exist, if not allocate a callback table
     */
    if ( sound_callback == NULL ) {
        sound_callback = callback_init( "sound" );
        if ( sound_callback == NULL ) {
            log_e("sound callback alloc failed");
            while(true);
        }
    }
    /*
     * register an callback entry and return them
     */
    return( callback_register( sound_callback, event, callback_func, id ) );
}

bool sound_send_event_cb( EventBits_t event, void *arg ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return( true );
    }
    /*
     * call all callbacks with her event mask
     */
    return( callback_send( sound_callback, event, arg ) );
}

/**
 * @brief enable or disable the power output for AXP202_LDO3 or AXP202_LDO4
 * depending on the current value of: sound_config.enable
 */
void sound_set_enabled( bool enabled ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return;
    }
#ifdef NATIVE_64BIT

#else
    #if defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V3 )
        if ( enabled ) {
            /**
             * ttgo->enableAudio() is not working
             */
            #if     defined( LILYGO_WATCH_2020_V1 )
                    TTGOClass *ttgo = TTGOClass::getWatch();
                    ttgo->power->setPowerOutPut( AXP202_LDO3, AXP202_ON );
            #elif   defined( LILYGO_WATCH_2020_V3 )
                    TTGOClass *ttgo = TTGOClass::getWatch();
                    ttgo->power->setPowerOutPut( AXP202_LDO4, AXP202_ON );
            #endif
            delay( 50 );
        }
        else {
            if ( sound_init ) {
                if ( mp3->isRunning() ) mp3->stop();
                if ( wav->isRunning() ) wav->stop();
                if ( rtttl && rtttl->isRunning() ) rtttl->stop();
                if ( sound_tone && sound_tone->isRunning() ) sound_tone->stop();
                sound_boost_give();
            }
            /**
             * ttgo->disableAudio() is not working
             */
            #if     defined( LILYGO_WATCH_2020_V1 )
                    TTGOClass *ttgo = TTGOClass::getWatch();
                    ttgo->power->setPowerOutPut( AXP202_LDO3, AXP202_OFF );
            #elif   defined( LILYGO_WATCH_2020_V3 )
                    TTGOClass *ttgo = TTGOClass::getWatch();
                    ttgo->power->setPowerOutPut( AXP202_LDO4, AXP202_OFF );
            #endif
        }
    #endif
#endif
}

bool sound_get_random_spiffs_mp3( char *filename, size_t len ) {
#ifdef NATIVE_64BIT
    return( false );
#else
    #if defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V3 )
        fs::File root = SPIFFS.open( "/" );
        if ( !root )
            return( false );

        uint32_t count = 0;
        fs::File file = root.openNextFile();
        while ( file ) {
            if ( strstr( file.name(), ".mp3" ) )
                count++;
            file = root.openNextFile();
        }

        if ( !count )
            return( false );

        uint32_t pick = random( 0, count );
        uint32_t index = 0;
        root.rewindDirectory();
        file = root.openNextFile();
        while ( file ) {
            if ( strstr( file.name(), ".mp3" ) ) {
                if ( index == pick ) {
                    snprintf( filename, len, "%s", file.name() );
                    return( true );
                }
                index++;
            }
            file = root.openNextFile();
        }
    #endif
    return( false );
#endif
}

void sound_play_spiffs_mp3( const char *filename, bool ignore_silence ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return;
    }
#ifdef NATIVE_64BIT

#else
    #if defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V3 )
        if ( sound_config.enable && sound_init ) {
            if ( ignore_silence || !sound_is_silenced() ) {
                sound_set_enabled( sound_config.enable );
                sound_apply_gain();
                log_d("playing file %s from SPIFFS", filename);
                spliffs_file = new AudioFileSourceSPIFFS(filename);
                id3 = new AudioFileSourceID3(spliffs_file);
                mp3->begin(id3, out);
                sound_boost_take();
            }
            else {
                log_d("Cannot play mp3, sound is silenced");
            }
        } else {
            log_d("Cannot play mp3, sound is disabled");
        }
    #endif
#endif
}

void sound_play_progmem_wav( const void *data, uint32_t len, bool ignore_silence ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return;
    }
#ifdef NATIVE_64BIT

#else
    #if defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V3 )
        if ( sound_config.enable && sound_init ) {
            if ( ignore_silence || !sound_is_silenced() ) {
                sound_set_enabled( sound_config.enable );
                sound_apply_gain();
                log_d("playing audio (size %d) from PROGMEM ", len );
                progmem_file = new AudioFileSourcePROGMEM( data, len );
                wav->begin(progmem_file, out);
                sound_boost_take();
            }
            else {
                log_d("Cannot play mp3, sound is silenced");
            }
        } else {
            log_d("Cannot play wav, sound is disabled");
        }
    #endif
#endif
}

void sound_play_rtttl( const char *song, bool ignore_silence ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return;
    }
#ifdef NATIVE_64BIT

#else
    #if defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V3 )
        if ( sound_config.enable && sound_init ) {
            if ( ignore_silence || !sound_is_silenced() ) {
                sound_set_enabled( sound_config.enable );
                sound_apply_gain();
                log_d("playing rtttl \"%s\"", song );
                /*
                * recreate the generator, only its destructor frees the song buffer
                */
                if ( rtttl ) {
                    if ( rtttl->isRunning() ) rtttl->stop();
                    delete rtttl;
                }
                if ( rtttl_file ) {
                    delete rtttl_file;
                }
                /*
                * the I2S DMA repeats its last buffer on underrun, so every song
                * ends with a rest longer than the ring to leave silence behind
                */
                snprintf( rtttl_song, sizeof( rtttl_song ), "%s,16p", song );
                rtttl = new AudioGeneratorRTTTL();
                rtttl_file = new AudioFileSourcePROGMEM( rtttl_song, strlen( rtttl_song ) );
                rtttl->begin( rtttl_file, out );
                sound_boost_take();
            }
            else {
                log_d("Cannot play rtttl, sound is silenced");
            }
        } else {
            log_d("Cannot play rtttl, sound is disabled");
        }
    #endif
#endif
}

void sound_play_tone( uint16_t frequency, bool ignore_silence ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return;
    }
#ifdef NATIVE_64BIT

#else
    #if defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V3 )
        if ( sound_config.enable && sound_init ) {
            if ( ignore_silence || !sound_is_silenced() ) {
                sound_set_enabled( sound_config.enable );
                sound_apply_gain();
                log_d("playing %d Hz tone", frequency );

                if ( sound_tone ) {
                    if ( sound_tone->isRunning() ) sound_tone->stop();
                    delete sound_tone;
                }
                if ( sound_tone_file ) {
                    delete sound_tone_file;
                }

                sound_tone_hz = frequency;
                sound_tone = new AudioGeneratorWAV();
                sound_tone_file = new AudioFileSourceFunction( SOUND_TONE_SECONDS, 1, SOUND_TONE_RATE, 16 );
                sound_tone_file->addAudioGenerators( sound_tone_sample );
                sound_tone->begin( sound_tone_file, out );
                sound_boost_take();
            }
            else {
                log_d("Cannot play tone, sound is silenced");
            }
        } else {
            log_d("Cannot play tone, sound is disabled");
        }
    #endif
#endif
}

void sound_stop_tone( void ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return;
    }
#ifdef NATIVE_64BIT

#else
    #if defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V3 )
        if ( sound_tone && sound_tone->isRunning() )
            sound_tone->stop();
    #endif
#endif
}

bool sound_tone_is_running( void ) {
#ifdef NATIVE_64BIT
    return( false );
#else
    #if defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V3 )
        return( sound_tone && sound_tone->isRunning() );
    #else
        return( false );
    #endif
#endif
}

void sound_speak( const char *str ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return;
    }
#ifdef NATIVE_64BIT

#else
    #if defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V3 )
        if ( sound_config.enable && sound_init ) {
            if (!sound_is_silenced()) {
                sound_set_enabled( sound_config.enable );
                log_d("Speaking text", str);
                is_speaking = true;
                sound_boost_take();
                sam->Say(out, str);
                sound_boost_give();
                is_speaking = false;
            }
            else {
                log_d("Cannot play mp3, sound is silenced");
            }
        }
        else {
            log_d("Cannot speak, sound is disabled");
        }
    #endif
#endif
}

void sound_save_config( void ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return;
    }

    sound_config.save();
}

void sound_read_config( void ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return;
    }

    sound_config.load();
}

bool sound_get_enabled_config( void ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return( false );
    }

    return sound_config.enable;
}

void sound_set_enabled_config( bool enable ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return;
    }

    sound_config.enable = enable;
    if ( sound_config.enable) {
        sound_set_enabled( true );
    }
    else {
        sound_set_enabled( false );
    }
    sound_send_event_cb( SOUNDCTL_ENABLED, (void *)&sound_config.enable ); 
}

uint8_t sound_get_volume_config( void ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return( 0 );
    }

    return( sound_config.volume );
}

void sound_set_volume_config( uint8_t volume ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return;
    }

    sound_config.volume = volume;
        
#ifdef NATIVE_64BIT

#else
    #if defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V3 )
        if ( sound_config.enable && sound_init ) {
            log_d("Setting sound volume to: %d", volume);
            sound_apply_gain();
        }
    #endif
#endif
    sound_send_event_cb( SOUNDCTL_VOLUME, (void *)&sound_config.volume ); 
}

bool sound_get_silence_config( void ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return( false );
    }

    return( sound_config.silence_timeframe );
}

void sound_set_silence_config( bool enable ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return;
    }

    sound_config.silence_timeframe = enable;
}

void sound_get_silence_start_config( int *hour, int *minute ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return;
    }

    if ( hour ) *hour = sound_config.silence_start_hour;
    if ( minute ) *minute = sound_config.silence_start_minute;
}

void sound_set_silence_start_config( int hour, int minute ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return;
    }

    sound_config.silence_start_hour = hour;
    sound_config.silence_start_minute = minute;
}

void sound_get_silence_end_config( int *hour, int *minute ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return;
    }

    if ( hour ) *hour = sound_config.silence_end_hour;
    if ( minute ) *minute = sound_config.silence_end_minute;
}

void sound_set_silence_end_config( int hour, int minute ) {
    /**
     * check if sound available
     */
    if( !sound_get_available() ) {
        return;
    }

    sound_config.silence_end_hour = hour;
    sound_config.silence_end_minute = minute;
}

bool sound_is_silenced( void ) {
    if ( !sound_config.silence_timeframe ) {
        log_d("no silence sound timeframe");
        return( false );
    }

    struct tm start;
    struct tm end;
    start.tm_hour = sound_config.silence_start_hour;
    start.tm_min = sound_config.silence_start_minute;
    end.tm_hour = sound_config.silence_end_hour;
    end.tm_min = sound_config.silence_end_minute;

    return timesync_is_between( start, end );
}