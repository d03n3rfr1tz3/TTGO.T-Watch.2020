/****************************************************************************
 *   Aug 30 12:00:00 2026
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

#include <string.h>
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_system.h>

#include "assist_pair.h"
#include "assist_config.h"
#include "assist_ws.h"

static volatile assist_pair_state_t assist_pair_state = ASSIST_PAIR_OFF;
static volatile bool assist_pair_running = false;
static volatile bool assist_pair_stop_request = false;

static char assist_pair_url[ ASSIST_PAIR_URL_LEN ] = "";
static char assist_pair_client_id[ ASSIST_PAIR_CLIENT_ID_LEN ] = "";
static char assist_pair_message[ ASSIST_PAIR_MESSAGE_LEN ] = "";
static char assist_pair_code[ ASSIST_PAIR_CODE_LEN ] = "";
static char assist_pair_access_token[ ASSIST_TOKEN_LEN ] = "";
static char assist_pair_nonce[ 12 ] = "";

static void assist_pair_task( void *arg );
static void assist_pair_set_message( const char *msg );
static void assist_pair_fail( const char *msg );
static bool assist_pair_build_url( void );
static bool assist_pair_listen( void );
static bool assist_pair_exchange_code( void );
static bool assist_pair_issue_token( void );
static bool assist_pair_query_value( const char *line, const char *key, char *out, uint32_t len );
static void assist_pair_reply( WiFiClient &client, const char *body );

bool assist_pair_start( void ) {
    if( assist_pair_running )
        return( false );

    assist_pair_code[ 0 ] = '\0';
    assist_pair_access_token[ 0 ] = '\0';
    assist_pair_url[ 0 ] = '\0';

    if( WiFi.status() != WL_CONNECTED ) {
        assist_pair_fail( "no wifi" );
        return( false );
    }

    if( !assist_get_config()->host[ 0 ] ) {
        assist_pair_fail( "no host" );
        return( false );
    }

    if( !assist_pair_build_url() ) {
        assist_pair_fail( "host too long" );
        return( false );
    }

    assist_pair_stop_request = false;
    assist_pair_running = true;
    assist_pair_state = ASSIST_PAIR_WAIT;
    assist_pair_set_message( "scan the code" );

    if( xTaskCreatePinnedToCore( assist_pair_task, "assist pair", ASSIST_PAIR_TASK_STACK, NULL, 1, NULL, 0 ) != pdPASS ) {
        assist_pair_running = false;
        assist_pair_fail( "no task" );
        return( false );
    }

    return( true );
}

void assist_pair_stop( void ) {
    assist_pair_stop_request = true;
}

assist_pair_state_t assist_pair_get_state( void ) {
    return( assist_pair_state );
}

const char *assist_pair_get_url( void ) {
    return( assist_pair_url );
}

const char *assist_pair_get_message( void ) {
    return( assist_pair_message );
}

static void assist_pair_set_message( const char *msg ) {
    snprintf( assist_pair_message, sizeof( assist_pair_message ), "%s", msg );
}

static void assist_pair_fail( const char *msg ) {
    assist_pair_state = ASSIST_PAIR_ERROR;
    assist_pair_set_message( msg );
}

static bool assist_pair_build_url( void ) {
    assist_config_t *assist_config = assist_get_config();
    IPAddress ip = WiFi.localIP();
    int len = 0;

    snprintf( assist_pair_nonce, sizeof( assist_pair_nonce ), "%08x", esp_random() );

    len = snprintf( assist_pair_client_id, sizeof( assist_pair_client_id ), "http://%d.%d.%d.%d:%d/", ip[ 0 ], ip[ 1 ], ip[ 2 ], ip[ 3 ], ASSIST_PAIR_PORT );
    if( len < 0 || len >= ( int )sizeof( assist_pair_client_id ) )
        return( false );

    len = snprintf( assist_pair_url, sizeof( assist_pair_url ), "http://%s:%d/auth/authorize?client_id=%s&redirect_uri=%scb&state=%s",
                    assist_config->host, assist_config->port, assist_pair_client_id, assist_pair_client_id, assist_pair_nonce );
    if( len < 0 || len >= ( int )sizeof( assist_pair_url ) ) {
        assist_pair_url[ 0 ] = '\0';
        return( false );
    }

    return( true );
}

static void assist_pair_task( void *arg ) {
    if( assist_pair_listen() && assist_pair_exchange_code() && assist_pair_issue_token() ) {
        assist_config_t *assist_config = assist_get_config();

        snprintf( assist_config->token, sizeof( assist_config->token ), "%s", assist_pair_access_token );
        assist_config->save();

        assist_pair_state = ASSIST_PAIR_DONE;
        assist_pair_set_message( "done" );
    }
    else if( assist_pair_state != ASSIST_PAIR_ERROR ) {
        assist_pair_fail( assist_pair_stop_request ? "cancelled" : "timed out" );
    }

    assist_pair_code[ 0 ] = '\0';
    assist_pair_access_token[ 0 ] = '\0';
    assist_pair_running = false;
    vTaskDelete( NULL );
}

/*
 * the first request line carries everything we need
 */
static bool assist_pair_listen( void ) {
    WiFiServer server( ASSIST_PAIR_PORT, 1 );
    uint32_t deadline = millis() + ASSIST_PAIR_TIMEOUT;
    bool found = false;

    server.begin();
    if( !server ) {
        assist_pair_fail( "no listener" );
        return( false );
    }

    while( !assist_pair_stop_request && ( int32_t )( millis() - deadline ) < 0 ) {
        WiFiClient client = server.available();
        char state[ 12 ] = "";

        if( !client ) {
            delay( ASSIST_PAIR_PERIOD );
            continue;
        }

        client.setTimeout( 2 );

        String line = client.readStringUntil( '\n' );

        for( int i = 0 ; i < 24 && client.connected() ; i++ ) {
            String header = client.readStringUntil( '\n' );
            if( header.length() < 2 )
                break;
        }

        if( assist_pair_query_value( line.c_str(), "code", assist_pair_code, sizeof( assist_pair_code ) ) &&
            assist_pair_query_value( line.c_str(), "state", state, sizeof( state ) ) &&
            !strcmp( state, assist_pair_nonce ) ) {
            assist_pair_reply( client, "<h2>paired</h2><p>you can close this page.</p>" );
            found = true;
        }
        else {
            char page[ ASSIST_PAIR_URL_LEN + 96 ] = "";
            snprintf( page, sizeof( page ), "<h2>t-watch assist</h2><p><a href=\"%s\">sign in to home assistant</a></p>", assist_pair_url );
            assist_pair_reply( client, page );
            assist_pair_code[ 0 ] = '\0';
        }

        client.stop();

        if( found )
            break;
    }

    server.end();

    return( found );
}

static void assist_pair_reply( WiFiClient &client, const char *body ) {
    client.printf( "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s", ( int )strlen( body ), body );
    client.flush();
}

static bool assist_pair_query_value( const char *line, const char *key, char *out, uint32_t len ) {
    const char *query = strchr( line, '?' );
    uint32_t key_len = strlen( key );
    uint32_t i = 0;

    out[ 0 ] = '\0';

    if( !query )
        return( false );

    while( *query ) {
        const char *value = ++query;

        if( !strncmp( value, key, key_len ) && value[ key_len ] == '=' ) {
            value += key_len + 1;
            while( *value && *value != '&' && *value != ' ' && i < len - 1 )
                out[ i++ ] = *value++;
            out[ i ] = '\0';
            return( i > 0 );
        }

        query = strchr( query, '&' );
        if( !query )
            break;
    }

    return( false );
}

static bool assist_pair_exchange_code( void ) {
    assist_config_t *assist_config = assist_get_config();
    char url[ ASSIST_HOST_LEN + 32 ] = "";
    char payload[ ASSIST_PAIR_CODE_LEN + ASSIST_PAIR_CLIENT_ID_LEN + 64 ] = "";
    HTTPClient http;
    int code = 0;

    assist_pair_state = ASSIST_PAIR_EXCHANGE;
    assist_pair_set_message( "signing in..." );

    snprintf( url, sizeof( url ), "http://%s:%d/auth/token", assist_config->host, assist_config->port );
    snprintf( payload, sizeof( payload ), "grant_type=authorization_code&code=%s&client_id=%s", assist_pair_code, assist_pair_client_id );

    http.setConnectTimeout( 3000 );
    http.setTimeout( 5000 );
    http.useHTTP10( true );
    http.begin( url );
    http.addHeader( "Content-Type", "application/x-www-form-urlencoded" );

    code = http.POST( ( uint8_t* )payload, strlen( payload ) );

    if( code < 200 || code >= 300 ) {
        log_e("assist: token exchange failed with %d", code );
        http.end();
        assist_pair_fail( "code rejected" );
        return( false );
    }

    StaticJsonDocument< 64 > filter;
    filter["access_token"] = true;

    DynamicJsonDocument doc( 1024 );

    if( deserializeJson( doc, http.getStream(), DeserializationOption::Filter( filter ) ) ) {
        http.end();
        assist_pair_fail( "bad answer" );
        return( false );
    }

    http.end();

    snprintf( assist_pair_access_token, sizeof( assist_pair_access_token ), "%s", doc["access_token"] | "" );

    if( !assist_pair_access_token[ 0 ] ) {
        assist_pair_fail( "no access token" );
        return( false );
    }

    return( true );
}

/*
 * the ten year token comes over the websocket
 */
static bool assist_pair_issue_token( void ) {
    char name[ ASSIST_WS_CLIENT_NAME_LEN ] = "";
    IPAddress ip = WiFi.localIP();
    bool issued = false;

    assist_pair_state = ASSIST_PAIR_TOKEN;
    assist_pair_set_message( "getting token..." );

    for( int try_num = 1 ; try_num <= ASSIST_PAIR_NAME_TRIES && !issued && !assist_pair_stop_request ; try_num++ ) {
        uint32_t deadline = millis() + ASSIST_PAIR_WS_TIMEOUT;

        if( try_num == 1 )
            snprintf( name, sizeof( name ), "T-Watch Assist %d", ip[ 3 ] );
        else
            snprintf( name, sizeof( name ), "T-Watch Assist %d-%d", ip[ 3 ], try_num );

        assist_ws_request_token( name );

        while( !assist_ws_connect( assist_pair_access_token ) ) {
            if( assist_pair_stop_request || ( int32_t )( millis() - deadline ) >= 0 )
                break;
            delay( ASSIST_PAIR_PERIOD );
        }

        while( ( int32_t )( millis() - deadline ) < 0 ) {
            if( assist_ws_get_issued_token()[ 0 ] ) {
                snprintf( assist_pair_access_token, sizeof( assist_pair_access_token ), "%s", assist_ws_get_issued_token() );
                issued = true;
                break;
            }
            if( assist_ws_get_state() == ASSIST_WS_ERROR || assist_pair_stop_request )
                break;
            delay( ASSIST_PAIR_PERIOD );
        }

        assist_ws_disconnect();

        if( !issued )
            log_w("assist: pairing attempt %d as \"%s\" failed, %s", try_num, name, assist_ws_get_message() );
    }

    assist_ws_request_token( NULL );

    if( !issued && assist_pair_state != ASSIST_PAIR_ERROR )
        assist_pair_fail( assist_pair_stop_request ? "cancelled" : "no token" );

    return( issued );
}
