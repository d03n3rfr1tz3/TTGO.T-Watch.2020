/****************************************************************************
 *   Aug 20 22:14:00 2026
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

#include "micctl.h"
#include "powermgm.h"
#include "callback.h"

float micctl_dbfs_to_spl( float dbfs ) {
    return( dbfs + MICCTL_SPL_OFFSET );
}

#ifdef HARDWARE_HAS_MIC

#include "TTGO.h"
#include <driver/i2s.h>

bool micctl_running = false;
uint32_t micctl_sample_rate = MICCTL_DEFAULT_SAMPLE_RATE;
uint32_t micctl_stop_request = 0;

callback_t *micctl_callback = NULL;

bool micctl_powermgm_event_cb( EventBits_t event, void *arg );
bool micctl_powermgm_loop_cb( EventBits_t event, void *arg );
bool micctl_send_event_cb( EventBits_t event, void *arg );
static void micctl_stop_now( void );

void micctl_setup( void ) {
    powermgm_register_cb( POWERMGM_STANDBY, micctl_powermgm_event_cb, "powermgm micctl" );
    powermgm_register_loop_cb( POWERMGM_WAKEUP, micctl_powermgm_loop_cb, "powermgm micctl loop" );
}

bool micctl_get_available( void ) {
    return( true );
}

bool micctl_start( uint32_t sample_rate ) {
    if ( micctl_stop_request ) {
        micctl_stop_request = 0;
        micctl_send_event_cb( MICCTL_START_REQUEST, (void *)&sample_rate );
    }

    if ( micctl_running ) {
        if ( micctl_sample_rate == sample_rate )
            return( true );
        micctl_stop_now();
    }
    /**
     * PDM RX is only available on I2S0
     */
    i2s_config_t i2s_config;
    memset( &i2s_config, 0, sizeof( i2s_config ) );
    i2s_config.mode = (i2s_mode_t)( I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM );
    i2s_config.sample_rate = sample_rate;
    i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_config.communication_format = (i2s_comm_format_t)( I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB );
    i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2s_config.dma_buf_count = 4;
    i2s_config.dma_buf_len = 256;

    if ( i2s_driver_install( I2S_NUM_0, &i2s_config, 0, NULL ) != ESP_OK ) {
        log_e("i2s driver install failed");
        return( false );
    }

    i2s_pin_config_t pin_config;
    pin_config.bck_io_num = I2S_PIN_NO_CHANGE;
    pin_config.ws_io_num = TWATCH_PDM_MIC_CLK;
    pin_config.data_out_num = I2S_PIN_NO_CHANGE;
    pin_config.data_in_num = TWATCH_PDM_MIC_DATA;

    if ( i2s_set_pin( I2S_NUM_0, &pin_config ) != ESP_OK ) {
        log_e("i2s set pin failed");
        i2s_driver_uninstall( I2S_NUM_0 );
        return( false );
    }

    i2s_set_clk( I2S_NUM_0, sample_rate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO );
    i2s_set_pdm_rx_down_sample( I2S_NUM_0, I2S_PDM_DSR_16S );

    micctl_sample_rate = sample_rate;
    micctl_running = true;
    log_i("mic started, %d hz pcm, %d hz pdm clock", sample_rate, sample_rate * 128 );

    micctl_send_event_cb( MICCTL_START, (void *)&micctl_sample_rate );

    return( true );
}

void micctl_stop( void ) {
    if ( !micctl_running || micctl_stop_request )
        return;

    micctl_stop_request = millis();
    micctl_send_event_cb( MICCTL_STOP_REQUEST, NULL );
}

static void micctl_stop_now( void ) {
    micctl_stop_request = 0;

    if ( !micctl_running )
        return;

    i2s_driver_uninstall( I2S_NUM_0 );
    micctl_running = false;
    log_i("mic stopped");

    micctl_send_event_cb( MICCTL_STOP, NULL );
}

bool micctl_is_running( void ) {
    return( micctl_running );
}

size_t micctl_read( int16_t *buffer, size_t max_samples, uint32_t timeout_ms ) {
    size_t bytes_read = 0;

    if ( !micctl_running || !buffer || !max_samples )
        return( 0 );

    if ( i2s_read( I2S_NUM_0, (void *)buffer, max_samples * sizeof( int16_t ), &bytes_read, timeout_ms / portTICK_PERIOD_MS ) != ESP_OK )
        return( 0 );

    return( bytes_read / sizeof( int16_t ) );
}

bool micctl_powermgm_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case POWERMGM_STANDBY:      micctl_stop_now();
                                    break;
    }
    return( true );
}

bool micctl_powermgm_loop_cb( EventBits_t event, void *arg ) {
    if ( micctl_stop_request && millis() - micctl_stop_request >= MICCTL_STOP_HOLD_TIME )
        micctl_stop_now();

    return( true );
}

bool micctl_register_cb( EventBits_t event, CALLBACK_FUNC callback_func, const char *id ) {
    if ( micctl_callback == NULL ) {
        micctl_callback = callback_init( "micctl" );
        if ( micctl_callback == NULL ) {
            log_e("micctl callback alloc failed");
            while( true );
        }
    }

    return( callback_register( micctl_callback, event, callback_func, id ) );
}

bool micctl_send_event_cb( EventBits_t event, void *arg ) {
    return( callback_send( micctl_callback, event, arg ) );
}

#else

void micctl_setup( void ) {
}

bool micctl_get_available( void ) {
    return( false );
}

bool micctl_start( uint32_t sample_rate ) {
    return( false );
}

void micctl_stop( void ) {
}

bool micctl_is_running( void ) {
    return( false );
}

size_t micctl_read( int16_t *buffer, size_t max_samples, uint32_t timeout_ms ) {
    return( 0 );
}

bool micctl_register_cb( EventBits_t event, CALLBACK_FUNC callback_func, const char *id ) {
    return( true );
}

#endif // HARDWARE_HAS_MIC
