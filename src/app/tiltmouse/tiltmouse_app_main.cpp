/****************************************************************************
 *   June 04 02:01:00 2021
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
#include <stdlib.h>

#include "tiltmouse_app.h"
#include "tiltmouse_app_main.h"

#include "gui/mainbar/app_tile/app_tile.h"
#include "gui/mainbar/main_tile/main_tile.h"
#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

#include "hardware/blectl.h"
#include "hardware/display.h"
#include "hardware/motion.h"
#include "hardware/pmu.h"
#include "hardware/powermgm.h"

lv_obj_t *tiltmouse_app_main_tile = NULL;
lv_style_t tiltmouse_app_main_style;

NimBLEHIDDevice *pHID = NULL;
NimBLECharacteristic *pHIDMouse = NULL;
lv_task_t * _tiltmouse_app_task;

bool tiltmouse_active = false;
bool tiltmouse_available = false;
uint8_t tiltmouse_button = 0;
static bool tiltmouse_hid_role = false;     /** @brief app has taken the hid role */
static int16_t tiltmouse_acc_x = 0;         /** @brief low pass state */
static int16_t tiltmouse_acc_y = 0;

#define MOUSE_SENSIVITY 0.1
#define MOUSE_SMOOTHING 4                   /** @brief low pass, takes 1/n of each new sample */
#define MOUSE_DEADZONE 5                    /** @brief ignore anything below, in mouse units */
#define MOUSE_NONE 0
#define MOUSE_LEFT 1
#define MOUSE_RIGHT 2

static const uint8_t _hidReportDescriptor[] = {
  USAGE_PAGE(1),       0x01, // USAGE_PAGE (Generic Desktop)
  USAGE(1),            0x02, // USAGE (Mouse)
  COLLECTION(1),       0x01, // COLLECTION (Application)
  USAGE(1),            0x01, //   USAGE (Pointer)
  COLLECTION(1),       0x00, //   COLLECTION (Physical)
  // ------------------------------------------------- Buttons (Left, Right, Middle, Back, Forward)
  USAGE_PAGE(1),       0x09, //     USAGE_PAGE (Button)
  USAGE_MINIMUM(1),    0x01, //     USAGE_MINIMUM (Button 1)
  USAGE_MAXIMUM(1),    0x05, //     USAGE_MAXIMUM (Button 5)
  LOGICAL_MINIMUM(1),  0x00, //     LOGICAL_MINIMUM (0)
  LOGICAL_MAXIMUM(1),  0x01, //     LOGICAL_MAXIMUM (1)
  REPORT_SIZE(1),      0x01, //     REPORT_SIZE (1)
  REPORT_COUNT(1),     0x05, //     REPORT_COUNT (5)
  HIDINPUT(1),         0x02, //     INPUT (Data, Variable, Absolute) ;5 button bits
  // ------------------------------------------------- Padding
  REPORT_SIZE(1),      0x03, //     REPORT_SIZE (3)
  REPORT_COUNT(1),     0x01, //     REPORT_COUNT (1)
  HIDINPUT(1),         0x03, //     INPUT (Constant, Variable, Absolute) ;3 bit padding
  // ------------------------------------------------- X/Y position, Wheel
  USAGE_PAGE(1),       0x01, //     USAGE_PAGE (Generic Desktop)
  USAGE(1),            0x30, //     USAGE (X)
  USAGE(1),            0x31, //     USAGE (Y)
  USAGE(1),            0x38, //     USAGE (Wheel)
  LOGICAL_MINIMUM(1),  0x81, //     LOGICAL_MINIMUM (-127)
  LOGICAL_MAXIMUM(1),  0x7f, //     LOGICAL_MAXIMUM (127)
  REPORT_SIZE(1),      0x08, //     REPORT_SIZE (8)
  REPORT_COUNT(1),     0x03, //     REPORT_COUNT (3)
  HIDINPUT(1),         0x06, //     INPUT (Data, Variable, Relative) ;3 bytes (X,Y,Wheel)
  // ------------------------------------------------- Horizontal wheel
  USAGE_PAGE(1),       0x0c, //     USAGE PAGE (Consumer Devices)
  USAGE(2),      0x38, 0x02, //     USAGE (AC Pan)
  LOGICAL_MINIMUM(1),  0x81, //     LOGICAL_MINIMUM (-127)
  LOGICAL_MAXIMUM(1),  0x7f, //     LOGICAL_MAXIMUM (127)
  REPORT_SIZE(1),      0x08, //     REPORT_SIZE (8)
  REPORT_COUNT(1),     0x01, //     REPORT_COUNT (1)
  HIDINPUT(1),         0x06, //     INPUT (Data, Var, Rel)
  END_COLLECTION(0),         //   END_COLLECTION
  END_COLLECTION(0)          // END_COLLECTION
};

void tiltmouse_app_task( lv_task_t * task );
bool tiltmouse_pmuctl_event_cb(EventBits_t event, void *arg);
bool tiltmouse_powermgm_event_cb(EventBits_t event, void *arg);
bool tiltmouse_blectl_event_cb(EventBits_t event, void *arg);
static void tiltmouse_activate_cb( void );
static void tiltmouse_hibernate_cb( void );
static void tiltmouse_restart_advertising( void );
void tiltmouse_left_event_cb( lv_obj_t * obj, lv_event_t event );
void tiltmouse_right_event_cb( lv_obj_t * obj, lv_event_t event );
void tiltmouse_move(signed char x, signed char y, signed char wheel, signed char hWheel);
void tiltmouse_battery(int32_t percent);

void tiltmouse_app_main_setup( uint32_t tile_num ) {
    tiltmouse_app_main_tile = mainbar_get_tile_obj( tile_num );
    lv_style_copy( &tiltmouse_app_main_style, ws_get_mainbar_style() );

    lv_obj_t * exit_btn = wf_add_exit_button( tiltmouse_app_main_tile, exit_tiltmouse_app_event_cb, &tiltmouse_app_main_style );
    lv_obj_align(exit_btn, tiltmouse_app_main_tile, LV_ALIGN_IN_BOTTOM_LEFT, 10, -10 );

    // left mouse button
	lv_obj_t *tiltmouse_left_btn = wf_add_button( tiltmouse_app_main_tile, "Left", 75, 150, tiltmouse_left_event_cb );
    lv_obj_align( tiltmouse_left_btn, NULL, LV_ALIGN_CENTER, -40, 0 );
    lv_btn_set_checkable(tiltmouse_left_btn, false);
    
    // right mouse button
	lv_obj_t *tiltmouse_right_btn = wf_add_button( tiltmouse_app_main_tile, "Right", 75, 150, tiltmouse_right_event_cb );
    lv_obj_align( tiltmouse_right_btn, NULL, LV_ALIGN_CENTER, 40, 0 );
    lv_btn_set_checkable(tiltmouse_right_btn, false);

    // create an task that runs every 50ms
    _tiltmouse_app_task = lv_task_create( tiltmouse_app_task, 50, LV_TASK_PRIO_HIGH, NULL );

    mainbar_add_tile_activate_cb( tile_num, tiltmouse_activate_cb );
    mainbar_add_tile_hibernate_cb( tile_num, tiltmouse_hibernate_cb );

    pmu_register_cb( PMUCTL_STATUS, tiltmouse_pmuctl_event_cb, "tiltmouse pmu");
    powermgm_register_cb( POWERMGM_STANDBY, tiltmouse_powermgm_event_cb, "tiltmouse powermgm");
    blectl_register_cb( BLECTL_SETUP | BLECTL_CONNECT | BLECTL_DISCONNECT | BLECTL_ON | BLECTL_OFF, tiltmouse_blectl_event_cb, "tiltmouse bluetooth" );
}

void tiltmouse_app_task( lv_task_t * task )
{
    if ( !tiltmouse_active || !tiltmouse_available ) return;
    
    int16_t acc_x = 0;
    int16_t acc_y = 0;
    int16_t acc_z = 0;

    if ( !bma_get_accel( acc_x, acc_y, acc_z ) ) return;

    // simple low pass
    tiltmouse_acc_x += ( acc_x - tiltmouse_acc_x ) / MOUSE_SMOOTHING;
    tiltmouse_acc_y += ( acc_y - tiltmouse_acc_y ) / MOUSE_SMOOTHING;

    int16_t x = 0;
    int16_t y = 0;
    switch( display_get_rotation() ) {
        case 90:    x =  tiltmouse_acc_y; y = -tiltmouse_acc_x; break;
        case 180:   x =  tiltmouse_acc_x; y =  tiltmouse_acc_y; break;
        case 270:   x = -tiltmouse_acc_y; y =  tiltmouse_acc_x; break;
        default:    x = -tiltmouse_acc_x; y = -tiltmouse_acc_y; break;
    }

    x = x * MOUSE_SENSIVITY;
    y = y * MOUSE_SENSIVITY;
    if ( abs( x ) < MOUSE_DEADZONE ) x = 0;
    if ( abs( y ) < MOUSE_DEADZONE ) y = 0;
    if ( x > 127 ) x = 127; else if ( x < -127 ) x = -127;
    if ( y > 127 ) y = 127; else if ( y < -127 ) y = -127;

    tiltmouse_move( x, y, 0, 0 );
    lv_disp_trig_activity( NULL );
}

void tiltmouse_init()
{
    if (pHID != NULL && pHIDMouse != NULL) return;

    NimBLEServer *pServer = blectl_get_ble_server();

    pHID = new NimBLEHIDDevice(pServer);
    pHIDMouse = pHID->inputReport(0); // <-- input REPORTID from report map

    pHID->manufacturer()->setValue("Lily Go");
    pHID->pnp(0x02, 0xe502, 0xa111, 0x0210);
    pHID->hidInfo(0x00,0x02);

    pHID->reportMap((uint8_t*)_hidReportDescriptor, sizeof(_hidReportDescriptor));
    pHID->startServices();
}

/**
 * @brief republish the advertising data after the app changed appearance or service uuids
 */
static void tiltmouse_restart_advertising( void )
{
    BLEAdvertising *pAdvertising = blectl_get_ble_advertising();

    pAdvertising->stop();
    if (tiltmouse_available && blectl_get_advertising())
        pAdvertising->start();

    log_d("tiltmouse advertising: %d", pAdvertising->isAdvertising() );
}

static void tiltmouse_activate_cb( void )
{
    if (tiltmouse_hid_role || !pHID || !pHIDMouse) return;

    BLEAdvertising *pAdvertising = blectl_get_ble_advertising();
    pAdvertising->addServiceUUID(pHID->hidService()->getUUID());
    pAdvertising->setAppearance( HID_MOUSE );
    tiltmouse_restart_advertising();

    tiltmouse_acc_x = 0;
    tiltmouse_acc_y = 0;

    log_d("tiltmouse HID report handle: 0x%04x", pHIDMouse->getHandle() );
    tiltmouse_hid_role = true;
    tiltmouse_active = true;
}

static void tiltmouse_hibernate_cb( void )
{
    tiltmouse_active = false;
    if (!tiltmouse_hid_role) return;

    BLEAdvertising *pAdvertising = blectl_get_ble_advertising();
    pAdvertising->removeServiceUUID(pHID->hidService()->getUUID());
    pAdvertising->setAppearance( 0x00c0 );
    tiltmouse_restart_advertising();

    log_d("tiltmouse HID role released");
    tiltmouse_hid_role = false;
}

void tiltmouse_move(signed char x, signed char y, signed char wheel, signed char hWheel)
{
    if ( !pHIDMouse ) return;

    uint8_t m[5];
    m[0] = tiltmouse_button;
    m[1] = x;
    m[2] = y;
    m[3] = wheel;
    m[4] = hWheel;
    pHIDMouse->setValue(m, 5);
    pHIDMouse->notify();
}

void tiltmouse_battery(int32_t percent)
{
    if ( !tiltmouse_active || !tiltmouse_available ) return;

    uint8_t level = (uint8_t)percent;
    if ( level > 100 ) {
        level = 100;
    }
    
    pHID->setBatteryLevel(level);
}

bool tiltmouse_powermgm_event_cb(EventBits_t event, void *arg)
{
    switch( event ) {
        case( POWERMGM_STANDBY ):
            tiltmouse_hibernate_cb();
            break;
    }
    return( true );
}

bool tiltmouse_pmuctl_event_cb( EventBits_t event, void *arg )
{
    switch( event ) {
        case PMUCTL_STATUS:
            int32_t percent = *(int32_t*)arg & PMUCTL_STATUS_PERCENT;
            tiltmouse_battery(percent);
            break;
    }
    return( true );
}

bool tiltmouse_blectl_event_cb(EventBits_t event, void *arg)
{
    switch( event ) {
        case BLECTL_SETUP:          tiltmouse_init();
                                    break;
        case BLECTL_ON:             tiltmouse_available = true;
                                    break;
        case BLECTL_OFF:            tiltmouse_available = false;
                                    break;
    }
    return (true);
}

void tiltmouse_left_event_cb( lv_obj_t * obj, lv_event_t event ) 
{
    switch( event ) {
        case( LV_EVENT_PRESSED ):       tiltmouse_button = MOUSE_LEFT;
                                        tiltmouse_move( 0, 0, 0, 0 );
                                        break;
        case( LV_EVENT_RELEASED ):      tiltmouse_button = MOUSE_NONE;
                                        tiltmouse_move( 0, 0, 0, 0 );
                                        break;
    }
}

void tiltmouse_right_event_cb( lv_obj_t * obj, lv_event_t event ) 
{
    switch( event ) {
        case( LV_EVENT_PRESSED ):       tiltmouse_button = MOUSE_RIGHT;
                                        tiltmouse_move( 0, 0, 0, 0 );
                                        break;
        case( LV_EVENT_RELEASED ):      tiltmouse_button = MOUSE_NONE;
                                        tiltmouse_move( 0, 0, 0, 0 );
                                        break;
    }
}