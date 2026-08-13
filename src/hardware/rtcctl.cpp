/****************************************************************************
 *   Copyright  2020  Jakub Vesely
 *   Email: jakub_vesely@seznam.cz
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
#include <time.h>

#include "rtcctl.h"
#include "powermgm.h"
#include "callback.h"
#include "timesync.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
    #include "utils/millis.h"

    volatile bool rtc_irq_flag = false;
#else
    #include <sys/time.h>

    volatile bool rtc_irq_flag = false;
    portMUX_TYPE RTC_IRQ_Mux = portMUX_INITIALIZER_UNLOCKED;

    #if defined( M5PAPER )
        #include <M5EPD.h>
        #include <SPIFFS.h>
    #elif defined( M5CORE2 )
        #include <M5Core2.h>
    #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
        #include "TTGO.h"
    #elif defined( LILYGO_WATCH_2021 )
        #include <PCF8563/pcf8563.h>
        #include <Wire.h>

        PCF8563_Class rtc;
    #elif defined( WT32_SC01 )

    #else
        #warning "no hardware driver for rtcctl"
    #endif

    void IRAM_ATTR rtcctl_irq( void );

    void IRAM_ATTR rtcctl_irq( void ) {
        portENTER_CRITICAL_ISR(&RTC_IRQ_Mux);
        rtc_irq_flag = true;
        portEXIT_CRITICAL_ISR(&RTC_IRQ_Mux);
        powermgm_resume_from_ISR();
    }
#endif

static rtcctl_alarm_t alarm_data;
static time_t alarm_time = 0;
static time_t last_alarm_time = 0;
static rtcctl_alarm_term_t ext_alarms[ RTCCTL_MAX_EXT_ALARMS ];
static size_t ext_alarm_count = 0;

bool rtcctl_powermgm_event_cb( EventBits_t event, void *arg );
bool rtcctl_powermgm_loop_cb( EventBits_t event, void *arg );
bool rtcctl_timesync_event_cb( EventBits_t event, void *arg );
bool rtcctl_send_event_cb( EventBits_t event );
void rtcctl_load_data( void );
void rtcctl_store_data( void );

callback_t *rtcctl_callback = NULL;

void rtcctl_setup( void ) {
#ifdef NATIVE_64BIT

#else
    #if defined( M5PAPER )
        M5.RTC.begin();
    #elif defined( M5CORE2 )
        M5.Rtc.begin();
    #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
        /**
         * fix issue #276
         * disable timer and clk if enabled from older projects
         */
        TTGOClass *ttgo = TTGOClass::getWatch();
        if ( ttgo->rtc->isTimerActive() || ttgo->rtc->isTimerEnable() ) {
            log_d("clear/disable rtc timer");
            ttgo->rtc->clearTimer();
            ttgo->rtc->disableTimer();
        }
        ttgo->rtc->disableCLK();

        pinMode( RTC_INT_PIN, INPUT_PULLUP);
        attachInterrupt( RTC_INT_PIN, &rtcctl_irq, FALLING );
    #elif defined( LILYGO_WATCH_2021 )
        #include <twatch2021_config.h>

        rtc.begin();
        if ( rtc.isTimerActive() || rtc.isTimerEnable() ) {
            log_d("clear/disable rtc timer");
            rtc.clearTimer();
            rtc.disableTimer();
        }
        rtc.disableCLK();

        #if defined( VERSION_2 )
//            pinMode( RTC_Int, INPUT);
//            attachInterrupt( RTC_Int, &rtcctl_irq, GPIO_INTR_POSEDGE );
        #endif
    #elif defined( WT32_SC01 )

    #endif
#endif

    powermgm_register_cb( POWERMGM_SILENCE_WAKEUP | POWERMGM_STANDBY | POWERMGM_WAKEUP | POWERMGM_ENABLE_INTERRUPTS | POWERMGM_DISABLE_INTERRUPTS , rtcctl_powermgm_event_cb, "powermgm rtcctl" );
    powermgm_register_loop_cb( POWERMGM_SILENCE_WAKEUP | POWERMGM_STANDBY | POWERMGM_WAKEUP, rtcctl_powermgm_loop_cb, "powermgm rtcctl loop" );
    timesync_register_cb( TIME_SYNC_OK, rtcctl_timesync_event_cb, "timesync rtcctl" );

    rtcctl_load_data();
}

bool rtcctl_send_event_cb( EventBits_t event ) {
    return( callback_send( rtcctl_callback, event, (void*)NULL ) );
}

static void alarm_hw_clear( void ) {
#ifdef NATIVE_64BIT

#else
    #if defined( M5PAPER )
//        M5.RTC.begin();
    #elif defined( M5CORE2 )

    #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
        TTGOClass *ttgo = TTGOClass::getWatch();
        ttgo->rtc->setAlarm( PCF8563_NO_ALARM, PCF8563_NO_ALARM, PCF8563_NO_ALARM, PCF8563_NO_ALARM );
    #elif defined( LILYGO_WATCH_2021 )
        rtc.setAlarm( PCF8563_NO_ALARM, PCF8563_NO_ALARM, PCF8563_NO_ALARM, PCF8563_NO_ALARM );
    #elif defined( WT32_SC01 )

    #else
        #warning "no alarm rtcctl function"
    #endif
#endif
}

static void alarm_hw_set( int hour, int minute, int mday ) {
#ifdef NATIVE_64BIT

#else
    #if defined( M5PAPER )

    #elif defined( M5CORE2 )

    #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
        /*
         * it is better define alarm by day in month rather than weekday.
         * This way will be work-around an error in pcf8563 source and
         * will avoid eaising alarm when there is only one alarm in the week (today) and alarm time is set to now
         */
        TTGOClass *ttgo = TTGOClass::getWatch();
        ttgo->rtc->setAlarm( hour, minute, mday, PCF8563_NO_ALARM );
    #elif defined( LILYGO_WATCH_2021 )
        rtc.setAlarm( hour, minute, mday, PCF8563_NO_ALARM );
    #elif defined( WT32_SC01 )

    #else
        #warning "no alarm rtcctl function"
    #endif
#endif
}

static void alarm_hw_disable( void ) {
#ifdef NATIVE_64BIT

#else
    #if defined( M5PAPER )
//        M5.RTC.clearIRQ();
    #elif defined( M5CORE2 )

    #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
        TTGOClass *ttgo = TTGOClass::getWatch();
        ttgo->rtc->disableAlarm();
    #elif defined( LILYGO_WATCH_2021 )
        rtc.disableAlarm();
    #elif defined( WT32_SC01 )

    #else
        #warning "no alarm rtcctl function"
    #endif
#endif
}

static void alarm_hw_enable( void ) {
#ifdef NATIVE_64BIT

#else
    #if defined( M5PAPER )
//        M5.RTC.setAlarmIRQ();
    #elif defined( M5CORE2 )

    #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
        TTGOClass *ttgo = TTGOClass::getWatch();
        ttgo->rtc->enableAlarm();
    #elif defined( LILYGO_WATCH_2021 )
        rtc.enableAlarm();
    #elif defined( WT32_SC01 )

    #else
        #warning "no alarm rtcctl function"
    #endif
#endif
}

static bool is_any_day_enabled( const bool week_days[] ) {
    for (int index = 0; index < DAYS_IN_WEEK; ++index){
        if (week_days[index])
            return true;
    }
    return false;
}

static bool is_day_checked( const bool week_days[], int wday ) {
    // No day checked mean ALL days
    return week_days[wday] || !is_any_day_enabled( week_days );
}

static time_t find_next_alarm_day( const bool week_days[], int day_of_week, time_t now ) {
    time_t ret_val = now;
    int wday_index = day_of_week;
    do {
        ret_val += 60 * 60 * 24;
        if (++wday_index == DAYS_IN_WEEK){
            wday_index = 0;
        }
        if (is_day_checked( week_days, wday_index )){
            return ret_val;
        }
    } while (wday_index != day_of_week);

    return ret_val;
}

/**
 * @brief get the next occurrence of a term
 *
 * @return  alarm time or 0 if the term is not enabled
 */
static time_t calc_next_term( const rtcctl_alarm_term_t *term, time_t now ) {
    if ( !term->enabled )
        return( 0 );

    struct tm alarm_tm;
    time_t term_time = now;

    localtime_r( &term_time, &alarm_tm );
    alarm_tm.tm_hour = term->hour;
    alarm_tm.tm_min = term->minute;
    alarm_tm.tm_sec = 0;
    term_time = mktime( &alarm_tm );

    if ( term_time <= now || !is_day_checked( term->week_days, alarm_tm.tm_wday ) )
        term_time = find_next_alarm_day( term->week_days, alarm_tm.tm_wday, term_time );

    return( term_time );
}

/**
 * @brief write the earliest term over all sources into the alarm register
 */
static void set_next_alarm( void ) {
    rtcctl_alarm_term_t local_term;
    struct tm alarm_tm;
    time_t now;
    time_t term;

    time( &now );
    alarm_time = 0;
    
    if ( last_alarm_time > now )
        now = last_alarm_time;

    local_term.enabled = alarm_data.enabled;
    local_term.hour = alarm_data.hour;
    local_term.minute = alarm_data.minute;
    for ( int index = 0 ; index < DAYS_IN_WEEK ; index++ )
        local_term.week_days[ index ] = alarm_data.week_days[ index ];

    term = calc_next_term( &local_term, now );
    if ( term && ( !alarm_time || term < alarm_time ) )
        alarm_time = term;

    for ( size_t index = 0 ; index < ext_alarm_count ; index++ ) {
        term = calc_next_term( &ext_alarms[ index ], now );
        if ( term && ( !alarm_time || term < alarm_time ) )
            alarm_time = term;
    }

    if ( !alarm_time ) {
        alarm_hw_clear();
        rtcctl_send_event_cb( RTCCTL_ALARM_TERM_SET );
        return;
    }

    /*
     * convert local alarm time into GMT0 alarm time, it is necessary sine rtc store time in GMT0
     */
    localtime_r( &alarm_time, &alarm_tm );
    log_d("next local alarm time: %02d:%02d day: %d", alarm_tm.tm_hour, alarm_tm.tm_min, alarm_tm.tm_mday );
    gmtime_r( &alarm_time, &alarm_tm );
    log_d("next GMT0 alarm time: %02d:%02d day %d", alarm_tm.tm_hour, alarm_tm.tm_min, alarm_tm.tm_mday );

    alarm_hw_set( alarm_tm.tm_hour, alarm_tm.tm_min, alarm_tm.tm_mday );
    rtcctl_send_event_cb( RTCCTL_ALARM_TERM_SET );
}

/**
 * @brief recalculate the alarm register and report the armed state if it changed
 *
 * @param   was_armed       armed state before the sources have been changed
 */
static void rtcctl_rearm( bool was_armed ) {
    if ( was_armed )
        alarm_hw_disable();

    set_next_alarm();

    if ( alarm_time )
        alarm_hw_enable();

    if ( was_armed && !alarm_time )
        rtcctl_send_event_cb( RTCCTL_ALARM_DISABLED );
    else if ( !was_armed && alarm_time )
        rtcctl_send_event_cb( RTCCTL_ALARM_ENABLED );
}

void rtcctl_set_next_alarm( void ) {
    rtcctl_rearm( alarm_time != 0 );
}

void rtcctl_set_ext_alarms( const rtcctl_alarm_term_t *terms, size_t count ) {
    bool was_armed = ( alarm_time != 0 );

    if ( !terms )
        count = 0;

    if ( count > RTCCTL_MAX_EXT_ALARMS )
        count = RTCCTL_MAX_EXT_ALARMS;

    for ( size_t index = 0 ; index < count ; index++ )
        ext_alarms[ index ] = terms[ index ];

    ext_alarm_count = count;

    rtcctl_rearm( was_armed );
}

bool rtcctl_powermgm_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case POWERMGM_STANDBY:          log_d("go standby");
                                        #ifdef NATIVE_64BIT

                                        #else
                                            #if defined( M5PAPER )
                                            #elif defined( M5CORE2 )
                                            #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
                                                gpio_wakeup_enable( (gpio_num_t)RTC_INT_PIN, GPIO_INTR_LOW_LEVEL );
                                                esp_sleep_enable_gpio_wakeup ();
                                            #elif defined( LILYGO_WATCH_2021 ) && defined( VERSION_2 )
                                                // gpio_wakeup_enable( (gpio_num_t)RTC_Int, GPIO_INTR_POSEDGE );
                                                // esp_sleep_enable_gpio_wakeup ();
                                            #elif defined( WT32_SC01 )
                                            #else
                                                #warning "no rtcctl powermgm standby event"
                                            #endif
                                        #endif
                                        break;
        case POWERMGM_WAKEUP:           log_d("go wakeup");
                                        break;
        case POWERMGM_SILENCE_WAKEUP:   log_d("go silence wakeup");
                                        break;
        case POWERMGM_ENABLE_INTERRUPTS:
                                        #ifdef NATIVE_64BIT

                                        #else
                                            #if defined( M5PAPER )
                                            #elif defined( M5CORE2 )
                                            #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
                                                attachInterrupt( RTC_INT_PIN, &rtcctl_irq, FALLING );
                                            #elif defined( LILYGO_WATCH_2021 )
                                            #elif defined( WT32_SC01 )
                                            #else
                                                #warning "no rtcctl powermgm enable interrupts event"
                                            #endif
                                        #endif
                                        break;
        case POWERMGM_DISABLE_INTERRUPTS:
                                        #ifdef NATIVE_64BIT

                                        #else
                                            #if defined( M5PAPER )
                                            #elif defined( M5CORE2 )
                                            #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
                                                detachInterrupt( RTC_INT_PIN );
                                            #elif defined( LILYGO_WATCH_2021 )
                                            #elif defined( WT32_SC01 )
                                            #else
                                                #warning "no rtcctl powermgm disable interrupts event"
                                            #endif
                                        #endif
                                        break;
    }
    return( true );
}

bool rtcctl_powermgm_loop_cb( EventBits_t event, void *arg ) {
    bool temp_rtc_irq_flag = false;

#ifndef NATIVE_64BIT
    portENTER_CRITICAL( &RTC_IRQ_Mux );
    temp_rtc_irq_flag = rtc_irq_flag;
    rtc_irq_flag = false;
    portEXIT_CRITICAL( &RTC_IRQ_Mux );
#endif
    if ( temp_rtc_irq_flag ) {
        #if defined( LILYGO_WATCH_2021 ) && defined( VERSION_2 )
            if( rtc.status2() & PCF8563_ALARM_AF ) {
                last_alarm_time = alarm_time;
                rtcctl_send_event_cb( RTCCTL_ALARM_OCCURRED );
                rtcctl_set_next_alarm();
            }
        #else
                last_alarm_time = alarm_time;
                rtcctl_send_event_cb( RTCCTL_ALARM_OCCURRED );
                rtcctl_set_next_alarm();
        #endif
    }
    return( true );
}

bool rtcctl_timesync_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case TIME_SYNC_OK:
            rtcctl_set_next_alarm();
            break;
    }
    return( true );
}

bool rtcctl_register_cb( EventBits_t event, CALLBACK_FUNC callback_func, const char *id ) {
    if ( rtcctl_callback == NULL ) {
        rtcctl_callback = callback_init( "rtctl" );
        if ( rtcctl_callback == NULL ) {
            log_e("rtcctl callback alloc failed");
            while(true);
        }
    }    
    return( callback_register( rtcctl_callback, event, callback_func, id ) );
}

void rtcctl_load_data( void ) {
    rtcctl_alarm_t stored_data;
    stored_data.load();
    rtcctl_set_alarm(&stored_data);
}

void rtcctl_store_data( void ) {
    alarm_data.save();
}

void rtcctl_set_alarm( rtcctl_alarm_t *data ) {
    bool was_armed = ( alarm_time != 0 );

    alarm_data = *data;
    rtcctl_store_data();

    rtcctl_rearm( was_armed );
}

rtcctl_alarm_t *rtcctl_get_alarm_data( void ) {
    return &alarm_data;
}

time_t rtcctl_get_next_alarm_time( void ) {
    return alarm_time;
}

time_t rtcctl_get_last_alarm_time( void ) {
    return last_alarm_time;
}

int rtcctl_get_next_alarm_week_day( void ) {
    if (!alarm_time){
        return RTCCTL_ALARM_NOT_SET;
    }
    tm alarm_tm;
    localtime_r(&alarm_time, &alarm_tm);
    return alarm_tm.tm_wday;
}

void rtcctl_syncToSystem( void ) {
    #ifdef NATIVE_64BIT
    
    #else
        #if defined( M5PAPER )
            struct tm t_tm;
            struct timeval val;
            /**
             * get GMT0 RTC time
             */
            rtc_time_t RTCtime;
            rtc_date_t RTCDate;

            M5.RTC.getTime(&RTCtime);
            M5.RTC.getDate(&RTCDate);

            t_tm.tm_hour = RTCtime.hour;
            t_tm.tm_min = RTCtime.min;
            t_tm.tm_sec = RTCtime.sec;
            t_tm.tm_year = RTCDate.year - 1900;    //Year, whose value starts from 1900
            t_tm.tm_mon = RTCDate.mon - 1;       //Month (starting from January, 0 for January) - Value range is [0,11]
            t_tm.tm_mday = RTCDate.day;
            val.tv_sec = mktime(&t_tm);
            val.tv_usec = 0;
            settimeofday(&val, NULL);
        #elif defined( M5CORE2 )
            struct tm t_tm;
            struct timeval val;
            /**
             * get GMT0 RTC time
             */
            
            RTC_TimeTypeDef RTCtime;
            RTC_DateTypeDef RTCDate;

            M5.Rtc.GetTime( &RTCtime );
            M5.Rtc.GetDate( &RTCDate );

            t_tm.tm_hour = RTCtime.Hours;
            t_tm.tm_min = RTCtime.Minutes;
            t_tm.tm_sec = RTCtime.Seconds;
            t_tm.tm_year = RTCDate.Year - 1900;    //Year, whose value starts from 1900
            t_tm.tm_mon = RTCDate.Month - 1;       //Month (starting from January, 0 for January) - Value range is [0,11]
            t_tm.tm_mday = RTCDate.Date;
            val.tv_sec = mktime(&t_tm);
            val.tv_usec = 0;
            settimeofday(&val, NULL);
        #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
            TTGOClass *ttgo = TTGOClass::getWatch();
            ttgo->rtc->syncToSystem();
        #elif defined( LILYGO_WATCH_2021 )
            rtc.syncToSystem();
        #elif defined( WT32_SC01 )
        #endif
    #endif
}

void rtcctl_syncToRtc( void ) {
    #ifdef NATIVE_64BIT
    
    #else
        #if defined( M5PAPER )
            time_t now;
            struct tm  t_tm;
            /**
             * get GMT0 system time
             */
            time(&now);
            localtime_r(&now, &t_tm);
            /**
             * store GMT0 System time to RTC
             */
            rtc_time_t RTCtime;
            rtc_date_t RTCDate;

            RTCtime.hour = t_tm.tm_hour;
            RTCtime.min = t_tm.tm_min;
            RTCtime.sec = t_tm.tm_sec;
            M5.RTC.setTime(&RTCtime);

            RTCDate.year = t_tm.tm_year + 1900;
            RTCDate.mon = t_tm.tm_mon + 1;
            RTCDate.day = t_tm.tm_mday;
            M5.RTC.setDate(&RTCDate);
        #elif defined( M5CORE2 )
            time_t now;
            struct tm  t_tm;
            /**
             * get GMT0 system time
             */
            time(&now);
            localtime_r(&now, &t_tm);
            /**
             * store GMT0 System time to RTC
             */
            RTC_TimeTypeDef RTCtime;
            RTC_DateTypeDef RTCDate;

            RTCtime.Hours = t_tm.tm_hour;
            RTCtime.Minutes = t_tm.tm_min;
            RTCtime.Seconds = t_tm.tm_sec;
            M5.Rtc.SetTime( &RTCtime );

            RTCDate.Year = t_tm.tm_year + 1900;
            RTCDate.Month = t_tm.tm_mon + 1;
            RTCDate.Date = t_tm.tm_mday;
            M5.Rtc.SetDate( &RTCDate );
        #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
            TTGOClass *ttgo = TTGOClass::getWatch();
            ttgo->rtc->syncToRtc();
        #elif defined( LILYGO_WATCH_2021 )
            rtc.syncToRtc();
        #elif defined( WT32_SC01 )
        #endif
    #endif
}