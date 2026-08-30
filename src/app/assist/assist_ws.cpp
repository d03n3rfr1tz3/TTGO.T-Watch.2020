/****************************************************************************
 *   Aug 29 20:00:00 2026
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
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_websocket_client.h>

#include "assist_ws.h"
#include "assist_config.h"

#include "hardware/micctl.h"
#include "hardware/powermgm.h"
#include "utils/alloc.h"

static esp_websocket_client_handle_t assist_ws_client = NULL;
static volatile assist_ws_state_t assist_ws_state = ASSIST_WS_OFF;
static volatile bool assist_ws_stopping = false;
static uint32_t assist_ws_deadline = 0;
static char assist_ws_message[ ASSIST_WS_MESSAGE_LEN ] = "";
static char assist_ws_token[ ASSIST_TOKEN_LEN ] = "";
static char assist_ws_client_name[ ASSIST_WS_CLIENT_NAME_LEN ] = "";
static char assist_ws_issued_token[ ASSIST_TOKEN_LEN ] = "";

static char *assist_ws_buffer = NULL;
static uint32_t assist_ws_buffer_len = 0;
static bool assist_ws_buffer_drop = false;

static char assist_ws_pipeline_id[ ASSIST_WS_PIPELINE_MAX ][ ASSIST_PIPELINE_LEN ];
static char assist_ws_pipeline_options[ ASSIST_WS_PIPELINE_MAX * ASSIST_WS_PIPELINE_NAME_LEN ] = "";
static volatile uint8_t assist_ws_pipeline_count = 0;
static volatile bool assist_ws_pipeline_fresh = false;

static SemaphoreHandle_t assist_ws_send_mutex = NULL;               /** @brief held by every sender outside the client task, and by the teardown */
static uint8_t assist_ws_audio_frame[ ASSIST_WS_AUDIO_FRAME + 1 ];  /** @brief handler byte plus pcm, only the sender task uses it */
static volatile assist_ws_run_t assist_ws_run_state = ASSIST_RUN_OFF;
static volatile uint32_t assist_ws_run_id = ASSIST_WS_ID_RUN;
static volatile uint8_t assist_ws_handler_id = 0;
static volatile bool assist_ws_text_fresh = false;
static char assist_ws_transcript[ ASSIST_WS_TRANSCRIPT_LEN ] = "";
static char assist_ws_answer[ ASSIST_WS_ANSWER_LEN ] = "";
static char assist_ws_conversation_id[ ASSIST_WS_CONV_ID_LEN ] = "";

static bool assist_ws_powermgm_event_cb( EventBits_t event, void *arg );
static void assist_ws_event_cb( void *arg, esp_event_base_t base, int32_t id, void *event_data );
static void assist_ws_collect( esp_websocket_event_data_t *data );
static void assist_ws_handle_message( const char *msg );
static void assist_ws_handle_event( JsonDocument &doc );
static void assist_ws_handle_token( const char *msg );
static void assist_ws_handle_pipelines( const char *msg );
static void assist_ws_set_message( const char *msg );
static bool assist_ws_send( const char *data, uint32_t len, bool binary );
static void assist_ws_teardown( void );
static void assist_ws_stop_task( void *arg );

void assist_ws_setup( void ) {
    assist_ws_send_mutex = xSemaphoreCreateMutex();

    powermgm_register_cb_with_prio( POWERMGM_STANDBY, assist_ws_powermgm_event_cb, "assist ws", CALL_CB_FIRST );
}

bool assist_ws_connect( const char *token ) {
    assist_config_t *assist_config = assist_get_config();
    char uri[ ASSIST_HOST_LEN + 32 ] = "";

    if( assist_ws_client || assist_ws_stopping )
        return( false );

    if( !assist_config->host[ 0 ] || !token || !token[ 0 ] ) {
        assist_ws_state = ASSIST_WS_OFF;
        assist_ws_set_message( "not paired" );
        return( false );
    }

    if( WiFi.status() != WL_CONNECTED ) {
        assist_ws_state = ASSIST_WS_OFF;
        assist_ws_set_message( "no wifi" );
        return( false );
    }

    assist_ws_buffer = ( char* )MALLOC( ASSIST_WS_BUFFER_SIZE );
    if( !assist_ws_buffer ) {
        assist_ws_state = ASSIST_WS_OFF;
        assist_ws_set_message( "out of memory" );
        return( false );
    }

    assist_ws_buffer_len = 0;
    assist_ws_buffer_drop = false;
    assist_ws_issued_token[ 0 ] = '\0';
    assist_ws_conversation_id[ 0 ] = '\0';
    assist_ws_run_id = ASSIST_WS_ID_RUN - 1;
    assist_ws_run_state = ASSIST_RUN_OFF;
    snprintf( assist_ws_token, sizeof( assist_ws_token ), "%s", token );
    snprintf( uri, sizeof( uri ), "ws://%s:%d%s", assist_config->host, assist_config->port, ASSIST_WS_PATH );

    esp_websocket_client_config_t config;
    memset( &config, 0, sizeof( config ) );
    config.uri = uri;
    config.transport = WEBSOCKET_TRANSPORT_OVER_TCP;
    config.buffer_size = ASSIST_WS_FRAME_SIZE;
    config.task_stack = ASSIST_WS_TASK_STACK;
    config.task_prio = ASSIST_WS_TASK_PRIO;
    config.disable_auto_reconnect = true;
    config.pingpong_timeout_sec = 0;

    assist_ws_client = esp_websocket_client_init( &config );
    if( !assist_ws_client ) {
        free( assist_ws_buffer );
        assist_ws_buffer = NULL;
        assist_ws_state = ASSIST_WS_OFF;
        assist_ws_set_message( "no socket" );
        return( false );
    }

    esp_websocket_register_events( assist_ws_client, WEBSOCKET_EVENT_ANY, assist_ws_event_cb, NULL );

    assist_ws_state = ASSIST_WS_CONNECTING;
    assist_ws_deadline = millis() + ASSIST_WS_TIMEOUT;
    assist_ws_set_message( "connecting..." );

    if( esp_websocket_client_start( assist_ws_client ) != ESP_OK ) {
        assist_ws_teardown();
        assist_ws_set_message( "no socket" );
        return( false );
    }

    log_i("assist: connecting to %s", uri );

    return( true );
}

void assist_ws_disconnect( void ) {
    if( !assist_ws_client || assist_ws_stopping )
        return;

    assist_ws_stopping = true;

    if( xTaskCreatePinnedToCore( assist_ws_stop_task, "assist ws stop", 3072, NULL, 1, NULL, 0 ) != pdPASS ) {
        log_e("assist: stop task failed, closing inline");
        assist_ws_teardown();
    }
}

assist_ws_state_t assist_ws_get_state( void ) {
    if( assist_ws_state == ASSIST_WS_CONNECTING || assist_ws_state == ASSIST_WS_AUTH ) {
        if( ( int32_t )( millis() - assist_ws_deadline ) >= 0 ) {
            assist_ws_state = ASSIST_WS_ERROR;
            assist_ws_set_message( "no answer" );
        }
    }

    return( assist_ws_state );
}

const char *assist_ws_get_message( void ) {
    return( assist_ws_message );
}

void assist_ws_request_token( const char *client_name ) {
    snprintf( assist_ws_client_name, sizeof( assist_ws_client_name ), "%s", client_name ? client_name : "" );
}

const char *assist_ws_get_issued_token( void ) {
    return( assist_ws_issued_token );
}

bool assist_ws_take_pipelines( void ) {
    if( !assist_ws_pipeline_fresh )
        return( false );

    assist_ws_pipeline_fresh = false;

    return( true );
}

const char *assist_ws_get_pipeline_options( void ) {
    return( assist_ws_pipeline_options );
}

uint8_t assist_ws_get_pipeline_count( void ) {
    return( assist_ws_pipeline_count );
}

const char *assist_ws_get_pipeline_id( uint8_t index ) {
    if( index >= assist_ws_pipeline_count )
        return( "" );

    return( assist_ws_pipeline_id[ index ] );
}

bool assist_ws_run( const char *pipeline ) {
    char pipeline_arg[ ASSIST_PIPELINE_LEN + 16 ] = "";
    char conversation_arg[ ASSIST_WS_CONV_ID_LEN + 24 ] = "";
    char buf[ ASSIST_PIPELINE_LEN + ASSIST_WS_CONV_ID_LEN + 224 ] = "";

    if( assist_ws_state != ASSIST_WS_READY )
        return( false );

    if( pipeline && pipeline[ 0 ] )
        snprintf( pipeline_arg, sizeof( pipeline_arg ), ",\"pipeline\":\"%s\"", pipeline );

    if( assist_ws_conversation_id[ 0 ] )
        snprintf( conversation_arg, sizeof( conversation_arg ), ",\"conversation_id\":\"%s\"", assist_ws_conversation_id );

    assist_ws_transcript[ 0 ] = '\0';
    assist_ws_answer[ 0 ] = '\0';
    assist_ws_handler_id = 0;
    assist_ws_run_id++;
    assist_ws_run_state = ASSIST_RUN_STARTING;
    assist_ws_text_fresh = true;

    snprintf( buf, sizeof( buf ), "{\"id\":%u,\"type\":\"assist_pipeline/run\",\"start_stage\":\"stt\",\"end_stage\":\"intent\","
                                  "\"input\":{\"sample_rate\":%d},\"timeout\":%d%s%s}",
              ( uint32_t )assist_ws_run_id, MICCTL_DEFAULT_SAMPLE_RATE, ASSIST_WS_HA_TIMEOUT, pipeline_arg, conversation_arg );

    if( !assist_ws_send( buf, strlen( buf ), false ) ) {
        assist_ws_run_state = ASSIST_RUN_FAILED;
        assist_ws_set_message( "send failed" );
        return( false );
    }

    return( true );
}

void assist_ws_run_reset( void ) {
    assist_ws_run_state = ASSIST_RUN_OFF;
    assist_ws_run_id++;
}

assist_ws_run_t assist_ws_get_run( void ) {
    return( assist_ws_run_state );
}

bool assist_ws_send_audio( const void *pcm, uint32_t len ) {
    if( assist_ws_run_state != ASSIST_RUN_LISTENING || !len || len > ASSIST_WS_AUDIO_FRAME )
        return( false );

    assist_ws_audio_frame[ 0 ] = assist_ws_handler_id;
    memcpy( assist_ws_audio_frame + 1, pcm, len );

    return( assist_ws_send( ( const char* )assist_ws_audio_frame, len + 1, true ) );
}

bool assist_ws_send_audio_end( void ) {
    if( assist_ws_run_state == ASSIST_RUN_OFF || !assist_ws_handler_id )
        return( false );

    assist_ws_audio_frame[ 0 ] = assist_ws_handler_id;

    return( assist_ws_send( ( const char* )assist_ws_audio_frame, 1, true ) );
}

bool assist_ws_take_text( void ) {
    if( !assist_ws_text_fresh )
        return( false );

    assist_ws_text_fresh = false;

    return( true );
}

const char *assist_ws_get_transcript( void ) {
    return( assist_ws_transcript );
}

const char *assist_ws_get_answer( void ) {
    return( assist_ws_answer );
}

static bool assist_ws_send( const char *data, uint32_t len, bool binary ) {
    int sent = -1;

    if( !assist_ws_send_mutex )
        return( false );

    xSemaphoreTake( assist_ws_send_mutex, portMAX_DELAY );

    if( assist_ws_client && !assist_ws_stopping ) {
        if( binary )
            sent = esp_websocket_client_send_bin( assist_ws_client, data, len, pdMS_TO_TICKS( ASSIST_WS_SEND_TIMEOUT ) );
        else
            sent = esp_websocket_client_send_text( assist_ws_client, data, len, pdMS_TO_TICKS( ASSIST_WS_SEND_TIMEOUT ) );
    }

    xSemaphoreGive( assist_ws_send_mutex );

    return( sent >= 0 && ( uint32_t )sent == len );
}

static void assist_ws_set_message( const char *msg ) {
    snprintf( assist_ws_message, sizeof( assist_ws_message ), "%s", msg );
}

static void assist_ws_teardown( void ) {
    if( assist_ws_send_mutex )
        xSemaphoreTake( assist_ws_send_mutex, portMAX_DELAY );

    esp_websocket_client_stop( assist_ws_client );
    esp_websocket_client_destroy( assist_ws_client );
    assist_ws_client = NULL;

    if( assist_ws_send_mutex )
        xSemaphoreGive( assist_ws_send_mutex );

    assist_ws_run_state = ASSIST_RUN_OFF;

    free( assist_ws_buffer );
    assist_ws_buffer = NULL;
    assist_ws_buffer_len = 0;

    assist_ws_set_message( "" );
    assist_ws_state = ASSIST_WS_OFF;
    assist_ws_stopping = false;
}

static void assist_ws_stop_task( void *arg ) {
    assist_ws_teardown();
    vTaskDelete( NULL );
}

static void assist_ws_event_cb( void *arg, esp_event_base_t base, int32_t id, void *event_data ) {
    switch( id ) {
        case( WEBSOCKET_EVENT_CONNECTED ):      break;
        case( WEBSOCKET_EVENT_DATA ):           assist_ws_collect( ( esp_websocket_event_data_t* )event_data );
                                                break;
        case( WEBSOCKET_EVENT_DISCONNECTED ):
        case( WEBSOCKET_EVENT_ERROR ):          if( assist_ws_state == ASSIST_WS_CONNECTING || assist_ws_state == ASSIST_WS_AUTH )
                                                    assist_ws_set_message( "no answer" );
                                                else if( assist_ws_state != ASSIST_WS_ERROR )
                                                    assist_ws_set_message( "disconnected" );
                                                assist_ws_state = ASSIST_WS_ERROR;
                                                break;
    }
}

/*
 * a message arrives in chunks
 */
static void assist_ws_collect( esp_websocket_event_data_t *data ) {
    if( !assist_ws_buffer )
        return;

    if( data->payload_offset == 0 ) {
        assist_ws_buffer_len = 0;
        assist_ws_buffer_drop = false;
    }

    if( data->data_len > 0 ) {
        if( assist_ws_buffer_len + data->data_len < ASSIST_WS_BUFFER_SIZE ) {
            memcpy( assist_ws_buffer + assist_ws_buffer_len, data->data_ptr, data->data_len );
            assist_ws_buffer_len += data->data_len;
        }
        else if( !assist_ws_buffer_drop ) {
            assist_ws_buffer_drop = true;
            log_e("assist: message of %d bytes does not fit", data->payload_len );
        }
    }

    if( data->payload_offset + data->data_len < data->payload_len )
        return;

    if( !assist_ws_buffer_drop && assist_ws_buffer_len && assist_ws_buffer[ 0 ] == '{' ) {
        assist_ws_buffer[ assist_ws_buffer_len ] = '\0';
        assist_ws_handle_message( assist_ws_buffer );
    }

    assist_ws_buffer_len = 0;
    assist_ws_buffer_drop = false;
}

static void assist_ws_handle_message( const char *msg ) {
    StaticJsonDocument< 1024 > filter;

    filter["id"] = true;
    filter["type"] = true;
    filter["message"] = true;
    filter["success"] = true;
    filter["error"]["code"] = true;
    filter["error"]["message"] = true;
    filter["event"]["type"] = true;
    filter["event"]["data"]["code"] = true;
    filter["event"]["data"]["message"] = true;
    filter["event"]["data"]["runner_data"]["stt_binary_handler_id"] = true;
    filter["event"]["data"]["stt_output"]["text"] = true;
    filter["event"]["data"]["intent_output"]["conversation_id"] = true;
    filter["event"]["data"]["intent_output"]["response"]["speech"]["plain"]["speech"] = true;

    DynamicJsonDocument doc( ASSIST_WS_JSON_SIZE );

    if( deserializeJson( doc, msg, DeserializationOption::Filter( filter ) ) ) {
        log_e("assist: json error in %.256s", msg );
        return;
    }

    const char *type = doc["type"] | "";

    if( !strcmp( type, "auth_required" ) ) {
        char buf[ ASSIST_TOKEN_LEN + 48 ] = "";

        snprintf( buf, sizeof( buf ), "{\"type\":\"auth\",\"access_token\":\"%s\"}", assist_ws_token );
        assist_ws_state = ASSIST_WS_AUTH;
        esp_websocket_client_send_text( assist_ws_client, buf, strlen( buf ), pdMS_TO_TICKS( ASSIST_WS_SEND_TIMEOUT ) );
    }
    else if( !strcmp( type, "auth_ok" ) ) {
        char buf[ ASSIST_WS_CLIENT_NAME_LEN + 96 ] = "";

        assist_ws_state = ASSIST_WS_READY;
        assist_ws_set_message( "connected" );

        if( assist_ws_client_name[ 0 ] ) {
            snprintf( buf, sizeof( buf ), "{\"id\":%d,\"type\":\"auth/long_lived_access_token\",\"client_name\":\"%s\",\"lifespan\":%d}",
                      ASSIST_WS_ID_TOKEN, assist_ws_client_name, ASSIST_WS_TOKEN_LIFESPAN );
            assist_ws_set_message( "asking for token" );
        }
        else {
            snprintf( buf, sizeof( buf ), "{\"id\":%d,\"type\":\"assist_pipeline/pipeline/list\"}", ASSIST_WS_ID_PIPELINES );
        }

        esp_websocket_client_send_text( assist_ws_client, buf, strlen( buf ), pdMS_TO_TICKS( ASSIST_WS_SEND_TIMEOUT ) );
    }
    else if( !strcmp( type, "result" ) && ( doc["id"] | 0 ) == ASSIST_WS_ID_TOKEN ) {
        if( doc["success"] | false ) {
            assist_ws_handle_token( msg );
        }
        else {
            assist_ws_state = ASSIST_WS_ERROR;
            assist_ws_set_message( "token refused" );
            log_e("assist: token request failed, %s", doc["error"]["message"] | "" );
        }
    }
    else if( !strcmp( type, "result" ) && ( doc["id"] | 0 ) == ASSIST_WS_ID_PIPELINES ) {
        if( doc["success"] | false )
            assist_ws_handle_pipelines( msg );
        else
            log_e("assist: pipeline list failed, %s", doc["error"]["message"] | "" );
    }
    else if( !strcmp( type, "event" ) && ( uint32_t )( doc["id"] | 0 ) == assist_ws_run_id ) {
        assist_ws_handle_event( doc );
    }
    else if( !strcmp( type, "result" ) && ( uint32_t )( doc["id"] | 0 ) == assist_ws_run_id ) {
        if( !( doc["success"] | false ) ) {
            assist_ws_run_state = ASSIST_RUN_FAILED;
            assist_ws_set_message( "run refused" );
            log_e("assist: run refused, %s", doc["error"]["message"] | "" );
        }
    }
    else if( !strcmp( type, "auth_invalid" ) ) {
        assist_ws_state = ASSIST_WS_ERROR;
        assist_ws_set_message( "token rejected" );
        log_e("assist: auth invalid, %s", doc["message"] | "" );
    }
    else {
        log_i("assist: unhandled message, %.256s", msg );
    }
}

static void assist_ws_handle_event( JsonDocument &doc ) {
    const char *event = doc["event"]["type"] | "";
    JsonVariant data = doc["event"]["data"];

    if( !strcmp( event, "run-start" ) ) {
        assist_ws_handler_id = data["runner_data"]["stt_binary_handler_id"] | 0;

        if( !assist_ws_handler_id ) {
            assist_ws_run_state = ASSIST_RUN_FAILED;
            assist_ws_set_message( "no audio handler" );
            log_e("assist: run-start without a handler id");
            return;
        }

        assist_ws_run_state = ASSIST_RUN_LISTENING;
    }
    else if( !strcmp( event, "stt-vad-end" ) ) {
        assist_ws_run_state = ASSIST_RUN_THINKING;
    }
    else if( !strcmp( event, "stt-end" ) ) {
        snprintf( assist_ws_transcript, sizeof( assist_ws_transcript ), "%s", data["stt_output"]["text"] | "" );
        assist_ws_run_state = ASSIST_RUN_THINKING;
        assist_ws_text_fresh = true;
    }
    else if( !strcmp( event, "intent-end" ) ) {
        snprintf( assist_ws_answer, sizeof( assist_ws_answer ), "%s", data["intent_output"]["response"]["speech"]["plain"]["speech"] | "" );
        snprintf( assist_ws_conversation_id, sizeof( assist_ws_conversation_id ), "%s", data["intent_output"]["conversation_id"] | "" );
        assist_ws_text_fresh = true;
    }
    else if( !strcmp( event, "run-end" ) ) {
        assist_ws_run_state = ASSIST_RUN_DONE;
    }
    else if( !strcmp( event, "error" ) ) {
        assist_ws_run_state = ASSIST_RUN_FAILED;
        assist_ws_set_message( data["code"] | "run error" );
        log_e("assist: run error, %s", data["message"] | "" );
    }
    else if( strcmp( event, "stt-start" ) && strcmp( event, "stt-vad-start" ) ) {
        log_i("assist: event %s", event );
    }
}

static void assist_ws_handle_token( const char *msg ) {
    StaticJsonDocument< 32 > filter;
    StaticJsonDocument< ASSIST_TOKEN_LEN + 128 > doc;

    filter["result"] = true;

    if( deserializeJson( doc, msg, DeserializationOption::Filter( filter ) ) || !( doc["result"] | "" )[ 0 ] ) {
        assist_ws_state = ASSIST_WS_ERROR;
        assist_ws_set_message( "token refused" );
        log_e("assist: no token in the answer");
        return;
    }

    snprintf( assist_ws_issued_token, sizeof( assist_ws_issued_token ), "%s", doc["result"] | "" );
    assist_ws_set_message( "token issued" );
}

static void assist_ws_handle_pipelines( const char *msg ) {
    StaticJsonDocument< 192 > filter;
    DynamicJsonDocument doc( ASSIST_WS_LIST_JSON_SIZE );
    uint8_t count = 0;
    uint32_t len = 0;

    filter["result"]["pipelines"][0]["id"] = true;
    filter["result"]["pipelines"][0]["name"] = true;

    if( deserializeJson( doc, msg, DeserializationOption::Filter( filter ) ) ) {
        log_e("assist: pipeline list does not fit into %d bytes", ASSIST_WS_LIST_JSON_SIZE );
        return;
    }

    for( JsonObject pipeline : doc["result"]["pipelines"].as< JsonArray >() ) {
        const char *id = pipeline["id"] | "";
        const char *name = pipeline["name"] | "";

        if( count >= ASSIST_WS_PIPELINE_MAX || !id[ 0 ] || !name[ 0 ] )
            break;

        len += snprintf( assist_ws_pipeline_options + len, ASSIST_WS_PIPELINE_NAME_LEN, count ? "\n%.*s" : "%.*s", ASSIST_WS_PIPELINE_NAME_LEN - 2, name );
        snprintf( assist_ws_pipeline_id[ count ], ASSIST_PIPELINE_LEN, "%s", id );
        count++;
    }

    if( !count ) {
        log_i("assist: no pipelines in %.256s", msg );
        return;
    }

    assist_ws_pipeline_count = count;
    assist_ws_pipeline_fresh = true;

    log_i("assist: %d pipelines", count );
}

static bool assist_ws_powermgm_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case POWERMGM_STANDBY:      assist_ws_disconnect();
                                    break;
    }
    return( true );
}
