/****************************************************************************
 *   Mo May 23 00:08:51 2021
 *   Copyright  2021  Dirk Sarodnick
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

#ifndef _MQTT_H
    #define _MQTT_H
    
    #include <PubSubClient.h>
    #include "hardware/callback.h"
    #include "stdint.h"

    #define MQTT_MSG_QUEUE_LEN          8       /** @brief mqtt message queue depth */
    #define MQTT_TOPIC_LEN              64      /** @brief mqtt topic buffer size */
    #define MQTT_PAYLOAD_LEN            64      /** @brief mqtt payload buffer size */
    #define MQTT_DELAY                  100     /** @brief mqtt task delay while connected and awake */
    #define MQTT_STANDBY_DELAY          1000    /** @brief mqtt task delay while in standby or not connected */
    #define MQTT_IDLE_DELAY             5000    /** @brief mqtt task delay while wifi is off */

    typedef std::function<void(char* topic, byte* payload, size_t len)> MqttMessageCallback;
    /**
     * @brief mqtt event enum
     */
    enum mqtt_event_t {
        MQTTCTL_OFF                    = _BV(0),            /** @brief mqtt off event */
        MQTTCTL_CONNECT                = _BV(1),            /** @brief mqtt connect event */
        MQTTCTL_DISCONNECT             = _BV(2),            /** @brief mqtt disconnect event */
        MQTTCTL_ON                     = _BV(3),            /** @brief mqtt switch on event */
        MQTTCTL_CONFIGURED             = _BV(4),            /** @brief mqtt server credentials known */
        MQTTCTL_CONNECT_REQUEST        = _BV(5),            /** @brief mqtt connect request event */
        MQTTCTL_DISCONNECT_REQUEST     = _BV(6),            /** @brief mqtt disconnect request event */
        MQTTCTL_PUBLISH_REQUEST        = _BV(7)             /** @brief mqtt publish device state request event */
    };
    /**
     * @brief mqtt message type enum, a message carries data and can not be an event
     */
    typedef enum {
        MQTT_MSG_SUBSCRIBE,
        MQTT_MSG_UNSUBSCRIBE,
        MQTT_MSG_PUBLISH
    } mqtt_msg_type_t;
    /**
     * @brief mqtt message structure
     */
    typedef struct {
        mqtt_msg_type_t type;                               /** @brief message type */
        bool retain;                                        /** @brief retain flag, publish only */
        bool has_payload;                                   /** @brief false means publish a NULL payload */
        char topic[ MQTT_TOPIC_LEN ];                       /** @brief topic to work on */
        char payload[ MQTT_PAYLOAD_LEN ];                   /** @brief payload to publish */
    } mqtt_msg_t;

    /**
     *  @brief init builtin mqtt.
     */
    void mqtt_init( void );

    /**
     *  @brief setup builtin mqtt
     */
    void mqtt_start();

    /**
     *  @brief setup builtin mqtt, call after first wifi-connection.
     */
    void mqtt_start( const char *id, bool ssl, const char *server, int32_t port, const char *user, const char *pass );

    /**
     *  @brief stop builtin mqtt.
     */
    void mqtt_stop( void );

    /**
     * @brief subscribes to a specified mqtt topic
     * 
     * @param   topic  to which should be subscribed
     */
    void mqtt_subscribe(const char* topic);

    /**
     * @brief unsubscribes to a specified mqtt topic
     * 
     * @param   topic  to which should be unsubscribed
     */
    void mqtt_unsubscribe(const char* topic);

    /**
     * @brief publishes to a specified mqtt topic
     * 
     * @param   topic  to which should be published
     * @param   retain  if the message should be retained
     * @param   payload  that should be published
     */
    void mqtt_publish(const char* topic, bool retain, const char* payload = (const char*)nullptr);

    /**
     *  @brief publish online state.
     */
    void mqtt_publish_state();

    /**
     *  @brief publish battery state.
     */
    void mqtt_publish_battery();

    /**
     *  @brief publish version.
     */
    void mqtt_publish_version();

    /**
     *  @brief publish ambient temperature state.
     */
    void mqtt_publish_ambient_temperature();

    /**
     *  @brief publish power temperature state.
     */
    void mqtt_publish_power_temperature();

    /**
     *  @brief publish heap state.
     */
    void mqtt_publish_heap();

    /**
     *  @brief publish psram state.
     */
    void mqtt_publish_psram();

    /**
     *  @brief publish sketch state.
     */
    void mqtt_publish_sketch();

    /**
     *  @brief get connection state.
     */
    bool mqtt_get_connected();
    
    /**
     * @brief registers a callback function which is called on a corresponding event
     * 
     * @param   event  possible values: MQTT_CONNECT,
     *                                  MQTT_DISCONNECT
     * @param   mqtt_event_cb   pointer to the callback function 
     * @param   id      program id
     */
    bool mqtt_register_cb( EventBits_t event, CALLBACK_FUNC callback_func, const char *id );

    /**
     *  @brief registers a callback function which is called on receiving a mqtt message for a subscribed topic
     */
    void mqtt_register_message_cb(MqttMessageCallback callback);

#endif // _MQTT_H