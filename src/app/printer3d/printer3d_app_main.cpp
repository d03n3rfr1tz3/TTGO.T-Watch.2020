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
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

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

lv_obj_t *printer3d_app_main_tile = NULL;

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

LV_IMG_DECLARE(refresh_32px);

LV_FONT_DECLARE(Ubuntu_12px);
LV_FONT_DECLARE(Ubuntu_16px);

static void printer3d_setup_activate_callback ( void );
static void printer3d_setup_hibernate_callback ( void );
static void exit_printer3d_app_main_event_cb( lv_obj_t * obj, lv_event_t event );
static void enter_printer3d_app_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static bool printer3d_main_wifictl_event_cb( EventBits_t event, void *arg );

#ifndef NATIVE_64BIT
    TaskHandle_t printer3d_refresh_handle;
#endif
printer3d_result_t printer3d_refresh_result;

void printer3d_refresh(void *parameter);
void printer3d_send(WiFiClient client, char* buffer, const char* command);
void printer3d_app_task( lv_task_t * task );

void printer3d_app_main_setup( uint32_t tile_num ) {

    mainbar_add_tile_activate_cb( tile_num, printer3d_setup_activate_callback );
    mainbar_add_tile_hibernate_cb( tile_num, printer3d_setup_hibernate_callback );
    printer3d_app_main_tile = mainbar_get_tile_obj( tile_num );

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

    // callbacks
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
            nextmillis = millis() + 15000L;
        } else {
            nextmillis = millis() + 60000L;
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
    }

    if (printer3d_refresh_result.changed) {
        printer3d_refresh_result.changed = false;
        char val[32];

        snprintf( val, sizeof(val), "%s", printer3d_refresh_result.machineType );
        lv_label_set_text(printer3d_heading_name, val);

        snprintf( val, sizeof(val), "%s", printer3d_refresh_result.machineVersion );
        lv_label_set_text(printer3d_heading_version, val);

        snprintf( val, sizeof(val), "%s", printer3d_refresh_result.stateMachine );
        lv_label_set_text(printer3d_progress_state, val);

        if (printer3d_refresh_result.extruderTemp >= 0 && printer3d_refresh_result.extruderTemp <= 350) {
            snprintf( val, sizeof(val), "%d°C", printer3d_refresh_result.extruderTemp );
            lv_label_set_text(printer3d_extruder_temp, val);
        }
        
        if (printer3d_refresh_result.printbedTemp >= 0 && printer3d_refresh_result.printbedTemp <= 100) {
            snprintf( val, sizeof(val), "%d°C", printer3d_refresh_result.printbedTemp );
            lv_label_set_text(printer3d_printbed_temp, val);
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
    client.connect(printer3d_config->host, printer3d_config->port, 3000);

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
    }

    // sending G-Codes to 3d printer
    char* generalInfo = (char*)MALLOC( 1024 );
    char* stateInfo = (char*)MALLOC( 1024 );
    char* tempInfo = (char*)MALLOC( 512 );
    char* printInfo = (char*)MALLOC( 512 );
    printer3d_send(client, generalInfo, "~M115");
    printer3d_send(client, stateInfo, "~M119");
    printer3d_send(client, tempInfo, "~M105");
    printer3d_send(client, printInfo, "~M27");

    // close connection
    client.stop();

    // parse received information from the 3d printer
    if (generalInfo != NULL && strlen(generalInfo) > 0) {
        char machineType[32], machineVersion[16];

        char* generalInfoType = strstr(generalInfo, "Machine Type:");
        if ( generalInfoType != NULL && strlen(generalInfoType) > 0 && sscanf( generalInfoType, "Machine Type: %[a-zA-Z0-9- ]", machineType ) > 0 ) {
            for (uint8_t i = 0; i < strlen(machineType); i++) printer3d_refresh_result.machineType[i] = machineType[i];
        }

        char* generalInfoVersion = strstr(generalInfo, "Firmware:");
        if ( generalInfoVersion != NULL && strlen(generalInfoVersion) > 0 && sscanf( generalInfoVersion, "Firmware: %s", machineVersion ) > 0 ) {
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
        int extruderTemp, printbedTemp;
        
        char* tempInfoLine = strstr(tempInfo, "T0:");
        if ( tempInfoLine != NULL && strlen(tempInfoLine) > 0 && sscanf( tempInfoLine, "T0:%d /%*d B:%d/%*d", &extruderTemp, &printbedTemp ) > 0 ) {
            printer3d_refresh_result.extruderTemp = extruderTemp;
            printer3d_refresh_result.printbedTemp = printbedTemp;
        }
    }
    free( tempInfo );
    
    if (printInfo != NULL && strlen(printInfo) > 0) {
        int printProgress, printMax;
        
        char* printInfoLine = strstr(printInfo, "byte ");
        if ( printInfoLine != NULL && strlen(printInfoLine) > 0 && sscanf( printInfoLine, "byte %d/%d", &printProgress, &printMax ) > 0 ) {
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
