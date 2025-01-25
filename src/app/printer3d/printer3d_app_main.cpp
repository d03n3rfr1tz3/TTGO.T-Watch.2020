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

#include "gui/mainbar/app_tile/app_tile.h"
#include "gui/mainbar/main_tile/main_tile.h"
#include "gui/mainbar/mainbar.h"
#include "gui/sjpg_decoder/tjpgd.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

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

static uint8_t* mjpeg_buffer = nullptr;
static uint8_t* mjpeg_frame = nullptr;
static char* mjpeg_url;

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
    static lv_img_dsc_t printer3d_video;
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
#endif
printer3d_result_t printer3d_refresh_result;

void printer3d_refresh(void *parameter);
void printer3d_send(WiFiClient client, char* buffer, const char* command);
void printer3d_app_task( lv_task_t * task );
void printer3d_mjpeg_init( void );

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

        lv_obj_t * video_exit_btn = wf_add_exit_button( printer3d_app_video_tile, exit_printer3d_app_main_event_cb );
        lv_obj_align(video_exit_btn, printer3d_app_video_tile, LV_ALIGN_IN_BOTTOM_LEFT, THEME_ICON_PADDING, -THEME_ICON_PADDING );
    #endif

    // callbacks
    powermgm_register_cb( POWERMGM_STANDBY | POWERMGM_STANDBY_REQUEST, printer3d_powermgm_event_cb, "printer3d powermgm");
    wifictl_register_cb( WIFICTL_OFF | WIFICTL_CONNECT_IP | WIFICTL_DISCONNECT, printer3d_main_wifictl_event_cb, "printer3d main" );

    // create an task that runs every second
    _printer3d_app_task = lv_task_create( printer3d_app_task, 1000, LV_TASK_PRIO_MID, NULL );
}

static void printer3d_setup_activate_callback ( void ) {
    printer3d_open_state = true;
    nextmillis = 0;
}

static void printer3d_setup_hibernate_callback ( void ) {
    printer3d_open_state = false;
    nextmillis = 0;
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
        case( LV_EVENT_CLICKED ):       mainbar_jump_to_tilenumber( printer3d_app_get_app_setup_tile_num(), LV_ANIM_ON );
                                        statusbar_hide( true );
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

        printer3d_app_set_indicator( ICON_INDICATOR_UPDATE );
        #ifdef NATIVE_64BIT
            printer3d_refresh( NULL );
        #else
            xTaskCreatePinnedToCore(printer3d_refresh, "printer3d_refresh", 2500, NULL, 0, &printer3d_refresh_handle, 1);
        #endif

        if (printer3d_open_state) {
            printer3d_mjpeg_init();
        }
    }

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

void printer3d_refresh(void *parameter) {
    if (!printer3d_state) return;

    printer3d_config_t *printer3d_config = printer3d_get_config();
    if (!strlen(printer3d_config->host)) {
        printer3d_refresh_result.changed = true;
        printer3d_refresh_result.success = false;
        #ifndef NATIVE_64BIT
            vTaskDelete(NULL);
        #endif
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
        printer3d_refresh_result.changed = true;
        printer3d_refresh_result.success = false;
        #ifndef NATIVE_64BIT
            vTaskDelete(NULL);
        #endif
        return;
    } else {
        log_i("printer3d: connected to %s:%d", printer3d_config->host, printer3d_config->port);
    }

    // sending G-Codes to 3d printer
    char* esp3dInfo = (char*)MALLOC( 1024 );
    char* generalInfo = (char*)MALLOC( 1024 );
    char* stateInfo = (char*)MALLOC( 1024 );
    char* tempInfo = (char*)MALLOC( 512 );
    char* printInfo = (char*)MALLOC( 512 );

    if (strlen(printer3d_config->pass) > 0) {
        char val[30];
        snprintf( val, sizeof(val), "[ESP800]pwd=%s", printer3d_config->pass );
        printer3d_send(client, esp3dInfo, val);
    }
    printer3d_send(client, generalInfo, "~M115");
    printer3d_send(client, stateInfo, "~M119");
    printer3d_send(client, tempInfo, "~M105");
    printer3d_send(client, printInfo, "~M27");

    // close connection
    client.stop();

    // parse received information from the 3d printer
    if (esp3dInfo != NULL && strlen(esp3dInfo) > 0) {
        char machineType[32], machineVersion[16];

        char* esp3dInfoType1 = strstr(esp3dInfo, "FW target");
        if ( esp3dInfoType1 != NULL && strlen(esp3dInfoType1) > 0 && sscanf( esp3dInfoType1, "FW target: %s", machineType ) > 0 ) {
            for (uint8_t i = 0; i < strlen(machineType); i++) printer3d_refresh_result.machineType[i] = machineType[i];
        }

        char* esp3dInfoType2 = strstr(esp3dInfo, "hostname");
        if ( esp3dInfoType2 != NULL && strlen(esp3dInfoType2) > 0 && sscanf( esp3dInfoType2, "hostname: %s", machineType ) > 0 ) {
            for (uint8_t i = 0; i < strlen(machineType); i++) printer3d_refresh_result.machineType[i] = machineType[i];
        }

        char* esp3dInfoVersion = strstr(esp3dInfo, "FW version:");
        if ( esp3dInfoVersion != NULL && strlen(esp3dInfoVersion) > 0 && sscanf( esp3dInfoVersion, "FW version: %s", machineVersion ) > 0 ) {
            for (uint8_t i = 0; i < strlen(machineVersion); i++) printer3d_refresh_result.machineVersion[i] = machineVersion[i];
        }
    }
    free( esp3dInfo );
    
    if (generalInfo != NULL && strlen(generalInfo) > 0) {
        char machineType[32], machineVersion[16];

        char* generalInfoType1 = strstr(generalInfo, "Machine Type:");
        if ( generalInfoType1 != NULL && strlen(generalInfoType1) > 0 && sscanf( generalInfoType1, "Machine Type: %[a-zA-Z0-9- ]", machineType ) > 0 ) {
            for (uint8_t i = 0; i < strlen(machineType); i++) printer3d_refresh_result.machineType[i] = machineType[i];
        }

        char* generalInfoType2 = strstr(generalInfo, "FIRMWARE_NAME");
        if ( generalInfoType2 != NULL && strlen(generalInfoType2) > 0 && sscanf( generalInfoType2, "FIRMWARE_NAME: %s", machineType ) > 0 ) {
            for (uint8_t i = 0; i < strlen(machineType); i++) printer3d_refresh_result.machineType[i] = machineType[i];
        }

        char* generalInfoVersion1 = strstr(generalInfo, "Firmware:");
        if ( generalInfoVersion1 != NULL && strlen(generalInfoVersion1) > 0 && sscanf( generalInfoVersion1, "Firmware: %s", machineVersion ) > 0 ) {
            for (uint8_t i = 0; i < strlen(machineVersion); i++) printer3d_refresh_result.machineVersion[i] = machineVersion[i];
        }

        char* generalInfoVersion2 = strstr(generalInfo, "FIRMWARE_VERSION:");
        if ( generalInfoVersion2 != NULL && strlen(generalInfoVersion2) > 0 && sscanf( generalInfoVersion2, "FIRMWARE_VERSION: %s", machineVersion ) > 0 ) {
            for (uint8_t i = 0; i < strlen(machineVersion); i++) printer3d_refresh_result.machineVersion[i] = machineVersion[i];
        }
    }
    free( generalInfo );
    
    if (stateInfo != NULL && strlen(stateInfo) > 0) {
        char stateMachine[16], stateMove[16];

        char* stateInfoMachine = strstr(stateInfo, "MachineStatus:");
        if ( stateInfoMachine != NULL && strlen(stateInfoMachine) > 0 && sscanf( stateInfoMachine, "MachineStatus: %[a-zA-Z]", stateMachine ) > 0 ) {
            for (uint8_t i = 0; i < strlen(stateMachine); i++) printer3d_refresh_result.stateMachine[i] = stateMachine[i];
        }

        char* stateInfoMove = strstr(stateInfo, "MoveMode:");
        if ( stateInfoMove != NULL && strlen(stateInfoMove) > 0 && sscanf( stateInfoMove, "MoveMode: %[a-zA-Z]", stateMove ) > 0 ) {
            for (uint8_t i = 0; i < strlen(stateMove); i++) printer3d_refresh_result.stateMove[i] = stateMove[i];
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
    #ifndef NATIVE_64BIT
        vTaskDelete(NULL);
    #endif
}

void printer3d_send(WiFiClient client, char* buffer, const char* command) {
    if (!printer3d_state) return;
    if (!client.connected()) return;

    client.write(command);
    log_d("3dprinter sent command: %s", command);
    
    for (uint8_t i = 0; i < 25; i++) {
        if (client.available()) break;
        delay(10);
    }
    
    while (client.available()) {
        client.readBytes(buffer, 512);
    }
    
    log_d("3dprinter received: %s", buffer);
}

#ifndef NATIVE_64BIT
    static uint32_t printer3d_mjpeg_input(JDEC* decoder, uint8_t* buffer, uint32_t size) {
        WiFiClient* stream = (WiFiClient*)decoder->device;

        // read from the image stream
        if (buffer) {
            return stream->readBytes(buffer, size);
        } else {
            char temp[size];
            return stream->readBytes(temp, size);
        }

        return 0;
    }

    static int32_t printer3d_mjpeg_output(JDEC* decoder, void* data, JRECT* rect) {
        uint8_t* buffer = (uint8_t*)data;
        const uint16_t row_width = rect->right - rect->left + 1;

        // write partial decoded image into frame buffer
        for ( uint16_t y = rect->top; y <= rect->bottom; y++ ) {
            if (!printer3d_state || !printer3d_open_state) break;
            if (!decoder->width || !decoder->height) break;
            if (!mjpeg_frame) break;

            // convert raw output into the corresponding color depth pixels
            #if LV_COLOR_DEPTH == 32
                uint8_t pixelsize = 4;
                uint32_t offset = y * decoder->width * pixelsize + rect->left * pixelsize;
                for ( uint32_t i = 0; i < row_width; i++ ) {
                    mjpeg_frame[offset + 3] = 0xff;
                    mjpeg_frame[offset + 2] = *buffer++;
                    mjpeg_frame[offset + 1] = *buffer++;
                    mjpeg_frame[offset + 0] = *buffer++;
                    offset += 4;
                }
            #elif LV_COLOR_DEPTH == 16
                uint8_t pixelsize = 2;
                uint32_t offset = y * decoder->width * pixelsize + rect->left * pixelsize;
                for ( uint32_t i = 0; i < row_width; i++ ) {
                    uint32_t col_16bit = (*buffer++ & 0xf8) << 8;
                    col_16bit |= (*buffer++ & 0xFC) << 3;
                    col_16bit |= (*buffer++ >> 3);
                    #ifdef LV_BIG_ENDIAN_SYSTEM
                        mjpeg_frame[offset++] = col_16bit >> 8;
                        mjpeg_frame[offset++] = col_16bit & 0xff;
                    #else
                        mjpeg_frame[offset++] = col_16bit & 0xff;
                        mjpeg_frame[offset++] = col_16bit >> 8;
                    #endif
                }
            #elif LV_COLOR_DEPTH == 8
                uint8_t pixelsize = 1;
                uint32_t offset = y * decoder->width * pixelsize + rect->left * pixelsize;
                for ( uint32_t i = 0; i < row_width; i++ ) {
                    uint8_t col_8bit = (*buffer++ & 0xC0);
                    col_8bit |= (*buffer++ & 0xe0) >> 2;
                    col_8bit |= (*buffer++ & 0xe0) >> 5;
                    mjpeg_frame[offset++] = col_8bit;
                }
            #else
                #error Unsupported LV_COLOR_DEPTH
            #endif
        }

        return 1;
    }

    void printer3d_mjpeg_task(void *parameter) {
        HTTPClient mjpeg_client;
        mjpeg_client.useHTTP10(true);
        mjpeg_client.setConnectTimeout(1000);
        mjpeg_client.setTimeout(3000);
        mjpeg_client.begin(mjpeg_url);

        int httpcode = mjpeg_client.GET();
        if (httpcode < 200 || httpcode >= 400) {
            log_w("3dprinter could not connect to video stream at %s", mjpeg_url);
            
            if (mjpeg_buffer != nullptr) {
                free(mjpeg_buffer);
                mjpeg_buffer = nullptr;
            }
            vTaskDelete(NULL);
        } else {
            log_i("3dprinter connected to video stream at %s", mjpeg_url);

            // prepare stream
            WiFiClient stream = mjpeg_client.getStream();
            stream.setTimeout(5);

            // give it about 3 seconds receive something
            for (uint8_t i = 0; i < 30; i++) {
                if (stream.available()) break;
                delay(10);
            }

            // prepare frame buffer and decoder
            JDEC* decoder = (JDEC*)MALLOC(sizeof(JDEC));
            JRESULT result;

            uint8_t failcount = 0;
            while (true) {
                if (!printer3d_state || !printer3d_open_state) {
                    log_i("3dprinter closing connection to video stream at %s", mjpeg_url);
                    break;
                }
                if (!stream.connected()) {
                    log_w("3dprinter lost connection to video stream at %s", mjpeg_url);
                    break;
                }

                // wait for more frames
                if (!stream.available()) {
                    log_d("3dprinter waiting for more data in video stream at %s", mjpeg_url);
                    delay(100);
                    continue;
                }

                // decode frames and notify lvgl image
                result = jd_prepare( decoder, printer3d_mjpeg_input, mjpeg_buffer, PRINTER3D_MJPEG_BUFFER_SIZE, &(stream) );
                if (result == JDR_OK) {
                    log_d("3dprinter successfully prepared a %dx%d video frame", decoder->width, decoder->height);

                    // use decoded size to allocate memory
                    bool first_frame = false;
                    size_t mjpeg_size = decoder->width * decoder->height * LV_COLOR_DEPTH / 8;
                    if (mjpeg_frame == nullptr) {
                        mjpeg_frame = (uint8_t*)MALLOC(mjpeg_size);
                        first_frame = true;
                    }
                    
                    result = jd_decomp( decoder, printer3d_mjpeg_output, 0 );
                    if (result == JDR_OK) {
                        log_d("3dprinter successfully decoded a %dx%d video frame with %d bytes", decoder->width, decoder->height, mjpeg_size);

                        printer3d_video.header.always_zero = 0;
                        printer3d_video.header.cf = LV_IMG_CF_TRUE_COLOR;
                        printer3d_video.header.w = decoder->width;
                        printer3d_video.header.h = decoder->height;
                        printer3d_video.data = mjpeg_frame;
                        printer3d_video.data_size = mjpeg_size;

                        if (first_frame) {
                            bool landscape = decoder->width > decoder->height;
                            uint8_t ratio = RES_Y_MAX * 100 / RES_X_MAX;
                            uint16_t maxX = landscape ? (decoder->height * 100 / ratio) : decoder->width;
                            uint16_t maxY = landscape ? decoder->height : (decoder->width * 100 / ratio);
                            uint8_t zoomFactor = (landscape ? RES_Y_MAX : RES_X_MAX) * 100 / (landscape ? decoder->height : decoder->width);

                            lv_obj_set_hidden( printer3d_video_img, true );
                            lv_img_set_src( printer3d_video_img, &printer3d_video );
                            lv_img_set_antialias( printer3d_video_img, false );
                            lv_img_set_auto_size( printer3d_video_img, false );
                            lv_obj_set_size( printer3d_video_img, decoder->width, decoder->height );
                            lv_img_set_zoom( printer3d_video_img, LV_IMG_ZOOM_NONE * zoomFactor / 100 );
                            lv_img_set_pivot( printer3d_video_img, 0, 0 );
                            lv_img_set_offset_x( printer3d_video_img, landscape ? (maxX - decoder->width) / 2 : 0 );
                            lv_img_set_offset_y( printer3d_video_img, landscape ? 0 : (maxY - decoder->height) / 2 );
                            lv_obj_align( printer3d_video_img, printer3d_app_video_tile, LV_ALIGN_IN_TOP_LEFT, 0, 0 );
                            lv_obj_set_hidden( printer3d_video_img, false );
                        } else {
                            lv_obj_invalidate( printer3d_video_img );
                        }
                    } else {
                        log_d("3dprinter could not decode a video frame");
                    }

                    // give the µC some time to breath after each frame
                    failcount = 0;
                    delay(10);
                } else if (result == JDR_FMT1 || result == JDR_FMT2 || result == JDR_FMT3) {
                    // close connection after multiple wrong frames
                    if (result == JDR_FMT2 || result == JDR_FMT3) {
                        log_w("3dprinter received not supported format or JPEG from %s", mjpeg_url);
                        if (failcount++ >= 3) break;
                    } else log_d("3dprinter needs to find the next video frame");

                    // try to jump to the next end-of-image 0xFFD9 marker
                    bool found = false;
                    while ( !found && printer3d_state && printer3d_open_state && stream.connected() ) {
                        uint16_t available = stream.available();
                        if (!available) {
                            delay(1);
                            continue;
                        }

                        for ( uint16_t i = 0; i < available; i++ ) {
                            if (stream.read() != 0xFF) continue;
                            if (stream.read() != 0xD9) continue;

                            log_d("3dprinter found the next video frame");
                            found = true;
                            break;
                        }
                    }
                }
            }

            if (mjpeg_frame != nullptr) {
                free(mjpeg_frame);
                mjpeg_frame = nullptr;
            }

            free(decoder);
        }

        if (mjpeg_buffer != nullptr) {
            free(mjpeg_buffer);
            mjpeg_buffer = nullptr;
        }

        mjpeg_client.end();
        vTaskDelete(NULL);
    }
#endif

void printer3d_mjpeg_init( void ) {
    if (!printer3d_state) return;
    if (!printer3d_open_state) return;

    #ifdef NATIVE_64BIT
        return;
    #endif

    printer3d_config_t *printer3d_config = printer3d_get_config();
    if (mjpeg_buffer == nullptr && strlen(printer3d_config->camera) > 0) {
        mjpeg_url = printer3d_config->camera;

        mjpeg_buffer = (uint8_t*)malloc(PRINTER3D_MJPEG_BUFFER_SIZE);
        xTaskCreatePinnedToCore(printer3d_mjpeg_task, "printer3d_mjpeg", 2500, NULL, 0, &printer3d_mjpeg_handle, 1);
    }
}