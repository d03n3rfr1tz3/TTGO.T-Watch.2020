/****************************************************************************
 *   January 04 19:00:00 2022
 *   Copyright  2021  Dirk Sarodnick
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

#include "printer3d_app.h"
#include "printer3d_app_main.h"

#include "gui/gui.h"
#include "gui/mainbar/app_tile/app_tile.h"
#include "gui/mainbar/main_tile/main_tile.h"
#include "gui/mainbar/mainbar.h"
#include "gui/sjpg_decoder/tjpgd.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

#include "hardware/framebuffer.h"
#include "hardware/powermgm.h"
#include "hardware/wifictl.h"
#include "utils/json_psram_allocator.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
    #include "utils/millis.h"
    #include <string>

    using namespace std;
    #define String string
#else
    #include <Arduino.h>
    #include "HTTPClient.h"
    #include "esp_task_wdt.h"
#endif

volatile bool printer3d_state = false;
volatile bool printer3d_open_state = false;
static uint64_t nextmillis = 0;

#ifndef NATIVE_64BIT
    static uint8_t* mjpeg_buffer = nullptr;
    static uint8_t* mjpeg_frame = nullptr;
    static char* mjpeg_url;
#endif

lv_obj_t *printer3d_app_main_tile = NULL;
lv_obj_t *printer3d_app_video_tile = NULL;

lv_task_t * _printer3d_app_task;

lv_style_t printer3d_heading_big_style;
lv_style_t printer3d_heading_small_style;
lv_style_t printer3d_line_meter_style;
lv_obj_t* printer3d_heading_name;
lv_obj_t* printer3d_heading_version;
lv_obj_t* printer3d_progress_linemeter;
lv_obj_t* printer3d_progress_percent;
lv_obj_t* printer3d_progress_state;
lv_obj_t* printer3d_extruder_label;
lv_obj_t* printer3d_extruder_temp;
lv_obj_t* printer3d_printbed_label;
lv_obj_t* printer3d_printbed_temp;
#ifndef NATIVE_64BIT
    lv_style_t printer3d_app_video_style;
    lv_obj_t* printer3d_video_img;

    #define PRINTER3D_MJPEG_STRIP_H          16
    #define PRINTER3D_MJPEG_SCALE_ONE  ( 1 << 16 ) /** @brief q16 step of 1.0, the decoder output is shown as it is */
    #define PRINTER3D_MJPEG_SCALE_MAX      2048    /** @brief widest decoder output the q16 arithmetic stays exact for */
    #define PRINTER3D_MJPEG_READ_TIMEOUT   2000    /** @brief ms without a single byte before a read gives up */
    #define PRINTER3D_MJPEG_SEEK_MAX     262144    /** @brief max bytes discarded while looking for the next frame */
    #define PRINTER3D_MJPEG_STALL_TIMEOUT 10000    /** @brief ms without a decoded frame before the stream is dropped */
    #define PRINTER3D_MJPEG_RETRY          5000    /** @brief ms before a dropped stream is picked up again */
    #define PRINTER3D_MJPEG_SNAPSHOT       1000    /** @brief ms between two single image requests */

    typedef enum {
        MJPEG_MODE_MULTIPART,                       /** @brief part header per frame */
        MJPEG_MODE_STREAM,                          /** @brief images back to back, no part header */
        MJPEG_MODE_SINGLE                           /** @brief one image per connection */
    } mjpeg_mode_t;

    typedef struct {
        WiFiClient* stream;                         /** @brief the http body */
        size_t remaining;                           /** @brief unread bytes of the current frame, SIZE_MAX if unknown */
        uint8_t pending[ JD_SZBUF ];                /** @brief bytes read ahead, handed out before the socket */
        uint16_t pending_len;
        uint16_t pending_pos;
    } mjpeg_stream_src_t;

    static uint64_t mjpeg_nextmillis = 0;           /** @brief reconnect window, independent of the printer poll */
    static bool mjpeg_visible = false;              /** @brief video tile is the tile on screen */
    static uint8_t mjpeg_scale = 0;                 /** @brief jd_decomp scale, 0..3 */
    static uint32_t mjpeg_step = PRINTER3D_MJPEG_SCALE_ONE; /** @brief q16 shrink applied on top of jd_decomp */
    static int32_t mjpeg_final_w = 0;               /** @brief image width after mjpeg_step, before cropping */
    static int32_t mjpeg_final_h = 0;               /** @brief image height after mjpeg_step, before cropping */
    static int32_t mjpeg_crop_x = 0;                /** @brief final image column shown at screen x 0 */
    static int32_t mjpeg_crop_y = 0;                /** @brief final image row shown at screen y 0 */
    static int32_t mjpeg_dst_x = 0;                 /** @brief left edge of the image on screen */
    static int32_t mjpeg_dst_w = 0;                 /** @brief visible width of the image, strip stride */
    static int32_t mjpeg_strip_top = -1;            /** @brief source row of the strip being collected */
    static int32_t mjpeg_strip_y = 0;               /** @brief screen row the strip starts at */
    static int32_t mjpeg_strip_h = 0;               /** @brief rows collected in the strip */
    static int32_t mjpeg_strip_skip = 0;            /** @brief source rows cropped off the top of the strip */
    static int32_t mjpeg_strip_dy = 0;              /** @brief first final image row of the strip, resampling only */
    static int32_t mjpeg_next_dy = 0;               /** @brief next final image row not produced yet */
    static int32_t mjpeg_next_dx = 0;               /** @brief next final image column not produced yet */
#endif

LV_IMG_DECLARE(refresh_32px);

LV_FONT_DECLARE(Ubuntu_12px);
LV_FONT_DECLARE(Ubuntu_16px);

static void printer3d_setup_activate_callback ( void );
static void printer3d_setup_hibernate_callback ( void );
static void exit_printer3d_app_main_event_cb( lv_obj_t * obj, lv_event_t event );
static void enter_printer3d_app_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static bool printer3d_powermgm_event_cb(EventBits_t event, void *arg);
static bool printer3d_main_wifictl_event_cb( EventBits_t event, void *arg );

#ifndef NATIVE_64BIT
    TaskHandle_t printer3d_refresh_handle;
    TaskHandle_t printer3d_mjpeg_handle;
    static volatile bool printer3d_refresh_running = false;
    static volatile bool printer3d_mjpeg_running = false;
#endif
printer3d_result_t printer3d_refresh_result;

void printer3d_refresh(void *parameter);
void printer3d_send(WiFiClient &client, char* buffer, size_t buffer_size, const char* command);
void printer3d_app_task( lv_task_t * task );
#ifndef NATIVE_64BIT
    void printer3d_mjpeg_init( void );
#endif

void printer3d_app_main_setup( uint32_t tile_num ) {

    mainbar_add_tile_activate_cb( tile_num, printer3d_setup_activate_callback );
    mainbar_add_tile_hibernate_cb( tile_num, printer3d_setup_hibernate_callback );
    printer3d_app_main_tile = mainbar_get_tile_obj( tile_num );
    #ifndef NATIVE_64BIT
        printer3d_app_video_tile = mainbar_get_tile_obj( tile_num + 1 );
    #endif

    // menu buttons
    lv_obj_t * exit_btn = wf_add_exit_button( printer3d_app_main_tile, exit_printer3d_app_main_event_cb );
    lv_obj_align(exit_btn, printer3d_app_main_tile, LV_ALIGN_IN_BOTTOM_LEFT, THEME_ICON_PADDING, -THEME_ICON_PADDING );

    lv_obj_t * setup_btn = wf_add_setup_button( printer3d_app_main_tile, enter_printer3d_app_setup_event_cb );
    lv_obj_align(setup_btn, printer3d_app_main_tile, LV_ALIGN_IN_BOTTOM_RIGHT, -THEME_ICON_PADDING, -THEME_ICON_PADDING );

    // headings
    printer3d_heading_name = lv_label_create( printer3d_app_main_tile, NULL);
    lv_style_copy(&printer3d_heading_big_style, APP_STYLE);
    lv_style_set_text_font(&printer3d_heading_big_style, LV_STATE_DEFAULT, &Ubuntu_16px);
    lv_obj_add_style( printer3d_heading_name, LV_OBJ_PART_MAIN, &printer3d_heading_big_style );
    lv_label_set_text( printer3d_heading_name, "3D Printer");
    lv_label_set_long_mode( printer3d_heading_name, LV_LABEL_LONG_SROLL );
    lv_obj_set_width( printer3d_heading_name, lv_disp_get_hor_res( NULL ) - 20 );
    lv_obj_align( printer3d_heading_name, printer3d_app_main_tile, LV_ALIGN_IN_TOP_LEFT, 10, 10 );
    
    printer3d_heading_version = lv_label_create( printer3d_app_main_tile, NULL);
    lv_style_copy(&printer3d_heading_small_style, APP_STYLE);
    lv_style_set_text_font(&printer3d_heading_small_style, LV_STATE_DEFAULT, &Ubuntu_12px);
    lv_obj_add_style( printer3d_heading_version, LV_OBJ_PART_MAIN, &printer3d_heading_small_style );
    lv_label_set_text( printer3d_heading_version, "");
    lv_label_set_long_mode( printer3d_heading_version, LV_LABEL_LONG_SROLL );
    lv_obj_set_width( printer3d_heading_version, lv_disp_get_hor_res( NULL ) - 20 );
    lv_obj_align( printer3d_heading_version, printer3d_heading_name, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0 );

    // progress
	printer3d_progress_linemeter = lv_linemeter_create(printer3d_app_main_tile, NULL);
    lv_style_copy(&printer3d_line_meter_style, APP_STYLE);
	lv_style_set_line_rounded(&printer3d_line_meter_style, LV_STATE_DEFAULT, 1);
	lv_style_set_line_color(&printer3d_line_meter_style, LV_STATE_DEFAULT, lv_color_hex(0xffffff));
	lv_style_set_scale_grad_color(&printer3d_line_meter_style, LV_STATE_DEFAULT, lv_color_hex(0xffffff));
	lv_style_set_scale_end_color(&printer3d_line_meter_style, LV_STATE_DEFAULT, lv_color_hex(0x666666));
	lv_obj_add_style(printer3d_progress_linemeter, LV_OBJ_PART_MAIN, &printer3d_line_meter_style);
	lv_linemeter_set_range(printer3d_progress_linemeter, 0, 100);
	lv_linemeter_set_scale(printer3d_progress_linemeter, 270, 20);
	lv_linemeter_set_value(printer3d_progress_linemeter, 0);
	lv_obj_set_size(printer3d_progress_linemeter, 120, 120);
    lv_obj_align( printer3d_progress_linemeter, printer3d_app_main_tile, LV_ALIGN_IN_TOP_MID, 0, 30 );

    printer3d_progress_percent = lv_label_create( printer3d_app_main_tile, NULL);
    lv_obj_add_style( printer3d_progress_percent, LV_OBJ_PART_MAIN, &printer3d_heading_big_style );
	lv_label_set_align(printer3d_progress_percent, LV_LABEL_ALIGN_CENTER);
    lv_label_set_text( printer3d_progress_percent, "--%");
    lv_label_set_long_mode( printer3d_progress_percent, LV_LABEL_LONG_CROP );
    lv_obj_set_width( printer3d_progress_percent, 30 );
    lv_obj_align( printer3d_progress_percent, printer3d_app_main_tile, LV_ALIGN_IN_TOP_MID, 0, 80 );

    printer3d_progress_state = lv_label_create( printer3d_app_main_tile, NULL);
    lv_obj_add_style( printer3d_progress_state, LV_OBJ_PART_MAIN, &printer3d_heading_big_style );
	lv_label_set_align(printer3d_progress_state, LV_LABEL_ALIGN_CENTER);
    lv_label_set_text( printer3d_progress_state, "LOADING");
    lv_label_set_long_mode( printer3d_progress_state, LV_LABEL_LONG_SROLL );
    lv_obj_set_width( printer3d_progress_state, lv_disp_get_hor_res( NULL ) - 20 );
    lv_obj_align( printer3d_progress_state, printer3d_app_main_tile, LV_ALIGN_IN_TOP_MID, 0, 130 );

    // temperatures
    printer3d_extruder_label = lv_label_create( printer3d_app_main_tile, NULL);
    lv_obj_add_style( printer3d_extruder_label, LV_OBJ_PART_MAIN, &printer3d_heading_small_style );
	lv_label_set_align(printer3d_extruder_label, LV_LABEL_ALIGN_LEFT);
    lv_label_set_text( printer3d_extruder_label, "Extruder");
    lv_label_set_long_mode( printer3d_extruder_label, LV_LABEL_LONG_CROP );
    lv_obj_set_width( printer3d_extruder_label, 100 );
    lv_obj_align( printer3d_extruder_label, printer3d_app_main_tile, LV_ALIGN_IN_BOTTOM_LEFT, 10, -50 );
    
    printer3d_extruder_temp = lv_label_create( printer3d_app_main_tile, NULL);
    lv_obj_add_style( printer3d_extruder_temp, LV_OBJ_PART_MAIN, &printer3d_heading_big_style );
	lv_label_set_align(printer3d_extruder_temp, LV_LABEL_ALIGN_LEFT);
    lv_label_set_text( printer3d_extruder_temp, "--°C");
    lv_label_set_long_mode( printer3d_extruder_temp, LV_LABEL_LONG_CROP );
    lv_obj_set_width( printer3d_extruder_temp, 100 );
    lv_obj_align( printer3d_extruder_temp, printer3d_app_main_tile, LV_ALIGN_IN_BOTTOM_LEFT, 10, -65 );
    
    printer3d_printbed_label = lv_label_create( printer3d_app_main_tile, NULL);
    lv_obj_add_style( printer3d_printbed_label, LV_OBJ_PART_MAIN, &printer3d_heading_small_style );
	lv_label_set_align(printer3d_printbed_label, LV_LABEL_ALIGN_RIGHT);
    lv_label_set_text( printer3d_printbed_label, "Printbed");
    lv_label_set_long_mode( printer3d_printbed_label, LV_LABEL_LONG_CROP );
    lv_obj_set_width( printer3d_printbed_label, 100 );
    lv_obj_align( printer3d_printbed_label, printer3d_app_main_tile, LV_ALIGN_IN_BOTTOM_RIGHT, -10, -50 );
    
    printer3d_printbed_temp = lv_label_create( printer3d_app_main_tile, NULL);
    lv_obj_add_style( printer3d_printbed_temp, LV_OBJ_PART_MAIN, &printer3d_heading_big_style );
	lv_label_set_align(printer3d_printbed_temp, LV_LABEL_ALIGN_RIGHT);
    lv_label_set_text( printer3d_printbed_temp, "--°C");
    lv_label_set_long_mode( printer3d_printbed_temp, LV_LABEL_LONG_CROP );
    lv_obj_set_width( printer3d_printbed_temp, 100 );
    lv_obj_align( printer3d_printbed_temp, printer3d_app_main_tile, LV_ALIGN_IN_BOTTOM_RIGHT, -10, -65 );

    // video img
    #ifndef NATIVE_64BIT
        printer3d_video_img = lv_img_create(printer3d_app_video_tile, NULL);
        lv_style_copy(&printer3d_app_video_style, APP_ICON_STYLE );
        lv_style_set_text_font(&printer3d_app_video_style, LV_STATE_DEFAULT, &lv_font_montserrat_32);
        lv_obj_add_style( printer3d_video_img, LV_OBJ_PART_MAIN, &printer3d_app_video_style );
        lv_img_set_src( printer3d_video_img, LV_SYMBOL_IMAGE );
        lv_obj_align( printer3d_video_img, printer3d_app_video_tile, LV_ALIGN_IN_TOP_LEFT, (RES_X_MAX / 2) - 16, (RES_Y_MAX / 2) - 16 );
    #endif

    // callbacks
    powermgm_register_cb( POWERMGM_STANDBY | POWERMGM_STANDBY_REQUEST | POWERMGM_WAKEUP, printer3d_powermgm_event_cb, "printer3d powermgm");
    wifictl_register_cb( WIFICTL_OFF | WIFICTL_CONNECT_IP | WIFICTL_DISCONNECT, printer3d_main_wifictl_event_cb, "printer3d main" );

    // create an task that runs every second
    _printer3d_app_task = lv_task_create( printer3d_app_task, 1000, LV_TASK_PRIO_MID, NULL );
}

static void printer3d_setup_activate_callback ( void ) {
    printer3d_open_state = true;
    nextmillis = 0;
    #ifndef NATIVE_64BIT
        mjpeg_nextmillis = 0;
    #endif
}

static void printer3d_setup_hibernate_callback ( void ) {
    printer3d_open_state = false;
    nextmillis = 0;
    #ifndef NATIVE_64BIT
        mjpeg_nextmillis = 0;
    #endif
}

/** @brief true if one of the app tiles is the tile currently on screen */
static bool printer3d_tile_on_screen( void ) {
    lv_area_t area;
    bool onscreen = false;

    gui_take();
    if ( printer3d_app_main_tile != NULL ) {
        lv_obj_get_coords( printer3d_app_main_tile, &area );
        onscreen = area.x1 == 0 && area.y1 == 0;
    }
    #ifndef NATIVE_64BIT
        if ( !onscreen && printer3d_app_video_tile != NULL && strlen( printer3d_get_config()->camera ) > 0 ) {
            lv_obj_get_coords( printer3d_app_video_tile, &area );
            onscreen = area.x1 == 0 && area.y1 == 0;
        }
    #endif
    gui_give();

    return( onscreen );
}

static bool printer3d_powermgm_event_cb(EventBits_t event, void *arg)
{
    switch( event ) {
        case( POWERMGM_STANDBY ):
            printer3d_open_state = false;
            break;
        case( POWERMGM_STANDBY_REQUEST ):
            printer3d_open_state = false;
            break;
        case( POWERMGM_WAKEUP ):
            printer3d_open_state = printer3d_tile_on_screen();
            nextmillis = 0;
            #ifndef NATIVE_64BIT
                mjpeg_nextmillis = 0;
            #endif
            break;
    }
    return( true );
}

static bool printer3d_main_wifictl_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case WIFICTL_CONNECT_IP:    printer3d_state = true;
                                    printer3d_app_set_indicator( ICON_INDICATOR_UPDATE );
                                    break;
        case WIFICTL_DISCONNECT:    printer3d_state = false;
                                    printer3d_app_set_indicator( ICON_INDICATOR_FAIL );
                                    break;
        case WIFICTL_OFF:           printer3d_state = false;
                                    printer3d_app_hide_indicator();
                                    break;
    }
    return( true );
}

static void enter_printer3d_app_setup_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_jump_to_tilenumber( printer3d_app_get_app_setup_tile_num(), LV_ANIM_ON, true );
                                        nextmillis = 0;
                                        break;
    }
}

static void exit_printer3d_app_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       printer3d_open_state = false;
                                        mainbar_jump_back();
                                        break;
    }
}

void printer3d_app_task( lv_task_t * task ) {
    if (!printer3d_state) return;

    if ( nextmillis < millis() ) {
        if (printer3d_open_state) {
            nextmillis = millis() + 30000L;
        } else {
            nextmillis = millis() + 120000L;
        }

        if (printer3d_refresh_result.machineType == nullptr) printer3d_refresh_result.machineType = (volatile char*)CALLOC(32, sizeof(char));
        if (printer3d_refresh_result.machineVersion == nullptr) printer3d_refresh_result.machineVersion = (volatile char*)CALLOC(16, sizeof(char));
        if (printer3d_refresh_result.stateMachine == nullptr) printer3d_refresh_result.stateMachine = (volatile char*)CALLOC(16, sizeof(char));
        if (printer3d_refresh_result.stateMove == nullptr) printer3d_refresh_result.stateMove = (volatile char*)CALLOC(16, sizeof(char));

        if (printer3d_refresh_result.machineType == nullptr || printer3d_refresh_result.machineVersion == nullptr ||
            printer3d_refresh_result.stateMachine == nullptr || printer3d_refresh_result.stateMove == nullptr) {
            log_e("printer3d: could not allocate the result buffers");
            printer3d_app_set_indicator( ICON_INDICATOR_FAIL );
            return;
        }

        printer3d_app_set_indicator( ICON_INDICATOR_UPDATE );
        #ifdef NATIVE_64BIT
            printer3d_refresh( NULL );
        #else
            // only start a refresh if the previous one already finished
            if ( !printer3d_refresh_running ) {
                printer3d_refresh_running = true;
                if ( xTaskCreatePinnedToCore(printer3d_refresh, "printer3d_refresh", 2560, NULL, 1, &printer3d_refresh_handle, 0) != pdPASS )
                    printer3d_refresh_running = false;
            }
        #endif
    }

    #ifndef NATIVE_64BIT
        if (printer3d_open_state && mjpeg_nextmillis < millis()) {
            mjpeg_nextmillis = millis() + PRINTER3D_MJPEG_RETRY;
            printer3d_mjpeg_init();
        }
    #endif

    if (printer3d_refresh_result.changed) {
        printer3d_refresh_result.changed = false;
        char val[32];

        if (printer3d_refresh_result.machineType[0] != '\0') {
            snprintf( val, sizeof(val), "%s", printer3d_refresh_result.machineType );
            lv_label_set_text(printer3d_heading_name, val);
        }

        if (printer3d_refresh_result.machineVersion[0] != '\0') {
            snprintf( val, sizeof(val), "%s", printer3d_refresh_result.machineVersion );
            lv_label_set_text(printer3d_heading_version, val);
        }

        snprintf( val, sizeof(val), "%s", printer3d_refresh_result.stateMachine );
        lv_label_set_text(printer3d_progress_state, val);

        if (printer3d_refresh_result.extruderTemp >= 0 && printer3d_refresh_result.extruderTemp <= 500) {
            if (printer3d_refresh_result.extruderTempMax > 0 && printer3d_refresh_result.extruderTempMax <= 500) {
                snprintf( val, sizeof(val), "%.1f / %.1f°C", printer3d_refresh_result.extruderTemp, printer3d_refresh_result.extruderTempMax );
                lv_label_set_text(printer3d_extruder_temp, val);
            } else {
                snprintf( val, sizeof(val), "%.1f°C", printer3d_refresh_result.extruderTemp );
                lv_label_set_text(printer3d_extruder_temp, val);
            }
        }
        
        if (printer3d_refresh_result.printbedTemp >= 0 && printer3d_refresh_result.printbedTemp <= 100) {
            if (printer3d_refresh_result.printbedTempMax > 0 && printer3d_refresh_result.printbedTempMax <= 100) {
                snprintf( val, sizeof(val), "%.1f / %.1f°C", printer3d_refresh_result.printbedTemp, printer3d_refresh_result.printbedTempMax );
                lv_label_set_text(printer3d_printbed_temp, val);
            } else {
                snprintf( val, sizeof(val), "%.1f°C", printer3d_refresh_result.printbedTemp );
                lv_label_set_text(printer3d_printbed_temp, val);
            }
        }

        lv_linemeter_set_value(printer3d_progress_linemeter, printer3d_refresh_result.printProgress);
        lv_linemeter_set_range(printer3d_progress_linemeter, 0, printer3d_refresh_result.printMax);

        uint8_t printPercent = printer3d_refresh_result.printProgress * 100 / printer3d_refresh_result.printMax;
        if (printPercent >= 0 && printPercent <= 100) {
            snprintf( val, sizeof(val), "%d%%", printPercent );
            lv_label_set_text(printer3d_progress_percent, val);
        }

        if (printer3d_refresh_result.success) {
            printer3d_app_set_indicator( ICON_INDICATOR_OK );
        } else {
            printer3d_app_set_indicator( ICON_INDICATOR_FAIL );
        }
    }
}

static void printer3d_refresh_request(void) {
    if (!printer3d_state) return;

    printer3d_config_t *printer3d_config = printer3d_get_config();
    if (!strlen(printer3d_config->host)) {
        printer3d_refresh_result.changed = true;
        printer3d_refresh_result.success = false;
        return;
    }

    // connecting to 3d printer
    WiFiClient client;
    client.connect(printer3d_config->host, printer3d_config->port, 2000);
    client.setTimeout(3);

    for (uint8_t i = 0; i < 30; i++) {
        if (client.connected()) break;
        delay(100);
    }

    if (!client.connected()){
        log_w("printer3d: could not connect to %s:%d", printer3d_config->host, printer3d_config->port);
        client.stop();
        printer3d_refresh_result.changed = true;
        printer3d_refresh_result.success = false;
        return;
    } else {
        log_i("printer3d: connected to %s:%d", printer3d_config->host, printer3d_config->port);
    }

    // sending G-Codes to 3d printer
    const size_t info_size = 1024;
    const size_t state_size = 512;

    char* esp3dInfo = (char*)CALLOC( info_size, sizeof(char) );
    char* generalInfo = (char*)CALLOC( info_size, sizeof(char) );
    char* stateInfo = (char*)CALLOC( info_size, sizeof(char) );
    char* tempInfo = (char*)CALLOC( state_size, sizeof(char) );
    char* printInfo = (char*)CALLOC( state_size, sizeof(char) );

    if (!esp3dInfo || !generalInfo || !stateInfo || !tempInfo || !printInfo) {
        log_e("printer3d: could not allocate the info buffers");
        free( esp3dInfo ); free( generalInfo ); free( stateInfo );
        free( tempInfo ); free( printInfo );
        client.stop();
        printer3d_refresh_result.changed = true;
        printer3d_refresh_result.success = false;
        return;
    }

    if (strlen(printer3d_config->pass) > 0) {
        char val[30];
        snprintf( val, sizeof(val), "[ESP800]pwd=%s", printer3d_config->pass );
        printer3d_send(client, esp3dInfo, info_size, val);
    }
    printer3d_send(client, generalInfo, info_size, "~M115");
    printer3d_send(client, stateInfo, info_size, "~M119");
    printer3d_send(client, tempInfo, state_size, "~M105");
    printer3d_send(client, printInfo, state_size, "~M27");

    // close connection
    client.stop();

    // parse received information from the 3d printer
    if (esp3dInfo != NULL && strlen(esp3dInfo) > 0) {
        char machineType[32], machineVersion[16];

        char* esp3dInfoType1 = strstr(esp3dInfo, "FW target");
        if ( esp3dInfoType1 != NULL && strlen(esp3dInfoType1) > 0 && sscanf( esp3dInfoType1, "FW target: %31s", machineType ) > 0 ) {
            snprintf( (char*)printer3d_refresh_result.machineType, 32, "%s", machineType );
        }

        char* esp3dInfoType2 = strstr(esp3dInfo, "hostname");
        if ( esp3dInfoType2 != NULL && strlen(esp3dInfoType2) > 0 && sscanf( esp3dInfoType2, "hostname: %31s", machineType ) > 0 ) {
            snprintf( (char*)printer3d_refresh_result.machineType, 32, "%s", machineType );
        }

        char* esp3dInfoVersion = strstr(esp3dInfo, "FW version:");
        if ( esp3dInfoVersion != NULL && strlen(esp3dInfoVersion) > 0 && sscanf( esp3dInfoVersion, "FW version: %15s", machineVersion ) > 0 ) {
            snprintf( (char*)printer3d_refresh_result.machineVersion, 16, "%s", machineVersion );
        }
    }
    free( esp3dInfo );
    
    if (generalInfo != NULL && strlen(generalInfo) > 0) {
        char machineType[32], machineVersion[16];

        char* generalInfoType1 = strstr(generalInfo, "Machine Type:");
        if ( generalInfoType1 != NULL && strlen(generalInfoType1) > 0 && sscanf( generalInfoType1, "Machine Type: %31[a-zA-Z0-9- ]", machineType ) > 0 ) {
            snprintf( (char*)printer3d_refresh_result.machineType, 32, "%s", machineType );
        }

        char* generalInfoType2 = strstr(generalInfo, "FIRMWARE_NAME");
        if ( generalInfoType2 != NULL && strlen(generalInfoType2) > 0 && sscanf( generalInfoType2, "FIRMWARE_NAME: %31s", machineType ) > 0 ) {
            snprintf( (char*)printer3d_refresh_result.machineType, 32, "%s", machineType );
        }

        char* generalInfoVersion1 = strstr(generalInfo, "Firmware:");
        if ( generalInfoVersion1 != NULL && strlen(generalInfoVersion1) > 0 && sscanf( generalInfoVersion1, "Firmware: %15s", machineVersion ) > 0 ) {
            snprintf( (char*)printer3d_refresh_result.machineVersion, 16, "%s", machineVersion );
        }

        char* generalInfoVersion2 = strstr(generalInfo, "FIRMWARE_VERSION:");
        if ( generalInfoVersion2 != NULL && strlen(generalInfoVersion2) > 0 && sscanf( generalInfoVersion2, "FIRMWARE_VERSION: %15s", machineVersion ) > 0 ) {
            snprintf( (char*)printer3d_refresh_result.machineVersion, 16, "%s", machineVersion );
        }
    }
    free( generalInfo );
    
    if (stateInfo != NULL && strlen(stateInfo) > 0) {
        char stateMachine[16], stateMove[16];

        char* stateInfoMachine = strstr(stateInfo, "MachineStatus:");
        if ( stateInfoMachine != NULL && strlen(stateInfoMachine) > 0 && sscanf( stateInfoMachine, "MachineStatus: %15[a-zA-Z]", stateMachine ) > 0 ) {
            snprintf( (char*)printer3d_refresh_result.stateMachine, 16, "%s", stateMachine );
        }

        char* stateInfoMove = strstr(stateInfo, "MoveMode:");
        if ( stateInfoMove != NULL && strlen(stateInfoMove) > 0 && sscanf( stateInfoMove, "MoveMode: %15[a-zA-Z]", stateMove ) > 0 ) {
            snprintf( (char*)printer3d_refresh_result.stateMove, 16, "%s", stateMove );
        }
    }
    free( stateInfo );

    if (tempInfo != NULL && strlen(tempInfo) > 0) {
        float extruderTemp = -1;
        float extruderTempMax = -1;
        float printbedTemp = -1;
        float printbedTempMax = -1;
        
        char* extruderLine1 = strstr(tempInfo, "T:");
        if ( extruderTemp < 0 && extruderLine1 != NULL && strlen(extruderLine1) > 0 && sscanf( extruderLine1, "T: %f / %f", &extruderTemp, &extruderTempMax ) > 0 ) {
            if (extruderTemp >= 0) printer3d_refresh_result.extruderTemp = extruderTemp;
            if (extruderTempMax >= 0) printer3d_refresh_result.extruderTempMax = extruderTempMax;
        }

        char* extruderLine2 = strstr(tempInfo, "T0:");
        if ( extruderTemp < 0 && extruderLine2 != NULL && strlen(extruderLine2) > 0 && sscanf( extruderLine2, "T0: %f / %f", &extruderTemp, &extruderTempMax ) > 0 ) {
            if (extruderTemp >= 0) printer3d_refresh_result.extruderTemp = extruderTemp;
            if (extruderTempMax >= 0) printer3d_refresh_result.extruderTempMax = extruderTempMax;
        }

        char* printbedLine = strstr(tempInfo, "B:");
        if ( printbedLine != NULL && strlen(printbedLine) > 0 && sscanf( printbedLine, "B: %f / %f", &printbedTemp, &printbedTempMax ) > 0 ) {
            if (printbedTemp >= 0) printer3d_refresh_result.printbedTemp = printbedTemp;
            if (printbedTempMax >= 0) printer3d_refresh_result.printbedTempMax = printbedTempMax;
        }
    }
    free( tempInfo );
    
    if (printInfo != NULL && strlen(printInfo) > 0) {
        int printProgress = -1;
        int printMax = -1;
        
        char* printInfoLine = strstr(printInfo, "byte ");
        if ( printInfoLine != NULL && strlen(printInfoLine) > 0 && sscanf( printInfoLine, "byte %d / %d", &printProgress, &printMax ) > 0 ) {
            if (printProgress >= 0 && printProgress <= printMax) printer3d_refresh_result.printProgress = printProgress;
            if (printMax > 0) printer3d_refresh_result.printMax = printMax;
        }
    }
    free( printInfo );

    printer3d_refresh_result.changed = true;
    printer3d_refresh_result.success = true;
}

void printer3d_refresh(void *parameter) {
    printer3d_refresh_request();

    #ifndef NATIVE_64BIT
        printer3d_refresh_running = false;
        vTaskDelete(NULL);
    #endif
}

void printer3d_send(WiFiClient &client, char* buffer, size_t buffer_size, const char* command) {
    if (!buffer || !buffer_size) return;
    buffer[0] = '\0';

    if (!printer3d_state) return;
    if (!client.connected()) return;

    client.write(command);
    log_d("3dprinter sent command: %s", command);

    for (uint8_t i = 0; i < 25; i++) {
        if (client.available()) break;
        delay(10);
    }

    size_t pos = 0;
    while (pos < buffer_size - 1) {
        size_t avail = client.available();
        if (!avail) break;
        if (avail > buffer_size - 1 - pos) avail = buffer_size - 1 - pos;

        int got = client.read((uint8_t*)buffer + pos, avail);
        if (got <= 0) break;
        pos += got;
    }
    buffer[pos] = '\0';

    log_d("3dprinter received: %s", buffer);
}

#ifndef NATIVE_64BIT
    static uint32_t printer3d_mjpeg_read( mjpeg_stream_src_t* src, uint8_t* buffer, uint32_t size ) {
        uint64_t deadline = millis() + PRINTER3D_MJPEG_READ_TIMEOUT;
        uint32_t got = 0;

        // hand out what has been read ahead before touching the socket
        while ( src->pending_pos < src->pending_len && got < size )
            buffer[ got++ ] = src->pending[ src->pending_pos++ ];

        while ( got < size ) {
            if ( !printer3d_state || !printer3d_open_state )
                break;
            if ( !src->stream->connected() && !src->stream->available() )
                break;

            int received = src->stream->read( buffer + got, size - got );
            if ( received > 0 ) {
                got += received;
                continue;
            }
            if ( received < 0 )
                break;
            if ( millis() > deadline ) {
                log_d("3dprinter timed out while reading the video stream");
                break;
            }
            delay( 1 );
        }

        return got;
    }

    static uint32_t printer3d_mjpeg_skip( mjpeg_stream_src_t* src, uint32_t size ) {
        uint8_t skip[ 64 ];
        uint32_t done = 0;

        while ( done < size ) {
            uint32_t chunk = ( size - done ) > sizeof( skip ) ? sizeof( skip ) : ( size - done );
            uint32_t got = printer3d_mjpeg_read( src, skip, chunk );
            if ( !got )
                break;
            done += got;
        }

        return done;
    }

    static uint32_t printer3d_mjpeg_input(JDEC* decoder, uint8_t* buffer, uint32_t size) {
        mjpeg_stream_src_t* src = (mjpeg_stream_src_t*)decoder->device;

        // never hand out more than belongs to the current frame
        if ( size > src->remaining )
            size = src->remaining;
        if ( !size )
            return 0;

        uint32_t got = buffer ? printer3d_mjpeg_read( src, buffer, size )
                              : printer3d_mjpeg_skip( src, size );
        src->remaining -= got;

        return got;
    }

    /** @brief read a single line, returns its length without the line break or -1 if the stream is gone */
    static int32_t printer3d_mjpeg_read_line( mjpeg_stream_src_t* src, char* line, size_t size ) {
        size_t pos = 0;

        while ( true ) {
            uint8_t byte;
            if ( !printer3d_mjpeg_read( src, &byte, 1 ) )
                return -1;
            if ( byte == '\n' )
                break;
            if ( byte != '\r' && pos < size - 1 )
                line[ pos++ ] = ( char )byte;
        }

        line[ pos ] = '\0';
        return pos;
    }

    /** @brief consume the part header of a multipart frame, returns its length or 0 if the server sent none */
    static size_t printer3d_mjpeg_read_part( mjpeg_stream_src_t* src, const char* boundary, bool* last, bool* failed ) {
        char line[ 128 ];
        size_t content_length = 0;
        bool header = false;

        while ( printer3d_mjpeg_read_line( src, line, sizeof( line ) ) >= 0 ) {
            // the previous frame ends with a line break, so skip empty lines until the boundary shows up
            if ( !line[ 0 ] ) {
                if ( header )
                    return content_length;
                continue;
            }

            if ( !header ) {
                size_t length = strlen( line );
                header = true;

                // a boundary with a trailing -- closes the stream
                if ( length >= 4 && !strcmp( line + length - 2, "--" ) ) {
                    *last = true;
                    return 0;
                }
                if ( boundary[ 0 ] && !strstr( line, boundary ) )
                    log_d("3dprinter received an unexpected boundary: %s", line);
                continue;
            }

            if ( !strncasecmp( line, "content-length:", 15 ) )
                content_length = strtoul( line + 15, NULL, 10 );
        }

        // a failed read is a broken connection, not a stream the server ended
        *failed = true;
        return 0;
    }

    /** @brief discard bytes until the end of the current image, the stream then stands on the next one */
    static bool printer3d_mjpeg_seek_eoi( mjpeg_stream_src_t* src ) {
        uint8_t block[ 64 ];
        uint32_t scanned = 0;
        uint8_t last = 0;

        while ( scanned < PRINTER3D_MJPEG_SEEK_MAX ) {
            // scan what has been read ahead first, it is not copied around
            if ( src->pending_pos < src->pending_len ) {
                while ( src->pending_pos < src->pending_len ) {
                    uint8_t byte = src->pending[ src->pending_pos++ ];
                    scanned++;
                    if ( last == 0xFF && byte == 0xD9 )
                        return true;
                    last = byte;
                }
                continue;
            }

            // never ask for more than is there, the read would only wait for it
            int available = src->stream->available();
            uint32_t want = available > ( int )sizeof( block ) ? sizeof( block )
                                                               : ( available > 0 ? ( uint32_t )available : 1 );
            uint32_t got = printer3d_mjpeg_read( src, block, want );
            if ( !got )
                return false;
            scanned += got;

            for ( uint32_t i = 0; i < got; i++ ) {
                if ( last == 0xFF && block[ i ] == 0xD9 ) {
                    // push the rest back, the next image starts right here
                    src->pending_len = got - i - 1;
                    src->pending_pos = 0;
                    memcpy( src->pending, block + i + 1, src->pending_len );
                    return true;
                }
                last = block[ i ];
            }
        }

        log_w("3dprinter could not find the end of an image in %d bytes", scanned);
        return false;
    }

    static void printer3d_mjpeg_strip_flush( void ) {
        if ( mjpeg_strip_h <= 0 || mjpeg_dst_w <= 0 || mjpeg_frame == nullptr )
            return;

        if ( mjpeg_visible && printer3d_state && printer3d_open_state ) {
            gui_take();
            framebuffer_push_image( mjpeg_dst_x, mjpeg_strip_y, mjpeg_dst_w, mjpeg_strip_h, ( lv_color_t* )mjpeg_frame );
            gui_give();
        }

        mjpeg_strip_h = 0;
    }

    /** @brief write one rgb888 pixel in the color depth of the display */
    static inline void printer3d_mjpeg_put_pixel( uint8_t* dst, uint8_t r, uint8_t g, uint8_t b ) {
        #if LV_COLOR_DEPTH == 32
            dst[ 0 ] = b;
            dst[ 1 ] = g;
            dst[ 2 ] = r;
            dst[ 3 ] = 0xff;
        #elif LV_COLOR_DEPTH == 16
            uint32_t col_16bit = ( r & 0xf8 ) << 8;
            col_16bit |= ( g & 0xFC ) << 3;
            col_16bit |= ( b >> 3 );
            #ifdef LV_BIG_ENDIAN_SYSTEM
                dst[ 0 ] = col_16bit >> 8;
                dst[ 1 ] = col_16bit & 0xff;
            #else
                dst[ 0 ] = col_16bit & 0xff;
                dst[ 1 ] = col_16bit >> 8;
            #endif
        #elif LV_COLOR_DEPTH == 8
            uint8_t col_8bit = ( r & 0xC0 );
            col_8bit |= ( g & 0xe0 ) >> 2;
            col_8bit |= ( b & 0xe0 ) >> 5;
            dst[ 0 ] = col_8bit;
        #else
            #error Unsupported LV_COLOR_DEPTH
        #endif
    }

    /** @brief the jd_decomp scale already fits the screen, the block is copied pixel by pixel */
    static int32_t printer3d_mjpeg_output_copy( void* data, JRECT* rect ) {
        const int32_t src_width = rect->right - rect->left + 1;

        // a new row starts, push the collected strip and set up the next one
        if ( rect->top != mjpeg_strip_top ) {
            printer3d_mjpeg_strip_flush();
            mjpeg_strip_top = rect->top;

            int32_t first = rect->top - mjpeg_crop_y;
            int32_t last = rect->bottom - mjpeg_crop_y;
            mjpeg_strip_skip = first < 0 ? -first : 0;
            mjpeg_strip_y = first + mjpeg_strip_skip;
            if ( last > RES_Y_MAX - 1 ) last = RES_Y_MAX - 1;
            mjpeg_strip_h = last - mjpeg_strip_y + 1;
            if ( mjpeg_strip_h < 0 ) mjpeg_strip_h = 0;
            if ( mjpeg_strip_h > PRINTER3D_MJPEG_STRIP_H ) mjpeg_strip_h = PRINTER3D_MJPEG_STRIP_H;
        }

        if ( mjpeg_strip_h <= 0 ) return 1;

        // clip the block against the visible part of the image
        int32_t left = rect->left - mjpeg_crop_x;
        int32_t skip = left < mjpeg_dst_x ? mjpeg_dst_x - left : 0;
        int32_t right = left + src_width - 1;
        if ( right > mjpeg_dst_x + mjpeg_dst_w - 1 ) right = mjpeg_dst_x + mjpeg_dst_w - 1;

        const int32_t copy_width = right - ( left + skip ) + 1;
        if ( copy_width <= 0 ) return 1;

        const int32_t pixelsize = sizeof( lv_color_t );
        const int32_t start = ( left + skip - mjpeg_dst_x ) * pixelsize;

        // convert raw output into the corresponding color depth pixels
        for ( int32_t row = 0; row < mjpeg_strip_h; row++ ) {
            uint8_t* buffer = (uint8_t*)data + ( ( row + mjpeg_strip_skip ) * src_width + skip ) * 3;
            uint32_t offset = row * mjpeg_dst_w * pixelsize + start;

            for ( int32_t i = 0; i < copy_width; i++ ) {
                printer3d_mjpeg_put_pixel( mjpeg_frame + offset, buffer[ 0 ], buffer[ 1 ], buffer[ 2 ] );
                buffer += 3;
                offset += pixelsize;
            }
        }

        return 1;
    }

    /** @brief bilinear shrink between two jd_decomp steps */
    static int32_t printer3d_mjpeg_output_resample( void* data, JRECT* rect ) {
        const int32_t src_width = rect->right - rect->left + 1;
        const int32_t pixelsize = sizeof( lv_color_t );

        // a new mcu row starts, push the collected strip and take the final rows this block covers
        if ( rect->top != mjpeg_strip_top ) {
            printer3d_mjpeg_strip_flush();
            mjpeg_strip_top = rect->top;
            mjpeg_next_dx = 0;

            // a truncated mcu row leaves the counter behind, never sample above the block
            while ( mjpeg_next_dy < mjpeg_final_h &&
                    ( int32_t )( ( uint32_t )mjpeg_next_dy * mjpeg_step >> 16 ) < rect->top ) mjpeg_next_dy++;

            mjpeg_strip_dy = mjpeg_next_dy;
            while ( mjpeg_next_dy < mjpeg_final_h &&
                    ( int32_t )( ( uint32_t )mjpeg_next_dy * mjpeg_step >> 16 ) <= rect->bottom ) mjpeg_next_dy++;

            int32_t first = mjpeg_strip_dy - mjpeg_crop_y;
            int32_t last = mjpeg_next_dy - 1 - mjpeg_crop_y;
            mjpeg_strip_skip = first < 0 ? -first : 0;
            mjpeg_strip_y = first + mjpeg_strip_skip;
            if ( last > RES_Y_MAX - 1 ) last = RES_Y_MAX - 1;
            mjpeg_strip_h = last - mjpeg_strip_y + 1;
            if ( mjpeg_strip_h < 0 ) mjpeg_strip_h = 0;
            if ( mjpeg_strip_h > PRINTER3D_MJPEG_STRIP_H ) mjpeg_strip_h = PRINTER3D_MJPEG_STRIP_H;
        }

        // take the final columns this block covers, the counter runs on even if nothing is drawn
        while ( mjpeg_next_dx < mjpeg_final_w &&
                ( int32_t )( ( uint32_t )mjpeg_next_dx * mjpeg_step >> 16 ) < rect->left ) mjpeg_next_dx++;

        int32_t dx_first = mjpeg_next_dx;
        while ( mjpeg_next_dx < mjpeg_final_w &&
                ( int32_t )( ( uint32_t )mjpeg_next_dx * mjpeg_step >> 16 ) <= rect->right ) mjpeg_next_dx++;
        int32_t dx_last = mjpeg_next_dx - 1;

        if ( mjpeg_strip_h <= 0 ) return 1;

        // clip the columns against the visible part of the image
        if ( dx_first - mjpeg_crop_x < mjpeg_dst_x ) dx_first = mjpeg_dst_x + mjpeg_crop_x;
        if ( dx_last - mjpeg_crop_x > mjpeg_dst_x + mjpeg_dst_w - 1 ) dx_last = mjpeg_dst_x + mjpeg_dst_w - 1 + mjpeg_crop_x;
        if ( dx_last < dx_first ) return 1;

        const int32_t last_row = rect->bottom - rect->top;
        const int32_t last_col = src_width - 1;
        const int32_t start = ( dx_first - mjpeg_crop_x - mjpeg_dst_x ) * pixelsize;

        for ( int32_t row = 0; row < mjpeg_strip_h; row++ ) {
            uint32_t ypos = ( uint32_t )( mjpeg_strip_dy + mjpeg_strip_skip + row ) * mjpeg_step;
            int32_t sy = ( int32_t )( ypos >> 16 ) - rect->top;
            uint32_t wy = ( ypos >> 8 ) & 0xff;
            // the row below belongs to the next mcu row, that pixel stays nearest neighbour
            if ( sy >= last_row ) { sy = last_row; wy = 0; }

            const uint8_t* top = (const uint8_t*)data + sy * src_width * 3;
            const uint8_t* bottom = wy ? top + src_width * 3 : top;
            uint32_t offset = row * mjpeg_dst_w * pixelsize + start;

            for ( int32_t dx = dx_first; dx <= dx_last; dx++ ) {
                uint32_t xpos = ( uint32_t )dx * mjpeg_step;
                int32_t sx = ( int32_t )( xpos >> 16 ) - rect->left;
                uint32_t wx = ( xpos >> 8 ) & 0xff;
                // same at the right edge of the block
                if ( sx >= last_col ) { sx = last_col; wx = 0; }

                const uint8_t* a = top + sx * 3;
                const uint8_t* b = bottom + sx * 3;
                const uint8_t* a2 = wx ? a + 3 : a;
                const uint8_t* b2 = wx ? b + 3 : b;

                uint8_t rgb[ 3 ];
                for ( int32_t i = 0; i < 3; i++ ) {
                    uint32_t upper = a[ i ] * ( 256 - wx ) + a2[ i ] * wx;
                    uint32_t lower = b[ i ] * ( 256 - wx ) + b2[ i ] * wx;
                    rgb[ i ] = ( uint8_t )( ( upper * ( 256 - wy ) + lower * wy ) >> 16 );
                }

                printer3d_mjpeg_put_pixel( mjpeg_frame + offset, rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] );
                offset += pixelsize;
            }
        }

        return 1;
    }

    static int32_t printer3d_mjpeg_output( JDEC* decoder, void* data, JRECT* rect ) {
        if (!printer3d_state || !printer3d_open_state) return 0;
        if (!mjpeg_frame) return 0;

        if ( mjpeg_step == PRINTER3D_MJPEG_SCALE_ONE )
            return printer3d_mjpeg_output_copy( data, rect );

        return printer3d_mjpeg_output_resample( data, rect );
    }

    static void printer3d_mjpeg_set_visible( bool visible ) {
        if ( visible == mjpeg_visible )
            return;

        mjpeg_visible = visible;

        gui_take();
        lv_obj_set_hidden( printer3d_video_img, visible );
        if ( !visible )
            lv_obj_invalidate( printer3d_app_video_tile );
        gui_give();
    }

    static void printer3d_mjpeg_update_visible( void ) {
        lv_area_t tile_area;

        gui_take();
        lv_obj_get_coords( printer3d_app_video_tile, &tile_area );
        gui_give();

        printer3d_mjpeg_set_visible( tile_area.x1 == 0 && tile_area.y1 == 0 );
    }

    /** @brief wait between two frames, keeps the tile state current. false if the app is going away */
    static bool printer3d_mjpeg_wait( uint32_t wait ) {
        uint64_t until = millis() + wait;

        while ( millis() < until ) {
            if ( !printer3d_state || !printer3d_open_state )
                return false;

            printer3d_mjpeg_update_visible();
            delay( 100 );
        }

        return true;
    }

    /** @brief one connection: connect, decode until it ends, close. returns the number of frames decoded */
    static uint32_t printer3d_mjpeg_session( HTTPClient& mjpeg_client, mjpeg_stream_src_t* src, JDEC* decoder, bool* connectfailed ) {
        uint32_t framecount = 0;

        mjpeg_client.begin(mjpeg_url);

        int httpcode = mjpeg_client.GET();
        if (httpcode < 200 || httpcode >= 400) {
            // say it once per outage, the retry every few seconds must not flood the log
            if (*connectfailed) log_d("3dprinter still cannot connect to video stream at %s, http %d", mjpeg_url, httpcode);
            else log_w("3dprinter could not connect to video stream at %s, http %d", mjpeg_url, httpcode);
            *connectfailed = true;
        } else {
            *connectfailed = false;
            log_d("3dprinter connected to video stream at %s", mjpeg_url);

            // find out how the frames are framed, the media type also tells single images from streams
            char media[ 32 ] = "";
            char boundary[ 72 ] = "";
            String content_type = mjpeg_client.header( "Content-Type" );
            String content_type_lower = content_type;
            content_type_lower.toLowerCase();

            int separator = content_type_lower.indexOf( ';' );
            snprintf( media, sizeof( media ), "%s", ( separator < 0 ? content_type_lower : content_type_lower.substring( 0, separator ) ).c_str() );

            int position = content_type_lower.indexOf( "boundary=" );
            if ( position >= 0 ) {
                String value = content_type.substring( position + 9 );
                int end = value.indexOf( ';' );
                if ( end >= 0 ) value = value.substring( 0, end );
                value.replace( "\"", "" );
                value.trim();
                snprintf( boundary, sizeof( boundary ), "%s", value.c_str() );
            }

            // a single image carries a content length, a stream does not, so the size decides when the type does not
            int contentsize = mjpeg_client.getSize();
            mjpeg_mode_t mode;
            if ( !strncmp( media, "multipart/", 10 ) ) mode = MJPEG_MODE_MULTIPART;
            else if ( !strcmp( media, "image/jpeg" ) || !strcmp( media, "image/jpg" ) ) mode = MJPEG_MODE_SINGLE;
            else if ( !strncmp( media, "video/", 6 ) ) mode = MJPEG_MODE_STREAM;
            else mode = contentsize >= 0 ? MJPEG_MODE_SINGLE : MJPEG_MODE_STREAM;

            log_d("3dprinter video source is of type '%s' with boundary '%s' and size %d, reading it as %s", media, boundary, contentsize,
                  mode == MJPEG_MODE_MULTIPART ? "multipart" : ( mode == MJPEG_MODE_SINGLE ? "single image" : "plain stream" ));

            // prepare stream
            WiFiClient stream = mjpeg_client.getStream();
            stream.setTimeout(2);

            // the reader carries no state over from the previous connection
            src->stream = &stream;
            src->remaining = 0;
            src->pending_len = 0;
            src->pending_pos = 0;

            // give it about 3 seconds receive something
            for (uint8_t i = 0; i < 30; i++) {
                if (stream.available()) break;
                delay(10);
            }

            JRESULT result;
            uint8_t failcount = 0;
            uint64_t stallmillis = millis();
            bool laststream = false;
            while (!laststream) {
                if (!printer3d_state || !printer3d_open_state) {
                    log_i("3dprinter closing connection to video stream at %s", mjpeg_url);
                    break;
                }
                if (!stream.connected() && !stream.available()) {
                    log_w("3dprinter lost connection to video stream at %s", mjpeg_url);
                    break;
                }
                // a half open connection stays connected without ever delivering again
                if (millis() - stallmillis >= PRINTER3D_MJPEG_STALL_TIMEOUT) {
                    log_w("3dprinter got no video frame from %s for %d ms", mjpeg_url, PRINTER3D_MJPEG_STALL_TIMEOUT);
                    break;
                }

                printer3d_mjpeg_update_visible();

                // wait for more frames
                if (!stream.available() && src->pending_pos >= src->pending_len) {
                    log_d("3dprinter waiting for more data in video stream at %s", mjpeg_url);
                    delay(100);
                    continue;
                }

                // consume the boundary and the part header, the stream then stands on the image itself
                size_t framesize = 0;
                if (mode == MJPEG_MODE_MULTIPART) {
                    bool readfailed = false;
                    framesize = printer3d_mjpeg_read_part( src, boundary, &laststream, &readfailed );
                    if (laststream) {
                        log_i("3dprinter reached the end of the video stream at %s", mjpeg_url);
                        break;
                    }
                    if (readfailed) {
                        log_w("3dprinter lost the video stream at %s while reading a part header", mjpeg_url);
                        break;
                    }
                    if (!framesize) log_d("3dprinter received a video frame without a content length");
                } else if (mode == MJPEG_MODE_SINGLE && contentsize > 0) {
                    framesize = contentsize;
                }

                // without a length the end of the frame can only be found by looking for it
                src->remaining = framesize ? framesize : SIZE_MAX;

                // decode frames and notify lvgl image
                result = jd_prepare( decoder, printer3d_mjpeg_input, mjpeg_buffer, PRINTER3D_MJPEG_BUFFER_SIZE, src );
                if (result == JDR_OK) {
                    log_d("3dprinter successfully prepared a %dx%d video frame", decoder->width, decoder->height);

                    // halve the image as long as it still covers the screen, the rest is cropped
                    mjpeg_scale = 0;
                    while ( mjpeg_scale < 3 && ( decoder->width >> ( mjpeg_scale + 1 ) ) >= RES_X_MAX
                                            && ( decoder->height >> ( mjpeg_scale + 1 ) ) >= RES_Y_MAX ) mjpeg_scale++;

                    int32_t scaled_w = decoder->width >> mjpeg_scale;
                    int32_t scaled_h = decoder->height >> mjpeg_scale;

                    // shrink the rest of the way, the smaller step is the one that still covers the screen
                    uint32_t step_x = ( ( uint32_t )scaled_w << 16 ) / RES_X_MAX;
                    uint32_t step_y = ( ( uint32_t )scaled_h << 16 ) / RES_Y_MAX;
                    mjpeg_step = step_x < step_y ? step_x : step_y;
                    if ( mjpeg_step < PRINTER3D_MJPEG_SCALE_ONE ||
                         scaled_w > PRINTER3D_MJPEG_SCALE_MAX || scaled_h > PRINTER3D_MJPEG_SCALE_MAX )
                        mjpeg_step = PRINTER3D_MJPEG_SCALE_ONE;

                    mjpeg_final_w = ( ( uint32_t )scaled_w << 16 ) / mjpeg_step;
                    mjpeg_final_h = ( ( uint32_t )scaled_h << 16 ) / mjpeg_step;
                    mjpeg_crop_x = ( mjpeg_final_w - RES_X_MAX ) / 2;
                    mjpeg_crop_y = ( mjpeg_final_h - RES_Y_MAX ) / 2;

                    int32_t left = -mjpeg_crop_x;
                    int32_t right = left + mjpeg_final_w - 1;
                    if ( left < 0 ) left = 0;
                    if ( right > RES_X_MAX - 1 ) right = RES_X_MAX - 1;
                    mjpeg_dst_x = left;
                    mjpeg_dst_w = right - left + 1;
                    mjpeg_strip_top = -1;
                    mjpeg_strip_h = 0;
                    mjpeg_next_dy = 0;
                    mjpeg_next_dx = 0;

                    log_d("3dprinter shows a %dx%d video frame as %dx%d with scale 1/%d, step %d/65536 and crop %d/%d", decoder->width, decoder->height, mjpeg_final_w, mjpeg_final_h, 1 << mjpeg_scale, mjpeg_step, mjpeg_crop_x, mjpeg_crop_y);

                    result = jd_decomp( decoder, printer3d_mjpeg_output, mjpeg_scale );
                    printer3d_mjpeg_strip_flush();

                    if (result == JDR_OK) {
                        log_d("3dprinter successfully decoded a %dx%d video frame", decoder->width, decoder->height);
                        framecount++;
                        stallmillis = millis();
                    } else {
                        log_d("3dprinter could not decode a video frame");
                    }

                    // tjpgd reads ahead in blocks, take the surplus back or the end of the image is lost
                    if (!framesize && decoder->dctr) {
                        src->pending_len = decoder->dctr > sizeof( src->pending ) ? sizeof( src->pending ) : decoder->dctr;
                        src->pending_pos = 0;
                        memcpy( src->pending, decoder->dptr + 1, src->pending_len );
                    }

                    // give the µC some time to breath after each frame
                    failcount = 0;
                    delay(10);
                } else {
                    // wrong marker or read errors on a flaky connection, never spin without a break
                    // remaining at 0 means the decoder swallowed the whole frame, usually a marker it did not understand
                    log_w("3dprinter could not read a video frame from %s, error %d, length %u, %u bytes left", mjpeg_url, result, (unsigned)framesize, (unsigned)src->remaining);
                    delay(100);
                    if (failcount++ >= 3) break;
                }

                // a single image ends with its connection, there is nothing to step over
                if (mode == MJPEG_MODE_SINGLE) break;

                // step to the start of the next frame
                if (framesize) {
                    if (src->remaining) printer3d_mjpeg_skip( src, src->remaining );
                } else if (!printer3d_mjpeg_seek_eoi( src )) {
                    log_w("3dprinter could not find the next video frame at %s", mjpeg_url);
                    if (failcount++ >= 3) break;
                }
            }
        }

        return framecount;
    }

    static void printer3d_mjpeg_stream(void) {
        // strip buffer, decoder and reader outlive a single connection, plain malloc to keep the hot path out of psram
        mjpeg_frame = (uint8_t*)malloc( RES_X_MAX * PRINTER3D_MJPEG_STRIP_H * sizeof( lv_color_t ) );
        if (mjpeg_frame == nullptr) log_e("3dprinter could not allocate the video strip buffer");

        JDEC* decoder = mjpeg_frame == nullptr ? nullptr : (JDEC*)MALLOC(sizeof(JDEC));
        if (decoder == nullptr) log_e("3dprinter could not allocate the jpeg decoder");

        // the read ahead buffer is too big for the task stack
        mjpeg_stream_src_t* src = decoder == nullptr ? nullptr : (mjpeg_stream_src_t*)malloc(sizeof(mjpeg_stream_src_t));
        if (src == nullptr) log_e("3dprinter could not allocate the video stream reader");

        HTTPClient mjpeg_client;
        mjpeg_client.useHTTP10(true);
        mjpeg_client.setConnectTimeout(1000);
        mjpeg_client.setTimeout(3000);

        // the header array survives every request, the values are wiped by the next one
        const char* headerkeys[] = { "Content-Type" };
        mjpeg_client.collectHeaders( headerkeys, 1 );

        uint8_t failcount = 0;
        uint32_t framecount = 0;
        uint64_t framemillis = millis();
        bool connectfailed = false;
        while (src != nullptr && printer3d_state && printer3d_open_state) {
            // a source that closes after every image is picked up again below
            uint32_t frames = printer3d_mjpeg_session( mjpeg_client, src, decoder, &connectfailed );
            mjpeg_client.end();

            framecount += frames;
            if (millis() - framemillis >= 10000) {
                log_d("3dprinter decoded %d video frames in %d ms", framecount, (uint32_t)(millis() - framemillis));
                framecount = 0;
                framemillis = millis();
            }

            if (frames) failcount = 0;
            else if (++failcount >= 3) {
                log_w("3dprinter gives up on the video stream at %s", mjpeg_url);
                break;
            }

            if (!printer3d_mjpeg_wait( frames ? PRINTER3D_MJPEG_SNAPSHOT : PRINTER3D_MJPEG_RETRY )) break;
        }

        free(src);
        free(decoder);

        if (mjpeg_frame != nullptr) {
            free(mjpeg_frame);
            mjpeg_frame = nullptr;
        }

        if (mjpeg_buffer != nullptr) {
            free(mjpeg_buffer);
            mjpeg_buffer = nullptr;
        }

        printer3d_mjpeg_set_visible( false );
    }

    void printer3d_mjpeg_task(void *parameter) {
        printer3d_mjpeg_stream();

        printer3d_mjpeg_handle = NULL;
        printer3d_mjpeg_running = false;
        vTaskDelete(NULL);
    }

    void printer3d_mjpeg_init( void ) {
        if (!printer3d_state) return;
        if (!printer3d_open_state) return;
        if (printer3d_mjpeg_running) return;

        printer3d_config_t *printer3d_config = printer3d_get_config();
        if (mjpeg_buffer == nullptr && strlen(printer3d_config->camera) > 0) {
            mjpeg_url = printer3d_config->camera;

            mjpeg_buffer = (uint8_t*)malloc(PRINTER3D_MJPEG_BUFFER_SIZE);
            if (mjpeg_buffer == nullptr) {
                log_e("3dprinter could not allocate the video stream buffer");
                return;
            }

            printer3d_mjpeg_running = true;
            if (xTaskCreatePinnedToCore(printer3d_mjpeg_task, "printer3d_mjpeg", 4096, NULL, 1, &printer3d_mjpeg_handle, 1) != pdPASS) {
                log_e("3dprinter could not start the video stream task");
                printer3d_mjpeg_running = false;
                free(mjpeg_buffer);
                mjpeg_buffer = nullptr;
            }
        }
    }
#endif