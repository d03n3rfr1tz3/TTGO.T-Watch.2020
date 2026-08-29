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

#include "hardware/wifictl.h"
#include "hardware/display.h"
#include "hardware/powermgm.h"

#include "wifimon_app.h"
#include "wifimon_app_main.h"

#include "gui/mainbar/app_tile/app_tile.h"
#include "gui/mainbar/main_tile/main_tile.h"
#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/keyboard.h"
#include "gui/widget_styles.h"
#include "gui/widget_factory.h"

#ifdef NATIVE_64BIT
    #include <time.h>
    #include "utils/logging.h"
    #include "utils/millis.h"
#else
    #include <Arduino.h>
    #include <math.h>
    #include <lwip/sockets.h>
    #include "esp_wifi.h"

    void wifimon_sniffer_packet_handler( void* buff, wifi_promiscuous_pkt_type_t type );
    static wifi_country_t wifi_country = {.cc="CN", .schan = 1, .nchan = 13};
    static bool wifimon_sniffer_active = false;
#endif

#define WIFIMON_POINTS          32                                      /** @brief seconds of history */
#define WIFIMON_STRIP_HEIGHT    24                                      /** @brief reserved for legend and scale, no curve reaches into it */
#define WIFIMON_LABEL_Y         4
#define WIFIMON_ROLLER_WIDTH    40

#define WIFIMON_FLOOR_COLOR     LV_COLOR_MAKE( 0x00, 0x02, 0x28 )
#define WIFIMON_GRID_COLOR      LV_COLOR_MAKE( 0x18, 0x22, 0x44 )
#define WIFIMON_BORDER_COLOR    LV_COLOR_MAKE( 0x30, 0x3c, 0x60 )
#define WIFIMON_SELECT_COLOR    LV_COLOR_MAKE( 0x2a, 0x3f, 0x6e )
#define WIFIMON_MGMT_COLOR      LV_COLOR_MAKE( 0xf0, 0x10, 0x10 )
#define WIFIMON_DATA_COLOR      LV_COLOR_MAKE( 0x70, 0xd0, 0x20 )
#define WIFIMON_MISC_COLOR      LV_COLOR_MAKE( 0xf0, 0xe0, 0x20 )

static lv_obj_t *wifimon_app_main_tile = NULL;
static lv_obj_t *chart = NULL;
static lv_obj_t *channel_select = NULL;
static lv_obj_t *wifimon_scale_label = NULL;
static lv_chart_series_t *ser1 = NULL;
static lv_chart_series_t *ser2 = NULL;
static lv_chart_series_t *ser3 = NULL;
static lv_task_t *_wifimon_app_task = NULL;
static int wifimon_display_timeout = 0;
static bool wifimon_app_active = false;         /** @brief app owns the wifi driver */
static bool wifimon_wifi_state = false;         /** @brief wifi state before the app took over */
static bool wifimon_wifi_off = false;           /** @brief wifictl has really switched the wifi off */
static uint8_t wifimon_wifi_off_timeout = 0;

static const lv_coord_t wifimon_range_steps[] = { 100, 250, 500, 1000, 2500, 5000 };
static lv_coord_t wifimon_peak[ WIFIMON_POINTS ];
static uint8_t wifimon_peak_pos = 0;
static lv_coord_t wifimon_range = 0;
static uint8_t wifimon_channel = 1;

LV_IMG_DECLARE(exit_dark_48px);
LV_IMG_DECLARE(wifimon_app_32px);
LV_FONT_DECLARE(Ubuntu_72px);

static void exit_wifimon_app_main_event_cb( lv_obj_t * obj, lv_event_t event );
static void wifimon_sniffer_set_channel( uint8_t channel );
static void wifimon_app_task( lv_task_t * task );
static void wifimon_activate_cb( void );
static void wifimon_hibernate_cb( void );
static bool wifimon_wifictl_event_cb( EventBits_t event, void *arg );

static volatile int data = 0, mgmt = 0, misc = 0;

static void wifimon_reset_scale( void ) {
    for( int i = 0 ; i < WIFIMON_POINTS ; i++ )
        wifimon_peak[ i ] = 0;
    wifimon_peak_pos = 0;
    wifimon_range = wifimon_range_steps[ 0 ];
    data = 0;
    mgmt = 0;
    misc = 0;

    if( chart ) {
        lv_chart_set_y_range( chart, LV_CHART_AXIS_PRIMARY_Y, 0, wifimon_range );
        lv_chart_init_points( chart, ser1, 0 );
        lv_chart_init_points( chart, ser2, 0 );
        lv_chart_init_points( chart, ser3, 0 );
        lv_chart_refresh( chart );
        wf_label_printf( wifimon_scale_label, chart, LV_ALIGN_IN_TOP_RIGHT, -THEME_PADDING, WIFIMON_LABEL_Y, "%d", wifimon_range );
    }
}

#ifdef NATIVE_64BIT

#else
void wifimon_sniffer_packet_handler( void* buff, wifi_promiscuous_pkt_type_t type ) {
    switch( type ) {
        case WIFI_PKT_MGMT: 
            mgmt++;
            break;
        case WIFI_PKT_DATA:
            data++; 
            break; 
        default:  
            misc++;
            break;
    }
}
#endif

static void wifimon_sniffer_set_channel( uint8_t channel ) {
#ifdef NATIVE_64BIT

#else
    esp_wifi_set_channel( channel, WIFI_SECOND_CHAN_NONE );
#endif
    log_i("set wifi channel: %d", channel );
}

static void wifimon_channel_select_event_handler( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case LV_EVENT_VALUE_CHANGED: {
            char buf[32];
            lv_roller_get_selected_str( obj, buf, sizeof( buf ) );
            wifimon_channel = atoi( buf );
            wifimon_sniffer_set_channel( wifimon_channel );
            /**
             * do not mix two channels into one sample
             */
            wifimon_reset_scale();
            break;
        }
    }
}

void wifimon_app_main_setup( uint32_t tile_num ) {

    wifimon_app_main_tile = mainbar_get_tile_obj( tile_num );
    /**
     * add chart widget
     */
    chart = lv_chart_create( wifimon_app_main_tile, NULL );
    lv_obj_set_size( chart, lv_disp_get_hor_res( NULL ), lv_disp_get_ver_res( NULL ) );
    lv_obj_align( chart, wifimon_app_main_tile, LV_ALIGN_IN_TOP_LEFT, 0, 0 );
    lv_chart_set_type( chart, LV_CHART_TYPE_LINE );
    lv_chart_set_point_count( chart, WIFIMON_POINTS );
    lv_chart_set_div_line_count( chart, 3, 7 );
    lv_obj_add_style( chart, LV_OBJ_PART_MAIN, APP_STYLE );

    lv_obj_set_style_local_bg_color( chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, WIFIMON_FLOOR_COLOR );
    lv_obj_set_style_local_bg_opa( chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, LV_OPA_COVER );
    lv_obj_set_style_local_border_width( chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_radius( chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, 0 );

    lv_obj_set_style_local_pad_top( chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, WIFIMON_STRIP_HEIGHT );
    lv_obj_set_style_local_pad_bottom( chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, THEME_PADDING );
    lv_obj_set_style_local_pad_left( chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_pad_right( chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_line_color( chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, WIFIMON_GRID_COLOR );
    lv_obj_set_style_local_line_width( chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, 1 );
    lv_obj_set_style_local_line_opa( chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, LV_OPA_COVER );
    lv_obj_set_style_local_size( chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 0 );
    lv_obj_set_style_local_line_width( chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 2 );
    lv_obj_set_style_local_bg_opa( chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_OPA_40 );
    lv_obj_set_style_local_bg_grad_dir( chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_GRAD_DIR_VER );
    lv_obj_set_style_local_bg_main_stop( chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 255 );
    lv_obj_set_style_local_bg_grad_stop( chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 0 );
    /**
     * add chart series
     */
    ser1 = lv_chart_add_series( chart, WIFIMON_MGMT_COLOR );
    ser2 = lv_chart_add_series( chart, WIFIMON_DATA_COLOR );
    ser3 = lv_chart_add_series( chart, WIFIMON_MISC_COLOR );
    lv_chart_set_y_range( chart, LV_CHART_AXIS_PRIMARY_Y, 0, wifimon_range_steps[ 0 ] );
    /**
     * add legend and scale into the reserved top strip
     */
    lv_obj_t * chart_series_label = wf_add_label( chart, "", APP_ICON_LABEL_STYLE );
    lv_label_set_recolor( chart_series_label, true );
    lv_label_set_text( chart_series_label, "#f01010 mgmt#  #70d020 data#  #f0e020 misc#" );
    lv_obj_align( chart_series_label, chart, LV_ALIGN_IN_TOP_LEFT, THEME_PADDING, WIFIMON_LABEL_Y );

    wifimon_scale_label = wf_add_label( chart, "100", APP_ICON_LABEL_STYLE );
    lv_obj_align( wifimon_scale_label, chart, LV_ALIGN_IN_TOP_RIGHT, -THEME_PADDING, WIFIMON_LABEL_Y );
    /**
     * add channel select roller
     */
    channel_select = wf_add_roller( wifimon_app_main_tile, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13", LV_ROLLER_MODE_INIFINITE, 5 );
    lv_obj_set_width( channel_select, WIFIMON_ROLLER_WIDTH );
    lv_obj_align( channel_select, chart, LV_ALIGN_IN_LEFT_MID, THEME_PADDING, 0 );
    lv_obj_set_event_cb( channel_select, wifimon_channel_select_event_handler );
    lv_obj_set_style_local_bg_color( channel_select, LV_ROLLER_PART_BG, LV_STATE_DEFAULT, WIFIMON_FLOOR_COLOR );
    lv_obj_set_style_local_bg_opa( channel_select, LV_ROLLER_PART_BG, LV_STATE_DEFAULT, LV_OPA_60 );
    lv_obj_set_style_local_border_color( channel_select, LV_ROLLER_PART_BG, LV_STATE_DEFAULT, WIFIMON_BORDER_COLOR );
    lv_obj_set_style_local_border_width( channel_select, LV_ROLLER_PART_BG, LV_STATE_DEFAULT, 1 );
    lv_obj_set_style_local_border_opa( channel_select, LV_ROLLER_PART_BG, LV_STATE_DEFAULT, LV_OPA_COVER );
    lv_obj_set_style_local_radius( channel_select, LV_ROLLER_PART_BG, LV_STATE_DEFAULT, 3 );
    lv_obj_set_style_local_text_color( channel_select, LV_ROLLER_PART_BG, LV_STATE_DEFAULT, LV_COLOR_SILVER );
    /**
     * the roller is a page, its scrollable part would cover the chart again
     */
    lv_obj_set_style_local_bg_opa( channel_select, LV_PAGE_PART_SCROLLABLE, LV_STATE_DEFAULT, LV_OPA_TRANSP );
    lv_obj_set_style_local_bg_color( channel_select, LV_ROLLER_PART_SELECTED, LV_STATE_DEFAULT, WIFIMON_SELECT_COLOR );
    lv_obj_set_style_local_bg_opa( channel_select, LV_ROLLER_PART_SELECTED, LV_STATE_DEFAULT, LV_OPA_70 );
    lv_obj_set_style_local_text_color( channel_select, LV_ROLLER_PART_SELECTED, LV_STATE_DEFAULT, LV_COLOR_WHITE );
    /**
     * add exit button last
     */
    lv_obj_t * exit_btn = wf_add_exit_button( wifimon_app_main_tile, exit_wifimon_app_main_event_cb );
    lv_obj_align( exit_btn, wifimon_app_main_tile, LV_ALIGN_IN_BOTTOM_LEFT, THEME_ICON_PADDING, -THEME_ICON_PADDING );

    mainbar_add_slide_element( chart );
    mainbar_add_tile_activate_cb( tile_num, wifimon_activate_cb );
    mainbar_add_tile_hibernate_cb( tile_num, wifimon_hibernate_cb );
    wifictl_register_cb( WIFICTL_ON | WIFICTL_OFF, wifimon_wifictl_event_cb, "wifimon main" );
}

/**
 * @brief keep track of the wifi state while the app is not running.
 */
static bool wifimon_wifictl_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case WIFICTL_ON:    if ( !wifimon_app_active )
                                wifimon_wifi_state = true;
                            break;
        case WIFICTL_OFF:   if ( wifimon_app_active )
                                wifimon_wifi_off = true;
                            else
                                wifimon_wifi_state = false;
                            break;
    }
    return( true );
}

static void exit_wifimon_app_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):     mainbar_jump_back();
                                      break;
    }
}

static void wifimon_hibernate_cb( void ) {
    if(_wifimon_app_task != NULL) {
        lv_task_del(_wifimon_app_task);
        _wifimon_app_task = NULL;
    }  
#ifdef NATIVE_64BIT

#else
    if ( wifimon_sniffer_active ) {
        esp_wifi_set_promiscuous( false );
        esp_wifi_set_promiscuous_rx_cb( NULL );
        esp_wifi_stop();
        wifimon_sniffer_active = false;
    }
#endif
    wifimon_app_active = false;
    wifictl_block_scan( false );
    /**
     * restore wifi state
     */
    if ( !powermgm_get_event( POWERMGM_STANDBY ) ) {
        if ( wifimon_wifi_state )
            wifictl_on();
        else
            wifictl_off();
    }
    /**
     * restore display timeout time
     */
    display_set_timeout( wifimon_display_timeout );
}

static void wifimon_activate_cb( void ) {
    /**
     * take over the wifi driver
     */
    wifimon_app_active = true;
    wifictl_block_scan( true );
    wifimon_wifi_off = !wifimon_wifi_state;
    wifimon_wifi_off_timeout = 3;
    wifictl_off();
    /**
     * start with an empty chart and the lowest scale
     */
    wifimon_channel = 1;
    wifimon_reset_scale();
    /**
     * start stats fetch task
     */
    _wifimon_app_task = lv_task_create( wifimon_app_task, 1000, LV_TASK_PRIO_MID, NULL );
    /**
     * save display timeout time
     */
    wifimon_display_timeout = display_get_timeout();
    display_set_timeout( DISPLAY_MAX_TIMEOUT );
}

static void wifimon_app_task( lv_task_t * task ) {
#ifndef NATIVE_64BIT
    /**
     * setup promiscuous mode as soon as wifictl really switched the wifi off
     */
    if ( !wifimon_sniffer_active ) {
        if ( !wifimon_wifi_off && wifimon_wifi_off_timeout ) {
            wifimon_wifi_off_timeout--;
            return;
        }
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init( &cfg );
        esp_wifi_set_country( &wifi_country );
        esp_wifi_set_mode( WIFI_MODE_NULL );
        esp_wifi_start();
        esp_wifi_set_promiscuous( true );
        esp_wifi_set_promiscuous_rx_cb( &wifimon_sniffer_packet_handler );
        lv_roller_set_selected( channel_select, 0, LV_ANIM_OFF );
        wifimon_channel = 1;
        wifimon_sniffer_set_channel( wifimon_channel );
        wifimon_sniffer_active = true;
        return;
    }
#endif
    /**
     * take over the packet counter
     */
    int mgmt_count = mgmt;
    int data_count = data;
    int misc_count = misc;

    data = 0;
    mgmt = 0;
    misc = 0;

    lv_coord_t peak = mgmt_count;
    if( data_count > peak ) peak = data_count;
    if( misc_count > peak ) peak = misc_count;

    wifimon_peak[ wifimon_peak_pos ] = peak;
    wifimon_peak_pos = ( wifimon_peak_pos + 1 ) % WIFIMON_POINTS;
    /**
     * scale to the loudest second in the window, it decays when the burst scrolls out
     */
    lv_coord_t window = 0;
    for( int i = 0 ; i < WIFIMON_POINTS ; i++ )
        if( wifimon_peak[ i ] > window )
            window = wifimon_peak[ i ];

    size_t steps = sizeof( wifimon_range_steps ) / sizeof( wifimon_range_steps[ 0 ] );
    lv_coord_t range = wifimon_range_steps[ steps - 1 ];
    for( size_t i = 0 ; i < steps ; i++ ) {
        if( window <= wifimon_range_steps[ i ] ) {
            range = wifimon_range_steps[ i ];
            break;
        }
    }

    if( range != wifimon_range ) {
        wifimon_range = range;
        lv_chart_set_y_range( chart, LV_CHART_AXIS_PRIMARY_Y, 0, wifimon_range );
        wf_label_printf( wifimon_scale_label, chart, LV_ALIGN_IN_TOP_RIGHT, -THEME_PADDING, WIFIMON_LABEL_Y, "%d", wifimon_range );
    }
    /**
     * add seria data
     */
    if( mgmt_count > wifimon_range ) mgmt_count = wifimon_range;
    if( data_count > wifimon_range ) data_count = wifimon_range;
    if( misc_count > wifimon_range ) misc_count = wifimon_range;

    lv_chart_set_next( chart, ser1, mgmt_count );
    lv_chart_set_next( chart, ser2, data_count );
    lv_chart_set_next( chart, ser3, misc_count );
    /**
     * refresh chart
     */
    lv_chart_refresh( chart );
}

