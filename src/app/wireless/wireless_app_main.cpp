/****************************************************************************
 *   linuxthor 2020
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
#ifndef NATIVE_64BIT
    #include <Arduino.h>
#endif
#include <math.h>
#include <lwip/sockets.h>
#include <WiFi.h>

#include "esp_wifi.h"

#include "hardware/wifictl.h"
#include "hardware/powermgm.h"

#include "wireless_app.h"
#include "wireless_app_main.h"

#include "gui/mainbar/app_tile/app_tile.h"
#include "gui/mainbar/main_tile/main_tile.h"
#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/gui.h"
#include "gui/keyboard.h"
#include "gui/widget_styles.h"

lv_obj_t *wireless_app_main_tile = NULL;
lv_style_t wireless_app_main_style;
static lv_obj_t * throbber = NULL;
static lv_task_t * _wireless_app_task = NULL;

static bool wireless_wifi_owned = false;                /** @brief app has taken over the wifi driver */
static bool wireless_wifi_state = false;                /** @brief wifi state before the app took over */
static bool wireless_wifi_off = false;                  /** @brief wifictl has really switched the wifi off */
static uint8_t wireless_wifi_off_timeout = 0;
static bool wireless_ap_active = false;                 /** @brief our beacon ap is up */
static volatile bool wireless_spam_running = false;
static volatile bool wireless_spam_abort = false;

LV_IMG_DECLARE(wireless_app_32px);
LV_IMG_DECLARE(exit_32px);
LV_IMG_DECLARE(refresh_32px);
LV_FONT_DECLARE(Ubuntu_72px);

static void exit_wireless_app_main_event_cb( lv_obj_t * obj, lv_event_t event );
static void enter_wireless_app_next_event_cb( lv_obj_t * obj, lv_event_t event );
static void wireless_activate_cb( void );
static void wireless_hibernate_cb( void );
static bool wireless_wifictl_event_cb( EventBits_t event, void *arg );
static bool wireless_ap_start( void );
static void wireless_wifi_release( void );
void spam_task( void *pvParameter );
void wireless_app_task( lv_task_t * task );

esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer, int len, bool en_sys_seq);

uint8_t beacon_raw[] = {
	0x80, 0x00,							// 0-1: Frame Control
	0x00, 0x00,							// 2-3: Duration
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff,				// 4-9: Destination address (broadcast)
	0xba, 0xde, 0xaf, 0xfe, 0x00, 0x06,				// 10-15: Source address
	0xba, 0xde, 0xaf, 0xfe, 0x00, 0x06,				// 16-21: BSSID
	0x00, 0x00,							// 22-23: Sequence / fragment number
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,			// 24-31: Timestamp (GETS OVERWRITTEN TO 0 BY HARDWARE)
	0x64, 0x00,							// 32-33: Beacon interval
	0x31, 0x04,							// 34-35: Capability info
	0x00, 0x00, /* FILL CONTENT HERE */				// 36-38: SSID parameter set, 0x00:length:content
	0x01, 0x08, 0x82, 0x84,	0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,	// 39-48: Supported rates
	0x03, 0x01, 0x01,						// 49-51: DS Parameter set, current channel 1 (= 0x01),
	0x05, 0x04, 0x01, 0x02, 0x00, 0x00,				// 52-57: Traffic Indication Map	
};

char *rick_ssids[] = {
	"01 Never gonna give you up",
	"02 Never gonna let you down",
	"03 Never gonna run around",
	"04 and desert you",
	"05 Never gonna make you cry",
	"06 Never gonna say goodbye",
	"07 Never gonna tell a lie",
	"08 and hurt you"
};

#define BEACON_SSID_OFFSET 38
#define SRCADDR_OFFSET 10
#define BSSID_OFFSET 16
#define SEQNUM_OFFSET 22
#define DSCHANNEL_OFFSET 50
#define TOTAL_LINES (sizeof(rick_ssids) / sizeof(char *))

void wireless_app_main_setup( uint32_t tile_num ) {

    wireless_app_main_tile = mainbar_get_tile_obj( tile_num );
    lv_style_copy( &wireless_app_main_style, ws_get_mainbar_style() );

    lv_obj_t * exit_btn = lv_imgbtn_create( wireless_app_main_tile, NULL);
    lv_imgbtn_set_src(exit_btn, LV_BTN_STATE_RELEASED, &exit_32px);
    lv_imgbtn_set_src(exit_btn, LV_BTN_STATE_PRESSED, &exit_32px);
    lv_imgbtn_set_src(exit_btn, LV_BTN_STATE_CHECKED_RELEASED, &exit_32px);
    lv_imgbtn_set_src(exit_btn, LV_BTN_STATE_CHECKED_PRESSED, &exit_32px);
    lv_obj_add_style(exit_btn, LV_IMGBTN_PART_MAIN, &wireless_app_main_style );
    lv_obj_align(exit_btn, wireless_app_main_tile, LV_ALIGN_IN_BOTTOM_LEFT, 10, -10 );
    lv_obj_set_event_cb( exit_btn, exit_wireless_app_main_event_cb );

    lv_obj_t * next_btn = lv_imgbtn_create( wireless_app_main_tile, NULL);
    lv_imgbtn_set_src(next_btn, LV_BTN_STATE_RELEASED, &wireless_app_32px);
    lv_imgbtn_set_src(next_btn, LV_BTN_STATE_PRESSED, &wireless_app_32px);
    lv_imgbtn_set_src(next_btn, LV_BTN_STATE_CHECKED_RELEASED, &wireless_app_32px);
    lv_imgbtn_set_src(next_btn, LV_BTN_STATE_CHECKED_PRESSED, &wireless_app_32px);
    lv_obj_add_style(next_btn, LV_IMGBTN_PART_MAIN, &wireless_app_main_style );
    lv_obj_align(next_btn, wireless_app_main_tile, LV_ALIGN_IN_BOTTOM_LEFT, (LV_HOR_RES / 2) -5 , -10 );
    lv_obj_set_event_cb( next_btn, enter_wireless_app_next_event_cb );

    mainbar_add_tile_activate_cb( tile_num, wireless_activate_cb );
    mainbar_add_tile_hibernate_cb( tile_num, wireless_hibernate_cb );
    wifictl_register_cb( WIFICTL_ON | WIFICTL_OFF, wireless_wifictl_event_cb, "wireless main" );
}

/**
 * @brief keep track of the wifi state while the app does not own the driver.
 */
static bool wireless_wifictl_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case WIFICTL_ON:    if ( !wireless_wifi_owned )
                                wireless_wifi_state = true;
                            break;
        case WIFICTL_OFF:   if ( wireless_wifi_owned )
                                wireless_wifi_off = true;
                            else
                                wireless_wifi_state = false;
                            break;
    }
    return( true );
}

/**
 * @brief bring up our own beacon ap.
 */
static bool wireless_ap_start( void ) {
    if ( !WiFi.mode( WIFI_MODE_STA ) ) {
        log_e("start beacon radio failed: set mode");
        return( false );
    }
    esp_wifi_set_ps( WIFI_PS_MIN_MODEM );
    wireless_ap_active = true;
    log_d("start beacon radio");
    return( true );
}

/**
 * @brief tear down our own beacon ap and hand the wifi back to wifictl.
 */
static void wireless_wifi_release( void ) {
    if ( wireless_ap_active ) {
        WiFi.mode( WIFI_OFF );
        wireless_ap_active = false;
    }
    if ( throbber ) {
        lv_obj_del( throbber );
        throbber = NULL;
    }
    if ( !wireless_wifi_owned )
        return;
    wireless_wifi_owned = false;
    wifictl_block_scan( false );
    /**
     * never switch the wifi on while going to standby
     */
    if ( powermgm_get_event( POWERMGM_STANDBY ) )
        return;
    /**
     * restore wifi state, exactly one request
     */
    if ( wireless_wifi_state )
        wifictl_on();
    else
        wifictl_off();
}

static void wireless_activate_cb( void ) {
    if ( !_wireless_app_task )
        _wireless_app_task = lv_task_create( wireless_app_task, 1000, LV_TASK_PRIO_MID, NULL );
}

static void wireless_hibernate_cb( void ) {
    if ( _wireless_app_task ) {
        lv_task_del( _wireless_app_task );
        _wireless_app_task = NULL;
    }
    wireless_spam_abort = true;
    
    for ( uint8_t i = 0 ; i < 50 && wireless_spam_running ; i++ )
        delay( 1 );

    wireless_wifi_release();
}

void wireless_app_task( lv_task_t * task ) {
    /**
     * start the beacon ap as soon as wifictl really switched the wifi off
     */
    if ( wireless_wifi_owned && !wireless_ap_active ) {
        /**
         * stop pressed before the session even started, hand the wifi back
         */
        if ( wireless_spam_abort ) {
            wireless_wifi_release();
            return;
        }
        if ( !wireless_wifi_off && wireless_wifi_off_timeout ) {
            wireless_wifi_off_timeout--;
            return;
        }
        if ( !wireless_ap_start() ) {
            wireless_wifi_release();
            return;
        }
        wireless_spam_abort = false;
        wireless_spam_running = true;
        if ( xTaskCreate( &spam_task, "spam_task", 2560, NULL, 5, NULL ) != pdPASS ) {
            wireless_spam_running = false;
            wireless_wifi_release();
        }
        return;
    }
    /**
     * spam_task has finished on its own, drop the spinner and give the wifi back
     */
    if ( wireless_ap_active && !wireless_spam_running )
        wireless_wifi_release();
}

void spam_task(void *pvParameter) {
	uint8_t line = 0;
	uint8_t channel = 1;
	uint32_t tx_ok = 0, tx_fail = 0;

	// Keep track of beacon sequence numbers on a per-songline-basis
	uint16_t seqnum[TOTAL_LINES] = { 0 };

	esp_wifi_set_channel( channel, WIFI_SECOND_CHAN_NONE );
	beacon_raw[DSCHANNEL_OFFSET] = channel;

	while ( !wireless_spam_abort )
    {
		vTaskDelay(10);

		// Insert line of Rick Astley's "Never Gonna Give You Up" into beacon packet
		log_d("%i %i %s", strlen(rick_ssids[line]), TOTAL_LINES, rick_ssids[line]);

		uint8_t beacon_rick[200];
		memcpy(beacon_rick, beacon_raw, BEACON_SSID_OFFSET - 1);
		beacon_rick[BEACON_SSID_OFFSET - 1] = strlen(rick_ssids[line]);
		memcpy(&beacon_rick[BEACON_SSID_OFFSET], rick_ssids[line], strlen(rick_ssids[line]));
		memcpy(&beacon_rick[BEACON_SSID_OFFSET + strlen(rick_ssids[line])], &beacon_raw[BEACON_SSID_OFFSET], sizeof(beacon_raw) - BEACON_SSID_OFFSET);

		// Last byte of source address / BSSID will be line number - emulate multiple APs broadcasting one song line each
		beacon_rick[SRCADDR_OFFSET + 5] = line;
		beacon_rick[BSSID_OFFSET + 5] = line;

		// Update sequence number
		beacon_rick[SEQNUM_OFFSET] = (seqnum[line] & 0x0f) << 4;
		beacon_rick[SEQNUM_OFFSET + 1] = (seqnum[line] & 0xff0) >> 4;
		seqnum[line]++;
		if (seqnum[line] > 0xfff)
			seqnum[line] = 0;

		if ( esp_wifi_80211_tx(WIFI_IF_STA, beacon_rick, sizeof(beacon_raw) + strlen(rick_ssids[line]), false) == ESP_OK )
			tx_ok++;
		else
			tx_fail++;

		// after a full pass through all song lines, hop to the next channel
		if (++line >= TOTAL_LINES) {
			line = 0;
			if (++channel > 13)
				channel = 1;
			esp_wifi_set_channel( channel, WIFI_SECOND_CHAN_NONE );
			beacon_raw[DSCHANNEL_OFFSET] = channel;     // keep DS byte in sync with the radio
		}
	}
    log_d("beacon tx: %u ok, %u failed", tx_ok, tx_fail );
    wireless_spam_running = false;
    vTaskDelete(NULL);
}

static void enter_wireless_app_next_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {	
                case(LV_EVENT_CLICKED):

                    /**
                     * second press stops the running beacon session
                     */
                    if ( wireless_wifi_owned ) {
                        wireless_spam_abort = true;
                        break;
                    }
                    /**
                     * take over the wifi driver
                     */
                    wireless_wifi_owned = true;
                    wireless_spam_abort = false;
                    wifictl_block_scan( true );
                    wireless_wifi_off = !wireless_wifi_state;
                    wireless_wifi_off_timeout = 3;
                    wifictl_off();

                    throbber = lv_spinner_create(wireless_app_main_tile, NULL);
                    lv_obj_set_size(throbber, 100, 100);
                    lv_obj_align(throbber, NULL, LV_ALIGN_CENTER, 0, 0);
                    break;
    }
}

static void exit_wireless_app_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_jump_back();
                                        break;
    }
}

