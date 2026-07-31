#ifndef __SYS_MQTT_H__
#define __SYS_MQTT_H__

#include "common.h"
#include "network_config.h"

typedef enum
{
    SYS_MQTT_STATE_DISABLED = 0,
    SYS_MQTT_STATE_WAIT_CONFIG,
    SYS_MQTT_STATE_WAIT_NETWORK,
    SYS_MQTT_STATE_CONFIG_RECV,
    SYS_MQTT_STATE_CONFIG_VERSION,
    SYS_MQTT_STATE_CONFIG_KEEPALIVE,
    SYS_MQTT_STATE_CONFIG_SESSION,
    SYS_MQTT_STATE_CONFIG_TIMEOUT,
    SYS_MQTT_STATE_CONFIG_WILL,
    SYS_MQTT_STATE_OPENING,
    SYS_MQTT_STATE_CONNECTING,
    SYS_MQTT_STATE_SUBSCRIBE_DOWNLINK,
    SYS_MQTT_STATE_SUBSCRIBE_UPGRADE,
    SYS_MQTT_STATE_READY,
    SYS_MQTT_STATE_PUBLISH_HEADER,
    SYS_MQTT_STATE_PUBLISH_PAYLOAD,
    SYS_MQTT_STATE_DISCONNECTING,
    SYS_MQTT_STATE_CLOSING,
    SYS_MQTT_STATE_RECOVERY_WAIT
} sys_mqtt_state_en;

typedef enum
{
    SYS_MQTT_RESULT_NONE = 0,
    SYS_MQTT_RESULT_SUCCESS,
    SYS_MQTT_RESULT_REJECTED,
    SYS_MQTT_RESULT_TIMEOUT,
    SYS_MQTT_RESULT_LINK_LOST,
    SYS_MQTT_RESULT_CANCELLED
} sys_mqtt_result_en;

typedef struct
{
    char server_host[NETWORK_MQTT_SERVER_HOST_CAPACITY];
    u16 server_port;
    char client_id[16];
    char username[16];
    char password[16];
    char downlink_topic[NETWORK_MQTT_TOPIC_CAPACITY];
    char upgrade_topic[NETWORK_MQTT_TOPIC_CAPACITY];
    char will_topic[NETWORK_MQTT_TOPIC_CAPACITY];
    u8 will_payload[NETWORK_MQTT_WILL_PAYLOAD_CAPACITY];
    u16 will_payload_length;
} sys_mqtt_config_st;

typedef struct
{
    char topic[NETWORK_MQTT_TOPIC_CAPACITY];
    char payload[NETWORK_MQTT_PAYLOAD_CAPACITY + 1U];
    u16 payload_length;
    u16 session_generation;
} sys_mqtt_message_st;

typedef struct
{
    sys_mqtt_result_en result;
    u16 source_id;
    u32 request_id;
    u16 packet_id;
    u16 session_generation;
    u32 timestamp_ms;
} sys_mqtt_publish_result_st;

typedef struct
{
    sys_mqtt_state_en state;
    boolean_en enabled;
    boolean_en configured;
    boolean_en opened;
    boolean_en connected;
    boolean_en downlink_subscribed;
    boolean_en upgrade_subscribed;
    boolean_en ready;
    u16 session_generation;
    u16 next_packet_id;
    u16 active_packet_id;
    u16 active_source_id;
    u32 active_request_id;
    u32 state_since_ms;
    u32 last_puback_ms;
    u32 last_message_ms;
    u32 publish_success_count;
    u32 publish_fail_count;
    u32 publish_timeout_count;
    u32 message_drop_count;
    u8 queued_publish_count;
    u8 queued_message_count;
    u8 queued_result_count;
    u8 recovery_count;
    s16 last_status_code;
} sys_mqtt_snapshot_st;

extern void sys_mqtt_init(void);
extern void sys_mqtt_process(void);
extern void sys_mqtt_set_enabled(boolean_en enabled);
extern boolean_en sys_mqtt_configure(const sys_mqtt_config_st *config);
extern boolean_en sys_mqtt_publish(
    const char *topic,
    const u8 *payload,
    u16 payload_length,
    u8 priority,
    u16 source_id,
    u32 request_id);
extern boolean_en sys_mqtt_get_message(sys_mqtt_message_st *message);
extern boolean_en sys_mqtt_get_publish_result(
    sys_mqtt_publish_result_st *result);
extern void sys_mqtt_request_recovery(void);
extern void sys_mqtt_get_snapshot(sys_mqtt_snapshot_st *snapshot);
extern void sys_mqtt_on_urc(
    const u8 *line,
    u16 length,
    u16 owner_id,
    void *context);

#endif
