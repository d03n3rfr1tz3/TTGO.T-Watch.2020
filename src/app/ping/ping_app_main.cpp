/****************************************************************************
 *   linuxthor 2020
 *   ttgo watch ping utility
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
    #include <WiFi.h>
    #include <lwip/sockets.h>
    #include <lwip/icmp.h>
    #include <lwip/ip.h>
    #include <lwip/inet_chksum.h>
    #include <ESP32Ping.h>
#endif

#include "ping_app.h"
#include "ping_app_main.h"

#include "gui/mainbar/app_tile/app_tile.h"
#include "gui/mainbar/main_tile/main_tile.h"
#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/keyboard.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

LV_IMG_DECLARE( ping_app_32px );

#ifdef NATIVE_64BIT
    #define PING_LOCK()
    #define PING_UNLOCK()
#else
    static portMUX_TYPE ping_mux = portMUX_INITIALIZER_UNLOCKED;
    #define PING_LOCK()     portENTER_CRITICAL( &ping_mux )
    #define PING_UNLOCK()   portEXIT_CRITICAL( &ping_mux )

    static TaskHandle_t ping_worker_handle = NULL;
#endif

static volatile bool ping_worker_running = false;
static volatile bool ping_worker_abort = false;
static volatile bool ping_result_changed = false;

static char ping_result_text[ PING_RESULT_LEN ] = "";
static char ping_target[ PING_TARGET_LEN ] = "";
static uint16_t ping_target_port = 80;
static ping_tool_t ping_tool = PING_TOOL_PING;

lv_obj_t *ping_app_main_tile = NULL;
lv_obj_t *ping_tool_list = NULL;
lv_obj_t *ping_host_cont = NULL;
lv_obj_t *ping_ip_textfield = NULL;
lv_obj_t *ping_port_cont = NULL;
lv_obj_t *ping_port_textfield = NULL;
lv_obj_t *ping_result_page = NULL;
lv_obj_t *ping_result_label = NULL;
lv_obj_t *ping_start_btn = NULL;
lv_obj_t *ping_exit_btn = NULL;

static lv_task_t *ping_app_lv_task = NULL;
static bool ping_start_btn_is_stop = false;

static void exit_ping_app_main_event_cb( lv_obj_t * obj, lv_event_t event );
static void ping_textarea_event_cb( lv_obj_t * obj, lv_event_t event );
static void ping_port_textarea_event_cb( lv_obj_t * obj, lv_event_t event );
static void ping_tool_event_cb( lv_obj_t * obj, lv_event_t event );
static void ping_start_event_cb( lv_obj_t * obj, lv_event_t event );
static void ping_app_activate_cb( void );
static void ping_app_hibernate_cb( void );
static void ping_app_task( lv_task_t * task );
static void ping_app_layout_result( void );
static void ping_app_set_start_btn( bool stop );
static void ping_result_clear( void );
static void ping_result_append( const char *format, ... );
static void ping_worker_start( void );

void ping_app_main_setup( uint32_t tile_num ) {

    mainbar_add_tile_activate_cb( tile_num, ping_app_activate_cb );
    mainbar_add_tile_hibernate_cb( tile_num, ping_app_hibernate_cb );

    ping_app_main_tile = mainbar_get_tile_obj( tile_num );

    lv_coord_t hor_res = lv_disp_get_hor_res( NULL );

    lv_obj_t *tool_cont = lv_obj_create( ping_app_main_tile, NULL );
    lv_obj_set_size( tool_cont, hor_res, THEME_CONT_HEIGHT );
    lv_obj_add_style( tool_cont, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_obj_align( tool_cont, ping_app_main_tile, LV_ALIGN_IN_TOP_MID, 0, THEME_PADDING );
    ping_tool_list = wf_add_list( tool_cont, "ping\ntraceroute\nport" );
    lv_obj_set_width( ping_tool_list, hor_res - 2 * THEME_PADDING );
    lv_obj_align( ping_tool_list, tool_cont, LV_ALIGN_CENTER, 0, 0 );
    lv_obj_set_event_cb( ping_tool_list, ping_tool_event_cb );

    ping_host_cont = lv_obj_create( ping_app_main_tile, NULL );
    lv_obj_set_size( ping_host_cont, hor_res, THEME_CONT_HEIGHT );
    lv_obj_add_style( ping_host_cont, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_obj_align( ping_host_cont, tool_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_PADDING );
    lv_obj_t *host_label = wf_add_label( ping_host_cont, "Host" );
    lv_obj_align( host_label, ping_host_cont, LV_ALIGN_IN_LEFT_MID, THEME_ICON_PADDING, 0 );
    ping_ip_textfield = lv_textarea_create( ping_host_cont, NULL );
    lv_textarea_set_text( ping_ip_textfield, "" );
    lv_textarea_set_placeholder_text( ping_ip_textfield, "host or ip" );
    lv_textarea_set_accepted_chars( ping_ip_textfield, "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-" );
    lv_textarea_set_max_length( ping_ip_textfield, PING_TARGET_LEN - 1 );
    lv_textarea_set_pwd_mode( ping_ip_textfield, false );
    lv_textarea_set_one_line( ping_ip_textfield, true );
    lv_textarea_set_cursor_hidden( ping_ip_textfield, true );
    lv_obj_set_width( ping_ip_textfield, hor_res / 4 * 3 - THEME_PADDING * 2 );
    lv_obj_align( ping_ip_textfield, ping_host_cont, LV_ALIGN_IN_RIGHT_MID, -THEME_PADDING, 0 );
    lv_obj_set_event_cb( ping_ip_textfield, ping_textarea_event_cb );

    ping_port_cont = lv_obj_create( ping_app_main_tile, NULL );
    lv_obj_set_size( ping_port_cont, hor_res, THEME_CONT_HEIGHT );
    lv_obj_add_style( ping_port_cont, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_obj_align( ping_port_cont, ping_host_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_PADDING );
    lv_obj_t *port_label = wf_add_label( ping_port_cont, "Port" );
    lv_obj_align( port_label, ping_port_cont, LV_ALIGN_IN_LEFT_MID, THEME_ICON_PADDING, 0 );
    ping_port_textfield = lv_textarea_create( ping_port_cont, NULL );
    lv_textarea_set_text( ping_port_textfield, "80" );
    lv_textarea_set_accepted_chars( ping_port_textfield, "0123456789" );
    lv_textarea_set_max_length( ping_port_textfield, 5 );
    lv_textarea_set_pwd_mode( ping_port_textfield, false );
    lv_textarea_set_one_line( ping_port_textfield, true );
    lv_textarea_set_cursor_hidden( ping_port_textfield, true );
    lv_obj_set_width( ping_port_textfield, hor_res / 4 * 3 - THEME_PADDING * 2 );
    lv_obj_align( ping_port_textfield, ping_port_cont, LV_ALIGN_IN_RIGHT_MID, -THEME_PADDING, 0 );
    lv_obj_set_event_cb( ping_port_textfield, ping_port_textarea_event_cb );
    lv_obj_set_hidden( ping_port_cont, true );

    ping_result_page = lv_page_create( ping_app_main_tile, NULL );
    lv_obj_set_width( ping_result_page, hor_res - 2 * THEME_PADDING );
    lv_obj_add_style( ping_result_page, LV_OBJ_PART_MAIN, APP_STYLE );
    lv_page_set_scrlbar_mode( ping_result_page, LV_SCRLBAR_MODE_DRAG );
    ping_result_label = lv_label_create( ping_result_page, NULL );
    lv_label_set_long_mode( ping_result_label, LV_LABEL_LONG_BREAK );
    lv_obj_set_width( ping_result_label, lv_page_get_width_fit( ping_result_page ) );
    lv_label_set_text( ping_result_label, "" );
    ping_app_layout_result();

    lv_obj_t *footer = wf_add_tile_footer_container( ping_app_main_tile, LV_LAYOUT_PRETTY_MID );
    ping_exit_btn = wf_add_exit_button( footer, exit_ping_app_main_event_cb );
    ping_start_btn = wf_add_image_button( footer, ping_app_32px, ping_start_event_cb );
    lv_obj_align( footer, ping_app_main_tile, LV_ALIGN_IN_BOTTOM_MID, 0, -10 );

    lv_tileview_add_element( ping_app_main_tile, tool_cont );
    lv_tileview_add_element( ping_app_main_tile, ping_tool_list );
    lv_tileview_add_element( ping_app_main_tile, ping_host_cont );
    lv_tileview_add_element( ping_app_main_tile, ping_port_cont );
    lv_tileview_add_element( ping_app_main_tile, ping_result_page );

    ping_app_lv_task = lv_task_create( ping_app_task, 500, LV_TASK_PRIO_OFF, NULL );
}

/*
 * the result page fills whatever is left between the input rows and the footer
 */
static void ping_app_layout_result( void ) {
    lv_obj_t *anchor = lv_obj_get_hidden( ping_port_cont ) ? ping_host_cont : ping_port_cont;

    lv_obj_align( ping_result_page, anchor, LV_ALIGN_OUT_BOTTOM_MID, 0, THEME_PADDING );
    lv_coord_t top = lv_obj_get_y( ping_result_page );
    lv_obj_set_height( ping_result_page, lv_disp_get_ver_res( NULL ) - THEME_ICON_SIZE - top );
}

static void ping_app_set_start_btn( bool stop ) {
    if ( stop == ping_start_btn_is_stop )
        return;

    lv_img_set_src( lv_obj_get_child( ping_start_btn, NULL ), stop ? &wf_get_stop_img() : &ping_app_32px );
    ping_start_btn_is_stop = stop;
}

static void ping_app_activate_cb( void ) {
    #ifndef NATIVE_64BIT
        if ( !strlen( lv_textarea_get_text( ping_ip_textfield ) ) && WiFi.isConnected() )
            lv_textarea_set_text( ping_ip_textfield, WiFi.gatewayIP().toString().c_str() );
    #endif

    ping_result_changed = true;
    lv_task_set_prio( ping_app_lv_task, LV_TASK_PRIO_MID );
}

static void ping_app_hibernate_cb( void ) {
    keyboard_hide();
    ping_worker_abort = true;
    lv_task_set_prio( ping_app_lv_task, LV_TASK_PRIO_OFF );
}

static void ping_app_task( lv_task_t * task ) {
    ping_app_set_start_btn( ping_worker_running );

    if ( !ping_result_changed )
        return;

    ping_result_changed = false;

    char text[ PING_RESULT_LEN ];
    PING_LOCK();
    memcpy( text, ping_result_text, sizeof( text ) );
    PING_UNLOCK();

    lv_label_set_text( ping_result_label, text );
}

static void ping_tool_event_cb( lv_obj_t * obj, lv_event_t event ) {
    if( event != LV_EVENT_VALUE_CHANGED )
        return;

    ping_tool = ( ping_tool_t )lv_dropdown_get_selected( obj );
    lv_obj_set_hidden( ping_port_cont, ping_tool != PING_TOOL_PORT );
    ping_app_layout_result();
}

static void ping_textarea_event_cb( lv_obj_t * obj, lv_event_t event ) {
    if( event == LV_EVENT_CLICKED ) {
        keyboard_set_textarea( obj );
    }
}

static void ping_port_textarea_event_cb( lv_obj_t * obj, lv_event_t event ) {
    if( event == LV_EVENT_CLICKED ) {
        num_keyboard_set_textarea( obj );
    }
}

static void ping_start_event_cb( lv_obj_t * obj, lv_event_t event ) {
    if( event != LV_EVENT_CLICKED )
        return;

    if ( ping_worker_running ) {
        ping_worker_abort = true;
        return;
    }

    keyboard_hide();

    const char *target = lv_textarea_get_text( ping_ip_textfield );
    if ( !strlen( target ) ) {
        ping_result_clear();
        ping_result_append( "no target" );
        return;
    }

    PING_LOCK();
    strncpy( ping_target, target, sizeof( ping_target ) - 1 );
    ping_target[ sizeof( ping_target ) - 1 ] = '\0';
    PING_UNLOCK();

    ping_target_port = atoi( lv_textarea_get_text( ping_port_textfield ) );

    ping_result_clear();
    ping_worker_start();
}

static void exit_ping_app_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_jump_back();
                                        break;
    }
}

static void ping_result_clear( void ) {
    PING_LOCK();
    ping_result_text[ 0 ] = '\0';
    PING_UNLOCK();
    ping_result_changed = true;
}

static void ping_result_append( const char *format, ... ) {
    char line[ PING_LINE_LEN ];
    va_list args;

    va_start( args, format );
    vsnprintf( line, sizeof( line ), format, args );
    va_end( args );

    PING_LOCK();
    size_t used = strlen( ping_result_text );
    if ( used && used + 1 < sizeof( ping_result_text ) ) {
        ping_result_text[ used++ ] = '\n';
        ping_result_text[ used ] = '\0';
    }
    strncat( ping_result_text, line, sizeof( ping_result_text ) - used - 1 );
    PING_UNLOCK();

    ping_result_changed = true;
}

#ifndef NATIVE_64BIT

static ping_resp ping_resp_data;
static volatile bool ping_resp_valid = false;

static void ping_recv_cb( void *opt, void *pdata ) {
    memcpy( &ping_resp_data, pdata, sizeof( ping_resp_data ) );
    ping_resp_valid = true;
}

static void ping_run_ping( IPAddress ip ) {
    ping_option opt;

    memset( &opt, 0, sizeof( opt ) );
    opt.count = PING_COUNT;
    opt.ip = ( uint32_t )ip;
    opt.recv_function = ping_recv_cb;

    ping_resp_valid = false;
    ping_start( ip, PING_COUNT, PING_INTERVAL, PING_SIZE, PING_TIMEOUT, &opt );

    if ( !ping_resp_valid ) {
        ping_result_append( "no result" );
        return;
    }

    uint32_t sent = ping_resp_data.total_count;
    uint32_t recv = sent > ping_resp_data.timeout_count ? sent - ping_resp_data.timeout_count : 0;
    ping_result_append( "%d sent, %d recv, %d%% loss", ( int )sent, ( int )recv, ( int )( sent ? ( sent - recv ) * 100 / sent : 100 ) );
    if ( recv )
        ping_result_append( "avg %.1f ms", ping_resp_data.resp_time );
}

static void ping_trace_prepare_echo( struct icmp_echo_hdr *iecho, uint16_t len, uint16_t seqno ) {
    size_t data_len = len - sizeof( struct icmp_echo_hdr );

    ICMPH_TYPE_SET( iecho, ICMP_ECHO );
    ICMPH_CODE_SET( iecho, 0 );
    iecho->chksum = 0;
    iecho->id = PING_TRACE_ID;
    iecho->seqno = htons( seqno );
    for ( size_t i = 0 ; i < data_len ; i++ )
        ( ( char * )iecho )[ sizeof( struct icmp_echo_hdr ) + i ] = ( char )i;
    iecho->chksum = inet_chksum( iecho, len );
}

/**
 * @brief   wait for one icmp packet that belongs to us
 * @return  the icmp type, or -1 on timeout
 */
static int ping_trace_recv( int s, uint32_t *from_ip ) {
    uint8_t buf[ 128 ];

    // raw icmp sockets see every icmp packet, drop what is not ours
    for ( int retry = 0 ; retry < 4 ; retry++ ) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof( from );

        int len = recvfrom( s, buf, sizeof( buf ), 0, ( struct sockaddr * )&from, &fromlen );
        if ( len <= 0 )
            return( -1 );

        struct ip_hdr *iphdr = ( struct ip_hdr * )buf;
        int hlen = IPH_HL( iphdr ) * 4;
        if ( len < hlen + ( int )sizeof( struct icmp_echo_hdr ) )
            continue;

        struct icmp_echo_hdr *iecho = ( struct icmp_echo_hdr * )( buf + hlen );
        uint8_t type = ICMPH_TYPE( iecho );

        if ( type == ICMP_ER && iecho->id != PING_TRACE_ID )
            continue;
        if ( type != ICMP_ER && type != ICMP_TE )
            continue;

        *from_ip = from.sin_addr.s_addr;
        return( type );
    }

    return( -1 );
}

static void ping_run_trace( IPAddress ip ) {
    uint8_t sendbuf[ sizeof( struct icmp_echo_hdr ) + PING_SIZE ] __attribute__ (( aligned( 4 ) ));

    int s = socket( AF_INET, SOCK_RAW, IP_PROTO_ICMP );
    if ( s < 0 ) {
        ping_result_append( "no raw socket" );
        return;
    }

    struct timeval tout;
    tout.tv_sec = PING_TRACE_TIMEOUT;
    tout.tv_usec = 0;
    setsockopt( s, SOL_SOCKET, SO_RCVTIMEO, &tout, sizeof( tout ) );

    struct sockaddr_in target;
    memset( &target, 0, sizeof( target ) );
    target.sin_family = AF_INET;
    target.sin_addr.s_addr = ( uint32_t )ip;

    for ( int ttl = 1 ; ttl <= PING_TRACE_MAX_HOP && !ping_worker_abort ; ttl++ ) {
        setsockopt( s, IPPROTO_IP, IP_TTL, &ttl, sizeof( ttl ) );

        ping_trace_prepare_echo( ( struct icmp_echo_hdr * )sendbuf, sizeof( sendbuf ), ttl );
        if ( sendto( s, sendbuf, sizeof( sendbuf ), 0, ( struct sockaddr * )&target, sizeof( target ) ) < 0 ) {
            ping_result_append( "%2d  send failed", ttl );
            break;
        }

        uint32_t start = millis();
        uint32_t from_ip = 0;
        int type = ping_trace_recv( s, &from_ip );

        if ( type < 0 ) {
            ping_result_append( "%2d  *", ttl );
            continue;
        }

        ping_result_append( "%2d  %s  %d ms", ttl, IPAddress( from_ip ).toString().c_str(), ( int )( millis() - start ) );

        if ( type == ICMP_ER || from_ip == ( uint32_t )ip )
            break;
    }

    close( s );
}

static void ping_run_port( IPAddress ip ) {
    WiFiClient client;

    uint32_t start = millis();
    bool open = client.connect( ip, ping_target_port, PING_PORT_TIMEOUT );
    uint32_t took = millis() - start;
    client.stop();

    ping_result_append( "port %d %s (%d ms)", ( int )ping_target_port, open ? "open" : "closed or filtered", ( int )took );
}

static void ping_worker_run( void ) {
    char target[ PING_TARGET_LEN ];
    IPAddress ip;

    PING_LOCK();
    strncpy( target, ping_target, sizeof( target ) - 1 );
    target[ sizeof( target ) - 1 ] = '\0';
    PING_UNLOCK();

    if ( !WiFi.isConnected() ) {
        ping_result_append( "no wifi" );
        return;
    }

    if ( !ip.fromString( target ) ) {
        // hostByName blocks until the lwip dns timeout
        if ( WiFi.hostByName( target, ip ) != 1 ) {
            ping_result_append( "cannot resolve" );
            return;
        }
        ping_result_append( "%s -> %s", target, ip.toString().c_str() );
    }

    if ( ping_worker_abort )
        return;

    switch( ping_tool ) {
        case PING_TOOL_PING:    ping_run_ping( ip );
                                break;
        case PING_TOOL_TRACE:   ping_run_trace( ip );
                                break;
        case PING_TOOL_PORT:    ping_run_port( ip );
                                break;
    }

    if ( ping_worker_abort )
        ping_result_append( "aborted" );
}

static void ping_worker( void *pvParameters ) {
    ping_worker_run();

    log_i("ping worker done");
    ping_worker_running = false;
    ping_result_changed = true;
    ping_worker_handle = NULL;
    vTaskDelete( NULL );
}

#endif // NATIVE_64BIT

static void ping_worker_start( void ) {
    #ifdef NATIVE_64BIT
        ping_result_append( "no wifi" );
    #else
        if ( ping_worker_handle )
            return;
        ping_worker_abort = false;
        ping_worker_running = true;
        xTaskCreatePinnedToCore( ping_worker, "ping worker", 4096, NULL, 1, &ping_worker_handle, 0 );
    #endif
}
