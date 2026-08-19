/****************************************************************************
 *  NetTools_sniff.cpp
 *  Copyright  2026  Dirk Sarodnick
 *
 *  Passive listener for the broadcasts every home network emits anyway:
 *  wake on lan magic packets, netbios name service and dhcp. On top of that
 *  an active arp sweep on demand, so a quiet network can be enumerated too.
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
    #include <WiFi.h>
    #include <lwip/sockets.h>
    #include <lwip/etharp.h>
    #include <lwip/tcpip.h>
#endif

#include "NetTools.h"
#include "NetTools_setup.h"
#include "NetTools_sniff.h"

#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"
#include "hardware/motor.h"
#include "hardware/powermgm.h"

#define NETTOOLS_SNIFF_BUFFER_SIZE  600
#define NETTOOLS_SNIFF_LINE_SIZE    48
#define NETTOOLS_SNIFF_STATUS       22
#define NETTOOLS_SNIFF_HOLD         3000
#define NETTOOLS_SNIFF_IDLE_ROUNDS  3

#define NETTOOLS_SWEEP_BATCH        6
#define NETTOOLS_SWEEP_WAIT         200
#define NETTOOLS_SWEEP_MAX          254
#define NETTOOLS_SWEEP_NAMES_BATCH  3

#ifdef NATIVE_64BIT
    #define NETTOOLS_SNIFF_LOCK()
    #define NETTOOLS_SNIFF_UNLOCK()
#else
    static portMUX_TYPE nettools_sniff_mux = portMUX_INITIALIZER_UNLOCKED;
    #define NETTOOLS_SNIFF_LOCK()       portENTER_CRITICAL( &nettools_sniff_mux )
    #define NETTOOLS_SNIFF_UNLOCK()     portEXIT_CRITICAL( &nettools_sniff_mux )

    static const uint16_t nettools_sniff_port[] = { 7, 9, 67, 68, 137, 138 };
    #define NETTOOLS_SNIFF_SOCKETS      ( sizeof( nettools_sniff_port ) / sizeof( nettools_sniff_port[ 0 ] ) )

    static uint8_t nettools_sniff_buffer[ NETTOOLS_SNIFF_BUFFER_SIZE ];
    static TaskHandle_t nettools_sniff_handle = NULL;
    static volatile bool nettools_sniff_stop = true;
    static uint16_t nettools_sniff_pkt[ NETTOOLS_SNIFF_SOCKETS ] = { 0 };
    static uint8_t nettools_sniff_drop_log[ NETTOOLS_SNIFF_SOCKETS ] = { 0 };
#endif

static nettools_sniff_entry_t nettools_sniff_table[ NETTOOLS_SNIFF_ENTRIES ];
static volatile int nettools_sniff_entries = 0;
static volatile bool nettools_sniff_changed = false;
static volatile int nettools_sniff_bound = 0;
static volatile uint32_t nettools_sniff_packets = 0;

static volatile bool nettools_sweep_request = false;
static volatile bool nettools_sweep_abort = false;
static volatile bool nettools_sweep_running = false;
static volatile int nettools_sweep_done = 0;
static volatile int nettools_sweep_total = 0;

lv_obj_t *NetTools_sniff_tile = NULL;
lv_obj_t *NetTools_sniff_list = NULL;
lv_obj_t *NetTools_sniff_status_label = NULL;
lv_obj_t *NetTools_sniff_scan_btn = NULL;
lv_obj_t *NetTools_sniff_row[ NETTOOLS_SNIFF_ENTRIES ] = { NULL };

static lv_task_t *NetTools_sniff_lv_task = NULL;
static bool nettools_sniff_scan_btn_is_stop = false;
static uint32_t nettools_sniff_status_hold = 0;
static int nettools_sniff_idle_rounds = 0;

static void back_NetTools_sniff_event_cb( lv_obj_t * obj, lv_event_t event );
static void scan_NetTools_sniff_event_cb( lv_obj_t * obj, lv_event_t event );
static void clear_NetTools_sniff_event_cb( lv_obj_t * obj, lv_event_t event );
static void row_NetTools_sniff_event_cb( lv_obj_t * obj, lv_event_t event );
static bool NetTools_sniff_powermgm_event_cb( EventBits_t event, void *arg );
static void NetTools_sniff_activate_cb( void );
static void NetTools_sniff_hibernate_cb( void );
static void NetTools_sniff_update_task( lv_task_t *task );
static void NetTools_sniff_refresh( void );
static void nettools_sniff_start( void );
static void nettools_sniff_stop_worker( void );
static void nettools_sniff_set_status( const char *text );

void NetTools_sniff_setup( uint32_t tile_num ) {

    mainbar_add_tile_activate_cb( tile_num, NetTools_sniff_activate_cb );
    mainbar_add_tile_hibernate_cb( tile_num, NetTools_sniff_hibernate_cb );
    powermgm_register_cb( POWERMGM_STANDBY | POWERMGM_WAKEUP, NetTools_sniff_powermgm_event_cb, "NetTools sniff powermgm" );

    NetTools_sniff_tile = mainbar_get_tile_obj( tile_num );

    lv_coord_t hor_res = lv_disp_get_hor_res( NULL );

    lv_obj_t *footer = wf_add_tile_footer_container( NetTools_sniff_tile, LV_LAYOUT_PRETTY_MID );
    lv_obj_t *back_btn = wf_add_left_button( footer, back_NetTools_sniff_event_cb );
    NetTools_sniff_scan_btn = wf_add_refresh_button( footer, scan_NetTools_sniff_event_cb );
    lv_obj_t *trash_btn = wf_add_trash_button( footer, clear_NetTools_sniff_event_cb );
    lv_obj_align( footer, NetTools_sniff_tile, LV_ALIGN_IN_BOTTOM_MID, 0, -10 );

    NetTools_sniff_list = lv_list_create( NetTools_sniff_tile, NULL );
    lv_obj_set_size( NetTools_sniff_list, hor_res, lv_obj_get_y( footer ) - NETTOOLS_SNIFF_STATUS );
    lv_obj_align( NetTools_sniff_list, NetTools_sniff_tile, LV_ALIGN_IN_TOP_MID, 0, 0 );

    for ( int i = 0 ; i < NETTOOLS_SNIFF_ENTRIES ; i++ ) {
        NetTools_sniff_row[ i ] = lv_list_add_btn( NetTools_sniff_list, NULL, "" );
        lv_obj_set_user_data( NetTools_sniff_row[ i ], ( lv_obj_user_data_t )( intptr_t )i );
        lv_obj_set_event_cb( NetTools_sniff_row[ i ], row_NetTools_sniff_event_cb );
        lv_obj_set_hidden( NetTools_sniff_row[ i ], true );
    }

    NetTools_sniff_status_label = wf_add_label( NetTools_sniff_tile, "" );
    lv_label_set_long_mode( NetTools_sniff_status_label, LV_LABEL_LONG_CROP );
    lv_label_set_align( NetTools_sniff_status_label, LV_LABEL_ALIGN_CENTER );
    lv_obj_set_size( NetTools_sniff_status_label, hor_res, NETTOOLS_SNIFF_STATUS );
    lv_obj_align( NetTools_sniff_status_label, NetTools_sniff_list, LV_ALIGN_OUT_BOTTOM_MID, 0, 0 );

    mainbar_add_slide_element( footer );
    mainbar_add_slide_element( back_btn );
    mainbar_add_slide_element( NetTools_sniff_scan_btn );
    mainbar_add_slide_element( trash_btn );

    NetTools_sniff_lv_task = lv_task_create( NetTools_sniff_update_task, 1000, LV_TASK_PRIO_OFF, NULL );
}

/*
 * result table, written by the worker, read by the gui
 */
static void nettools_sniff_add( sniff_kind_t kind, const char *mac, const char *host, uint32_t ip ) {
    int slot = -1;

    NETTOOLS_SNIFF_LOCK();

    for ( int i = 0 ; i < nettools_sniff_entries ; i++ ) {
        if ( mac && *mac && !strcmp( nettools_sniff_table[ i ].mac, mac ) ) {
            slot = i;
            break;
        }
        if ( ( !mac || !*mac ) && host && *host && !strcmp( nettools_sniff_table[ i ].host, host ) ) {
            slot = i;
            break;
        }
    }

    if ( slot < 0 ) {
        if ( nettools_sniff_entries < NETTOOLS_SNIFF_ENTRIES ) {
            slot = nettools_sniff_entries++;
        }
        else {
            slot = 0;
            for ( int i = 1 ; i < NETTOOLS_SNIFF_ENTRIES ; i++ ) {
                if ( nettools_sniff_table[ i ].last_seen < nettools_sniff_table[ slot ].last_seen )
                    slot = i;
            }
        }
        memset( &nettools_sniff_table[ slot ], 0, sizeof( nettools_sniff_entry_t ) );
        nettools_sniff_table[ slot ].kind = kind;
    }

    if ( mac && *mac )
        strncpy( nettools_sniff_table[ slot ].mac, mac, sizeof( nettools_sniff_table[ slot ].mac ) - 1 );
    if ( host && *host )
        strncpy( nettools_sniff_table[ slot ].host, host, sizeof( nettools_sniff_table[ slot ].host ) - 1 );
    if ( ip )
        nettools_sniff_table[ slot ].ip = ip;

    nettools_sniff_table[ slot ].last_seen = millis();
    if ( nettools_sniff_table[ slot ].count < 0xffff )
        nettools_sniff_table[ slot ].count++;

    NETTOOLS_SNIFF_UNLOCK();

    nettools_sniff_changed = true;
}

#ifndef NATIVE_64BIT

/*
 * a node status reply names a host we already know by ip, merge instead of add
 */
static bool nettools_sniff_name_by_ip( uint32_t ip, const char *host ) {
    bool found = false;

    NETTOOLS_SNIFF_LOCK();
    for ( int i = 0 ; i < nettools_sniff_entries ; i++ ) {
        if ( nettools_sniff_table[ i ].ip != ip )
            continue;
        strncpy( nettools_sniff_table[ i ].host, host, sizeof( nettools_sniff_table[ i ].host ) - 1 );
        nettools_sniff_table[ i ].host[ sizeof( nettools_sniff_table[ i ].host ) - 1 ] = '\0';
        found = true;
        break;
    }
    NETTOOLS_SNIFF_UNLOCK();

    if ( found )
        nettools_sniff_changed = true;

    return( found );
}

static void nettools_sniff_format_mac( char *dest, size_t size, const uint8_t *mac ) {
    snprintf( dest, size, "%02X:%02X:%02X:%02X:%02X:%02X", mac[ 0 ], mac[ 1 ], mac[ 2 ], mac[ 3 ], mac[ 4 ], mac[ 5 ] );
}

/*
 * a magic packet is 6 x 0xff followed by the target mac repeated 16 times,
 * only the first 102 byte matter, a SecureOn password may follow
 */
static bool nettools_sniff_parse_wol( const uint8_t *buf, int len, uint32_t ip ) {
    if ( len < 102 )
        return( false );

    for ( int i = 0 ; i < 6 ; i++ ) {
        if ( buf[ i ] != 0xff )
            return( false );
    }

    for ( int rep = 2 ; rep <= 16 ; rep++ ) {
        if ( memcmp( buf + 6, buf + rep * 6, 6 ) )
            return( false );
    }

    char mac[ NETTOOLS_MAC_LEN ];
    nettools_sniff_format_mac( mac, sizeof( mac ), buf + 6 );
    nettools_sniff_add( SNIFF_WOL, mac, NULL, ip );
    return( true );
}

/*
 * netbios first level encoding, 32 chars carrying one nibble each as 'A' + n
 */
static bool nettools_sniff_decode_netbios_name( const uint8_t *buf, int len, int offset, char *dest, size_t size ) {
    if ( offset + 33 > len )
        return( false );
    if ( buf[ offset ] != 0x20 )
        return( false );

    const uint8_t *enc = buf + offset + 1;
    char name[ 16 ] = "";

    for ( int i = 0 ; i < 15 ; i++ ) {
        uint8_t hi = enc[ i * 2 ];
        uint8_t lo = enc[ i * 2 + 1 ];
        if ( hi < 'A' || hi > 'P' || lo < 'A' || lo > 'P' )
            return( false );
        name[ i ] = ( char )( ( ( hi - 'A' ) << 4 ) | ( lo - 'A' ) );
    }

    int end = 15;
    while ( end > 0 && ( name[ end - 1 ] == ' ' || name[ end - 1 ] == '\0' ) )
        end--;
    if ( end <= 0 )
        return( false );
    name[ end ] = '\0';

    strncpy( dest, name, size - 1 );
    dest[ size - 1 ] = '\0';
    return( true );
}

/*
 * parse the sniffed packet
 * registration 5, release 6, refresh 8 and 9
 */
static bool nettools_sniff_parse_nbns( const uint8_t *buf, int len, uint32_t ip ) {
    if ( len < 12 )
        return( false );

    uint16_t flags = ( buf[ 2 ] << 8 ) | buf[ 3 ];
    uint8_t opcode = ( flags >> 11 ) & 0x0f;
    if ( opcode != 5 && opcode != 6 && opcode != 8 && opcode != 9 )
        return( false );

    char host[ NETTOOLS_NAME_LEN ];
    if ( !nettools_sniff_decode_netbios_name( buf, len, 12, host, sizeof( host ) ) )
        return( false );

    nettools_sniff_add( SNIFF_NETBIOS, NULL, host, ip );
    return( true );
}

/*
 * node status reply on our own query, the answer carries the name table
 */
static bool nettools_sniff_parse_nbstat( const uint8_t *buf, int len, uint32_t ip ) {
    if ( len < 57 )
        return( false );

    uint16_t flags = ( buf[ 2 ] << 8 ) | buf[ 3 ];
    if ( !( flags & 0x8000 ) || ( ( flags >> 11 ) & 0x0f ) != 0 )
        return( false );
    if ( !( ( buf[ 6 ] << 8 ) | buf[ 7 ] ) )
        return( false );
    if ( buf[ 12 ] != 0x20 )
        return( false );

    int pos = 12 + 34;
    if ( ( ( buf[ pos ] << 8 ) | buf[ pos + 1 ] ) != 0x0021 )
        return( false );

    pos += 2 + 2 + 4 + 2;
    int names = buf[ pos++ ];

    for ( int i = 0 ; i < names ; i++ ) {
        if ( pos + 18 > len )
            break;

        uint16_t nflags = ( buf[ pos + 16 ] << 8 ) | buf[ pos + 17 ];
        int end = 15;
        while ( end > 0 && ( buf[ pos + end - 1 ] == ' ' || !buf[ pos + end - 1 ] ) )
            end--;

        if ( !( nflags & 0x8000 ) && end > 0 ) {
            char host[ NETTOOLS_NAME_LEN ] = "";
            size_t copy = end < ( int )sizeof( host ) - 1 ? end : sizeof( host ) - 1;
            memcpy( host, buf + pos, copy );
            host[ copy ] = '\0';
            if ( !nettools_sniff_name_by_ip( ip, host ) )
                nettools_sniff_add( SNIFF_NETBIOS, NULL, host, ip );
            return( true );
        }

        pos += 18;
    }

    return( false );
}

/*
 * datagram service, the sender name sits behind the 14 byte header
 */
static bool nettools_sniff_parse_nbdgm( const uint8_t *buf, int len, uint32_t ip ) {
    if ( len < 14 )
        return( false );

    char host[ NETTOOLS_NAME_LEN ];
    if ( !nettools_sniff_decode_netbios_name( buf, len, 14, host, sizeof( host ) ) )
        return( false );

    nettools_sniff_add( SNIFF_NETBIOS, NULL, host, ip );
    return( true );
}

/*
 * bootp/dhcp, chaddr at 28, magic cookie at 236, then the options
 */
static bool nettools_sniff_parse_dhcp( const uint8_t *buf, int len, uint32_t ip ) {
    if ( len < 240 )
        return( false );
    if ( buf[ 236 ] != 0x63 || buf[ 237 ] != 0x82 || buf[ 238 ] != 0x53 || buf[ 239 ] != 0x63 )
        return( false );
    if ( buf[ 1 ] != 1 || buf[ 2 ] != 6 )
        return( false );

    char mac[ NETTOOLS_MAC_LEN ];
    nettools_sniff_format_mac( mac, sizeof( mac ), buf + 28 );

    char host[ NETTOOLS_NAME_LEN ] = "";
    int pos = 240;
    while ( pos < len ) {
        uint8_t code = buf[ pos ];
        if ( code == 255 )
            break;
        if ( code == 0 ) {
            pos++;
            continue;
        }
        if ( pos + 2 > len )
            break;
        uint8_t optlen = buf[ pos + 1 ];
        if ( pos + 2 + optlen > len )
            break;
        if ( code == 12 && optlen > 0 ) {
            size_t copy = optlen < sizeof( host ) - 1 ? optlen : sizeof( host ) - 1;
            memcpy( host, buf + pos + 2, copy );
            host[ copy ] = '\0';
        }
        pos += 2 + optlen;
    }

    // yiaddr carries the lease in a server reply, the request itself has ciaddr
    uint32_t addr = *( ( const uint32_t * )( buf + 16 ) );
    if ( !addr )
        addr = *( ( const uint32_t * )( buf + 12 ) );
    if ( !addr )
        addr = ip;

    nettools_sniff_add( SNIFF_DHCP, mac, host, addr );
    return( true );
}

static bool nettools_sniff_dispatch( uint16_t port, const uint8_t *buf, int len, uint32_t ip ) {
    switch( port ) {
        case 7:
        case 9:     return( nettools_sniff_parse_wol( buf, len, ip ) );
        case 67:
        case 68:    return( nettools_sniff_parse_dhcp( buf, len, ip ) );
        case 137:   return( nettools_sniff_parse_nbstat( buf, len, ip ) || nettools_sniff_parse_nbns( buf, len, ip ) );
        case 138:   return( nettools_sniff_parse_nbdgm( buf, len, ip ) );
    }
    return( false );
}

typedef struct {
    volatile bool   busy;
    int             num;
    ip4_addr_t      addr[ NETTOOLS_SWEEP_BATCH ];
} nettools_arp_req_t;

typedef struct {
    volatile bool   busy;
    int             num;
    uint32_t        ip[ ARP_TABLE_SIZE ];
    uint8_t         mac[ ARP_TABLE_SIZE ][ 6 ];
} nettools_arp_result_t;

typedef enum {
    SWEEP_IDLE = 0,
    SWEEP_REQUEST,
    SWEEP_HARVEST,
    SWEEP_NAMES
} nettools_sweep_state_t;

static nettools_arp_req_t nettools_arp_req;
static nettools_arp_result_t nettools_arp_result;
static nettools_sweep_state_t nettools_sweep_state = SWEEP_IDLE;
static uint32_t nettools_sweep_base = 0;
static uint32_t nettools_sweep_own = 0;
static uint32_t nettools_sweep_wait = 0;
static int nettools_sweep_index = 0;
static int nettools_sweep_count = 0;
static int nettools_sweep_name_index = 0;

static void nettools_arp_request_cb( void *ctx ) {
    nettools_arp_req_t *req = ( nettools_arp_req_t * )ctx;

    if ( netif_default ) {
        for ( int i = 0 ; i < req->num ; i++ )
            etharp_request( netif_default, &req->addr[ i ] );
    }

    req->busy = false;
}

static void nettools_arp_harvest_cb( void *ctx ) {
    nettools_arp_result_t *res = ( nettools_arp_result_t * )ctx;

    res->num = 0;
    for ( size_t i = 0 ; i < ARP_TABLE_SIZE ; i++ ) {
        ip4_addr_t *ip = NULL;
        struct netif *netif = NULL;
        struct eth_addr *eth = NULL;

        if ( !etharp_get_entry( i, &ip, &netif, &eth ) )
            continue;
        if ( !ip || !eth )
            continue;

        res->ip[ res->num ] = ip->addr;
        memcpy( res->mac[ res->num ], eth->addr, 6 );
        res->num++;
    }

    res->busy = false;
}

static bool nettools_arp_run( tcpip_callback_fn fn, void *ctx, volatile bool *busy ) {
    *busy = true;

    if ( tcpip_callback( fn, ctx ) != ERR_OK ) {
        *busy = false;
        return( false );
    }

    for ( int i = 0 ; i < 100 && *busy ; i++ )
        vTaskDelay( 5 / portTICK_PERIOD_MS );

    return( !*busy );
}

static void nettools_sweep_finish( void ) {
    nettools_sweep_state = SWEEP_IDLE;
    nettools_sweep_running = false;
    nettools_sweep_request = false;
    nettools_sweep_abort = false;
    nettools_sniff_changed = true;
}

static bool nettools_sweep_begin( void ) {
    uint32_t ip = ( uint32_t )WiFi.localIP();
    uint32_t mask = ( uint32_t )WiFi.subnetMask();

    if ( !ip || !mask )
        return( false );

    uint32_t host_ip = ntohl( ip );
    uint32_t host_mask = ntohl( mask );
    uint32_t hosts = ~host_mask;

    if ( hosts < 2 )
        return( false );

    hosts -= 1;
    if ( hosts > NETTOOLS_SWEEP_MAX )
        hosts = NETTOOLS_SWEEP_MAX;

    nettools_sweep_base = host_ip & host_mask;
    nettools_sweep_own = host_ip;
    nettools_sweep_count = hosts;
    nettools_sweep_index = 0;
    nettools_sweep_name_index = 0;
    nettools_sweep_done = 0;
    nettools_sweep_total = hosts;
    nettools_sweep_state = SWEEP_REQUEST;
    nettools_sweep_running = true;

    log_i("NetTools: arp sweep over %d addresses", hosts );
    return( true );
}

static void nettools_sweep_send_nbstat( int fd, uint32_t ip ) {
    static const uint8_t query[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x20,
        'C', 'K', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A',
        'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A',
        0x00,
        0x00, 0x21, 0x00, 0x01
    };

    struct sockaddr_in to;
    memset( &to, 0, sizeof( to ) );
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = ip;
    to.sin_port = htons( 137 );

    sendto( fd, query, sizeof( query ), 0, ( struct sockaddr * )&to, sizeof( to ) );
}

static void nettools_sweep_step( int nbns_fd ) {
    if ( nettools_sweep_abort ) {
        log_i("NetTools: arp sweep aborted at %d", nettools_sweep_index );
        nettools_sweep_finish();
        return;
    }

    switch( nettools_sweep_state ) {
        case SWEEP_REQUEST: {
            nettools_arp_req.num = 0;
            while ( nettools_arp_req.num < NETTOOLS_SWEEP_BATCH && nettools_sweep_index < nettools_sweep_count ) {
                uint32_t target = nettools_sweep_base + 1 + nettools_sweep_index;
                nettools_sweep_index++;
                if ( target == nettools_sweep_own )
                    continue;
                nettools_arp_req.addr[ nettools_arp_req.num++ ].addr = htonl( target );
            }

            nettools_sweep_done = nettools_sweep_index;

            if ( !nettools_arp_req.num ) {
                nettools_sweep_state = SWEEP_NAMES;
                break;
            }

            nettools_arp_run( nettools_arp_request_cb, &nettools_arp_req, &nettools_arp_req.busy );
            nettools_sweep_wait = millis() + NETTOOLS_SWEEP_WAIT;
            nettools_sweep_state = SWEEP_HARVEST;
            break;
        }

        case SWEEP_HARVEST: {
            if ( millis() < nettools_sweep_wait )
                break;

            if ( nettools_arp_run( nettools_arp_harvest_cb, &nettools_arp_result, &nettools_arp_result.busy ) ) {
                for ( int i = 0 ; i < nettools_arp_result.num ; i++ ) {
                    char mac[ NETTOOLS_MAC_LEN ];
                    nettools_sniff_format_mac( mac, sizeof( mac ), nettools_arp_result.mac[ i ] );
                    nettools_sniff_add( SNIFF_ARP, mac, NULL, nettools_arp_result.ip[ i ] );
                }
            }

            nettools_sweep_state = SWEEP_REQUEST;
            break;
        }

        case SWEEP_NAMES: {
            if ( nbns_fd < 0 ) {
                nettools_sweep_finish();
                break;
            }

            int sent = 0;
            while ( sent < NETTOOLS_SWEEP_NAMES_BATCH && nettools_sweep_name_index < NETTOOLS_SNIFF_ENTRIES ) {
                int slot = nettools_sweep_name_index++;
                uint32_t ip = 0;

                NETTOOLS_SNIFF_LOCK();
                if ( slot < nettools_sniff_entries && !nettools_sniff_table[ slot ].host[ 0 ] )
                    ip = nettools_sniff_table[ slot ].ip;
                NETTOOLS_SNIFF_UNLOCK();

                if ( !ip )
                    continue;

                nettools_sweep_send_nbstat( nbns_fd, ip );
                sent++;
            }

            if ( nettools_sweep_name_index >= NETTOOLS_SNIFF_ENTRIES ) {
                log_i("NetTools: arp sweep done, %d devices", nettools_sniff_entries );
                nettools_sweep_finish();
            }
            break;
        }

        default:    break;
    }
}

static void NetTools_sniff_worker( void *pvParameters ) {
    int fd[ NETTOOLS_SNIFF_SOCKETS ];
    int nbns_fd = -1;
    int bound = 0;

    memset( nettools_sniff_pkt, 0, sizeof( nettools_sniff_pkt ) );
    memset( nettools_sniff_drop_log, 0, sizeof( nettools_sniff_drop_log ) );
    nettools_sniff_packets = 0;

    for ( int i = 0 ; i < ( int )NETTOOLS_SNIFF_SOCKETS ; i++ ) {
        fd[ i ] = -1;

        int s = socket( AF_INET, SOCK_DGRAM, 0 );
        if ( s < 0 ) {
            log_w("NetTools: socket for port %d failed", nettools_sniff_port[ i ] );
            continue;
        }

        int on = 1;
        setsockopt( s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof( on ) );
        setsockopt( s, SOL_SOCKET, SO_BROADCAST, &on, sizeof( on ) );

        struct sockaddr_in addr;
        memset( &addr, 0, sizeof( addr ) );
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl( INADDR_ANY );
        addr.sin_port = htons( nettools_sniff_port[ i ] );

        if ( bind( s, ( struct sockaddr * )&addr, sizeof( addr ) ) < 0 ) {
            // lwip holds port 68 for its own dhcp client, that one is expected to fail
            log_w("NetTools: bind on port %d failed (errno %d)", nettools_sniff_port[ i ], errno );
            close( s );
            continue;
        }

        log_i("NetTools: listening on udp port %d", nettools_sniff_port[ i ] );
        fd[ i ] = s;
        if ( nettools_sniff_port[ i ] == 137 )
            nbns_fd = s;
        bound++;
    }

    nettools_sniff_bound = bound;
    nettools_sniff_changed = true;

    while ( bound > 0 && !nettools_sniff_stop ) {
        fd_set rfds;
        int maxfd = -1;

        FD_ZERO( &rfds );
        for ( int i = 0 ; i < ( int )NETTOOLS_SNIFF_SOCKETS ; i++ ) {
            if ( fd[ i ] < 0 )
                continue;
            FD_SET( fd[ i ], &rfds );
            if ( fd[ i ] > maxfd )
                maxfd = fd[ i ];
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = nettools_sweep_running ? 20000 : 500000;

        if ( select( maxfd + 1, &rfds, NULL, NULL, &tv ) > 0 ) {
            for ( int i = 0 ; i < ( int )NETTOOLS_SNIFF_SOCKETS ; i++ ) {
                if ( fd[ i ] < 0 || !FD_ISSET( fd[ i ], &rfds ) )
                    continue;

                struct sockaddr_in from;
                socklen_t fromlen = sizeof( from );
                int len = recvfrom( fd[ i ], nettools_sniff_buffer, sizeof( nettools_sniff_buffer ), 0, ( struct sockaddr * )&from, &fromlen );
                if ( len <= 0 )
                    continue;

                uint32_t src = from.sin_addr.s_addr;
                if ( !nettools_sniff_pkt[ i ] )
                    log_d("NetTools: first packet on port %d, %d byte from %d.%d.%d.%d", nettools_sniff_port[ i ], len,
                          ( int )( src & 0xff ), ( int )( ( src >> 8 ) & 0xff ), ( int )( ( src >> 16 ) & 0xff ), ( int )( ( src >> 24 ) & 0xff ) );
                if ( nettools_sniff_pkt[ i ] < 0xffff )
                    nettools_sniff_pkt[ i ]++;
                nettools_sniff_packets++;

                if ( !nettools_sniff_dispatch( nettools_sniff_port[ i ], nettools_sniff_buffer, len, src ) && nettools_sniff_drop_log[ i ] < 3 ) {
                    nettools_sniff_drop_log[ i ]++;
                    log_d("NetTools: unparsed packet on port %d, %d byte", nettools_sniff_port[ i ], len );
                }
            }
        }

        if ( nettools_sweep_running )
            nettools_sweep_step( nbns_fd );
        else if ( nettools_sweep_request )
            nettools_sweep_request = nettools_sweep_begin();
    }

    if ( nettools_sweep_running )
        nettools_sweep_finish();

    for ( int i = 0 ; i < ( int )NETTOOLS_SNIFF_SOCKETS ; i++ ) {
        if ( fd[ i ] >= 0 )
            close( fd[ i ] );
    }

    nettools_sniff_bound = 0;
    log_i("NetTools: broadcast listener stopped");
    nettools_sniff_handle = NULL;
    vTaskDelete( NULL );
}

#endif // NATIVE_64BIT

static void nettools_sniff_start( void ) {
    #ifndef NATIVE_64BIT
        if ( nettools_sniff_handle )
            return;
        nettools_sniff_stop = false;
        nettools_sweep_abort = false;
        nettools_sweep_request = false;
        xTaskCreatePinnedToCore( NetTools_sniff_worker, "NetTools sniff", 3072, NULL, 1, &nettools_sniff_handle, 0 );
    #endif
}

static void nettools_sniff_stop_worker( void ) {
    #ifndef NATIVE_64BIT
        nettools_sniff_stop = true;
        nettools_sweep_abort = true;
        nettools_sweep_request = false;
    #endif
}

static bool nettools_tile_visible( lv_obj_t *tile ) {
    if ( !tile )
        return( false );

    lv_area_t area;
    lv_obj_get_coords( tile, &area );
    return( area.x1 > -4 && area.x1 < 4 && area.y1 > -4 && area.y1 < 4 );
}

static void nettools_sniff_park( void ) {
    nettools_sniff_stop_worker();
    if ( NetTools_sniff_lv_task )
        lv_task_set_prio( NetTools_sniff_lv_task, LV_TASK_PRIO_OFF );
}

void NetTools_sniff_arm( void ) {
    nettools_sniff_idle_rounds = 0;
    if ( NetTools_sniff_lv_task )
        lv_task_set_prio( NetTools_sniff_lv_task, LV_TASK_PRIO_MID );
}

static void nettools_sniff_set_status( const char *text ) {
    lv_label_set_text( NetTools_sniff_status_label, text );
    nettools_sniff_status_hold = millis() + NETTOOLS_SNIFF_HOLD;
}

static void nettools_sniff_set_scan_btn( bool stop ) {
    if ( stop == nettools_sniff_scan_btn_is_stop )
        return;

    lv_img_set_src( lv_obj_get_child( NetTools_sniff_scan_btn, NULL ), stop ? &wf_get_stop_img() : &wf_get_refresh_img() );
    nettools_sniff_scan_btn_is_stop = stop;
}

static void nettools_sniff_update_header( void ) {
    if ( millis() < nettools_sniff_status_hold )
        return;

    char line[ NETTOOLS_SNIFF_LINE_SIZE ];

    if ( nettools_sweep_running )
        snprintf( line, sizeof( line ), "scan %d/%d", nettools_sweep_done, nettools_sweep_total );
    else
        snprintf( line, sizeof( line ), "%d ports  %d pkt  %d dev", nettools_sniff_bound, ( int )nettools_sniff_packets, nettools_sniff_entries );

    lv_label_set_text( NetTools_sniff_status_label, line );
}

static void NetTools_sniff_activate_cb( void ) {
    NetTools_sniff_arm();
    nettools_sniff_start();
    nettools_sniff_changed = true;
}

static void NetTools_sniff_hibernate_cb( void ) {
    nettools_sniff_stop_worker();
}

static bool NetTools_sniff_powermgm_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case( POWERMGM_STANDBY ):   nettools_sniff_park();
                                    break;
        case( POWERMGM_WAKEUP ):    if ( nettools_tile_visible( NetTools_sniff_tile ) )
                                        NetTools_sniff_arm();
                                    break;
    }
    return( true );
}

static void NetTools_sniff_update_task( lv_task_t *task ) {
    bool sniff_visible = nettools_tile_visible( NetTools_sniff_tile );
    bool app_visible = sniff_visible || nettools_tile_visible( mainbar_get_tile_obj( NetTools_get_app_main_tile_num() ) );

    if ( app_visible ) {
        nettools_sniff_idle_rounds = 0;
    }
    else if ( ++nettools_sniff_idle_rounds > NETTOOLS_SNIFF_IDLE_ROUNDS ) {
        nettools_sniff_park();
        return;
    }

    if ( sniff_visible )
        nettools_sniff_start();
    else
        nettools_sniff_stop_worker();

    if ( !sniff_visible )
        return;

    nettools_sniff_set_scan_btn( nettools_sweep_running );
    nettools_sniff_update_header();

    if ( !nettools_sniff_changed )
        return;

    nettools_sniff_changed = false;
    NetTools_sniff_refresh();
}

static void NetTools_sniff_refresh( void ) {
    static const char *kind_name[] = { "WOL", "NBT", "DHCP", "ARP" };

    for ( int i = 0 ; i < NETTOOLS_SNIFF_ENTRIES ; i++ ) {
        if ( !NetTools_sniff_row[ i ] )
            continue;

        if ( i >= nettools_sniff_entries ) {
            lv_obj_set_hidden( NetTools_sniff_row[ i ], true );
            continue;
        }

        nettools_sniff_entry_t entry;
        NETTOOLS_SNIFF_LOCK();
        memcpy( &entry, &nettools_sniff_table[ i ], sizeof( entry ) );
        NETTOOLS_SNIFF_UNLOCK();

        const char *detail = entry.host[ 0 ] ? entry.host : entry.mac;
        char line[ NETTOOLS_SNIFF_LINE_SIZE ];

        if ( detail[ 0 ] ) {
            snprintf( line, sizeof( line ), "%s %s", kind_name[ entry.kind ], detail );
        }
        else {
            snprintf( line, sizeof( line ), "%s %d.%d.%d.%d", kind_name[ entry.kind ],
                      ( int )( entry.ip & 0xff ), ( int )( ( entry.ip >> 8 ) & 0xff ),
                      ( int )( ( entry.ip >> 16 ) & 0xff ), ( int )( ( entry.ip >> 24 ) & 0xff ) );
        }

        lv_label_set_text( lv_list_get_btn_label( NetTools_sniff_row[ i ] ), line );
        lv_obj_set_hidden( NetTools_sniff_row[ i ], false );
    }
}

static void row_NetTools_sniff_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ): {
            int slot = ( int )( intptr_t )lv_obj_get_user_data( obj );
            if ( slot < 0 || slot >= nettools_sniff_entries )
                break;

            nettools_sniff_entry_t entry;
            NETTOOLS_SNIFF_LOCK();
            memcpy( &entry, &nettools_sniff_table[ slot ], sizeof( entry ) );
            NETTOOLS_SNIFF_UNLOCK();

            if ( !nettools_mac_valid( entry.mac ) ) {
                nettools_sniff_set_status( "no MAC" );
                motor_vibe( 7 );
                break;
            }

            if ( !NetTools_setup_add_target( entry.mac, entry.host ) ) {
                nettools_sniff_set_status( "no free slot" );
                motor_vibe( 7 );
                break;
            }

            motor_vibe( 7 );
            mainbar_jump_to_tilenumber( NetTools_get_app_setup_tile_num(), LV_ANIM_ON, true );
            break;
        }
    }
}

static void scan_NetTools_sniff_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):
            motor_vibe( 7 );
            #ifdef NATIVE_64BIT
                nettools_sniff_set_status( "no wifi" );
            #else
                if ( nettools_sweep_running )
                    nettools_sweep_abort = true;
                else if ( !nettools_sniff_bound )
                    nettools_sniff_set_status( "no listener" );
                else
                    nettools_sweep_request = true;
            #endif
            break;
    }
}

static void clear_NetTools_sniff_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):
            NETTOOLS_SNIFF_LOCK();
            nettools_sniff_entries = 0;
            NETTOOLS_SNIFF_UNLOCK();
            #ifndef NATIVE_64BIT
                memset( nettools_sniff_pkt, 0, sizeof( nettools_sniff_pkt ) );
                memset( nettools_sniff_drop_log, 0, sizeof( nettools_sniff_drop_log ) );
            #endif
            nettools_sniff_packets = 0;
            nettools_sniff_status_hold = 0;
            NetTools_sniff_refresh();
            break;
    }
}

static void back_NetTools_sniff_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_jump_back();
                                        break;
    }
}
