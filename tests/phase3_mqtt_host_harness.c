#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * 直接编入生产 sys_mqtt.c。AT、蜂窝、事件和单调时间均由主机桩替代，
 * 测试执行固件中的真实订阅门控、发布关联、URC 拷贝和恢复逻辑。
 */
#define __SYS_MQTT_H__
#define __SYS_AT_ENGINE_H__
#define __SYS_CELLULAR_H__
#define __SYS_EVENT_H__
#define __SYS_TIME_H__

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;

typedef enum
{
    BOOL_FALSE = 0,
    BOOL_TRUE = 1
} boolean_en;

#define NETWORK_MQTT_SERVER_HOST                    "127.0.0.1"
#define NETWORK_MQTT_SERVER_PORT                    ((u16)1883U)
#define NETWORK_MQTT_SERVER_HOST_CAPACITY           ((u16)48U)
#define NETWORK_MQTT_CLIENT_INDEX                   ((u8)0U)
#define NETWORK_MQTT_KEEPALIVE_SEC                  ((u16)30U)
#define NETWORK_MQTT_TOPIC_CAPACITY                 ((u16)64U)
#define NETWORK_MQTT_PAYLOAD_CAPACITY               ((u16)600U)
#define NETWORK_MQTT_WILL_PAYLOAD_CAPACITY          ((u16)192U)
#define NETWORK_MQTT_PUBLISH_QUEUE_CAPACITY         ((u8)4U)
#define NETWORK_MQTT_MESSAGE_QUEUE_CAPACITY         ((u8)2U)
#define NETWORK_MQTT_RESULT_QUEUE_CAPACITY          ((u8)4U)
#define NETWORK_MQTT_OPEN_TIMEOUT_MS                ((u32)30000U)
#define NETWORK_MQTT_CONNECT_TIMEOUT_MS             ((u32)10000U)
#define NETWORK_MQTT_SUBSCRIBE_TIMEOUT_MS           ((u32)10000U)
#define NETWORK_MQTT_CONFIG_TIMEOUT_MS              ((u32)2000U)
#define NETWORK_MQTT_PROMPT_TIMEOUT_MS              ((u32)5000U)
#define NETWORK_MQTT_PUBACK_TIMEOUT_MS              ((u32)15000U)
#define NETWORK_MQTT_CLOSE_TIMEOUT_MS               ((u32)5000U)
#define NETWORK_MQTT_RECOVERY_BACKOFF_FIRST_MS      ((u32)2000U)
#define NETWORK_MQTT_RECOVERY_BACKOFF_NEXT_MS       ((u32)5000U)
#define NETWORK_MQTT_PUBLISH_RETRY_DELAY_MS         ((u32)1000U)
#define SYS_RESOURCE_OWNER_MQTT                     ((u16)0x0302U)

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

typedef enum
{
    SYS_AT_RESULT_NONE = 0,
    SYS_AT_RESULT_OK,
    SYS_AT_RESULT_ERROR,
    SYS_AT_RESULT_TIMEOUT,
    SYS_AT_RESULT_TX_ERROR,
    SYS_AT_RESULT_CANCELLED,
    SYS_AT_RESULT_RESOURCE_BUSY
} sys_at_result_en;

typedef void (*sys_at_line_handler_fn)(
    const u8 *line,
    u16 length,
    u16 owner_id,
    void *context);
typedef void (*sys_at_complete_handler_fn)(
    sys_at_result_en result,
    u16 owner_id,
    u16 generation,
    void *context);

typedef struct
{
    const char *command;
    u16 command_length;
    const char *expected_token;
    const char *error_token;
    u32 timeout_ms;
    u8 retry_max;
    u8 priority;
    u16 owner_id;
    boolean_en expect_prompt;
    sys_at_line_handler_fn line_handler;
    sys_at_complete_handler_fn complete_handler;
    void *context;
} sys_at_request_st;

typedef struct
{
    boolean_en at_ready;
    boolean_en registered;
    boolean_en pdp_active;
    u16 generation;
    int recovery_reason;
} sys_cellular_snapshot_st;

typedef enum
{
    SYS_EVENT_NONE = 0,
    SYS_EVENT_MODEM_READY,
    SYS_EVENT_MODEM_NO_RESPONSE,
    SYS_EVENT_NETWORK_REGISTERED,
    SYS_EVENT_NETWORK_LOST,
    SYS_EVENT_MQTT_CONNECTED,
    SYS_EVENT_MQTT_SUBSCRIBED,
    SYS_EVENT_MQTT_DISCONNECTED,
    SYS_EVENT_MQTT_PUBACK,
    SYS_EVENT_MQTT_PUBLISH_FAILED,
    SYS_EVENT_MQTT_MESSAGE,
    SYS_EVENT_TYPE_COUNT
} sys_event_type_en;

typedef struct
{
    sys_event_type_en type;
    u16 source_id;
    u16 data_id;
    u32 timestamp_ms;
    u32 value;
} sys_event_st;

void sys_mqtt_on_urc(
    const u8 *line,
    u16 length,
    u16 owner_id,
    void *context);

static u32 host_now_ms;
static sys_at_request_st host_normal_request;
static sys_at_request_st host_continuation_request;
static char host_request_command[320];
static u8 host_continuation[NETWORK_MQTT_PAYLOAD_CAPACITY];
static u16 host_continuation_length;
static char host_continuation_expected[32];
static u32 host_continuation_timeout_ms;
static u32 host_submit_count;
static u32 host_continuation_submit_count;
static u32 host_cancel_count;
static u32 host_urc_registration_count;
static sys_at_line_handler_fn host_urc_handler;
static sys_event_st host_events[32];
static u8 host_event_count;
static u32 host_cellular_transport_open_count;
static sys_cellular_snapshot_st host_cellular_snapshot;

u32 sys_time_get_ms(void)
{
    return host_now_ms;
}

boolean_en sys_at_engine_submit(const sys_at_request_st *request)
{
    size_t command_length;

    assert(request != NULL);
    host_normal_request = *request;
    command_length = (request->command_length != 0U) ?
        request->command_length : strlen(request->command);
    assert(command_length < sizeof(host_request_command));
    memcpy(host_request_command, request->command, command_length);
    host_request_command[command_length] = '\0';
    host_submit_count++;
    return BOOL_TRUE;
}

boolean_en sys_at_engine_submit_continuation(
    const sys_at_request_st *request,
    const u8 *continuation,
    u16 continuation_length,
    const char *continuation_expected_token,
    u32 continuation_timeout_ms)
{
    size_t command_length;

    assert(request != NULL);
    assert(continuation != NULL);
    assert(continuation_expected_token != NULL);
    assert(continuation_length <= sizeof(host_continuation));
    host_continuation_request = *request;
    command_length = (request->command_length != 0U) ?
        request->command_length : strlen(request->command);
    assert(command_length < sizeof(host_request_command));
    memcpy(host_request_command, request->command, command_length);
    host_request_command[command_length] = '\0';
    memcpy(host_continuation, continuation, continuation_length);
    host_continuation_length = continuation_length;
    assert(strlen(continuation_expected_token) <
           sizeof(host_continuation_expected));
    strcpy(host_continuation_expected, continuation_expected_token);
    host_continuation_timeout_ms = continuation_timeout_ms;
    host_continuation_submit_count++;
    return BOOL_TRUE;
}

boolean_en sys_at_engine_cancel_owner(u16 owner_id)
{
    assert(owner_id == SYS_RESOURCE_OWNER_MQTT);
    host_cancel_count++;
    return BOOL_TRUE;
}

boolean_en sys_at_engine_add_urc_handler(
    sys_at_line_handler_fn handler,
    void *context)
{
    assert(handler != NULL);
    assert(context == NULL);
    host_urc_handler = handler;
    host_urc_registration_count++;
    return BOOL_TRUE;
}

void sys_cellular_notify_transport_opened(void)
{
    host_cellular_transport_open_count++;
}

void sys_cellular_get_snapshot(sys_cellular_snapshot_st *snapshot)
{
    assert(snapshot != NULL);
    *snapshot = host_cellular_snapshot;
}

boolean_en sys_event_post(const sys_event_st *event)
{
    assert(event != NULL);
    assert(host_event_count < (u8)(sizeof(host_events) / sizeof(host_events[0])));
    host_events[host_event_count++] = *event;
    return BOOL_TRUE;
}

#include "../Core/System/sys_mqtt.c"

static void host_reset(void)
{
    host_now_ms = 100U;
    memset(&host_normal_request, 0, sizeof(host_normal_request));
    memset(&host_continuation_request, 0, sizeof(host_continuation_request));
    memset(host_request_command, 0, sizeof(host_request_command));
    memset(host_continuation, 0, sizeof(host_continuation));
    memset(host_continuation_expected, 0, sizeof(host_continuation_expected));
    memset(host_events, 0, sizeof(host_events));
    memset(&host_cellular_snapshot, 0, sizeof(host_cellular_snapshot));
    host_cellular_snapshot.at_ready = BOOL_TRUE;
    host_cellular_snapshot.registered = BOOL_TRUE;
    host_cellular_snapshot.pdp_active = BOOL_TRUE;
    host_cellular_snapshot.generation = 1U;
    host_continuation_length = 0U;
    host_continuation_timeout_ms = 0U;
    host_submit_count = 0U;
    host_continuation_submit_count = 0U;
    host_cancel_count = 0U;
    host_urc_registration_count = 0U;
    host_urc_handler = NULL;
    host_event_count = 0U;
    host_cellular_transport_open_count = 0U;
    sys_mqtt_init();
    assert(host_urc_registration_count == 1U);
    assert(host_urc_handler == sys_mqtt_on_urc);
}

static void host_deliver_normal(const char *line, sys_at_result_en result)
{
    sys_at_request_st request;

    request = host_normal_request;
    assert(request.line_handler != NULL);
    assert(request.complete_handler != NULL);
    request.line_handler(
        (const u8 *)line,
        (u16)strlen(line),
        SYS_RESOURCE_OWNER_MQTT,
        request.context);
    request.complete_handler(
        result,
        SYS_RESOURCE_OWNER_MQTT,
        1U,
        request.context);
}

static void host_deliver_puback(u16 packet_id)
{
    sys_at_request_st request;
    char line[48];

    request = host_continuation_request;
    assert(request.line_handler != NULL);
    assert(request.complete_handler != NULL);
    (void)snprintf(
        line,
        sizeof(line),
        "+QMTPUBEX: 0,%u,0",
        (unsigned int)packet_id);
    request.line_handler(
        (const u8 *)line,
        (u16)strlen(line),
        SYS_RESOURCE_OWNER_MQTT,
        request.context);
    request.complete_handler(
        SYS_AT_RESULT_OK,
        SYS_RESOURCE_OWNER_MQTT,
        1U,
        request.context);
}

static u8 host_event_type_count(sys_event_type_en type)
{
    u8 index;
    u8 count;

    count = 0U;
    for (index = 0U; index < host_event_count; index++)
    {
        if (host_events[index].type == type)
        {
            count++;
        }
    }
    return count;
}

static void test_double_subscription_gate(void)
{
    sys_mqtt_snapshot_st snapshot;

    host_reset();
    _snapshot.connected = BOOL_TRUE;
    mqtt_set_state(SYS_MQTT_STATE_SUBSCRIBE_DOWNLINK, host_now_ms, 0U);

    sys_mqtt_process();
    assert(host_submit_count == 1U);
    assert(strstr(host_request_command, "AT+QMTSUB=0,1") != NULL);
    host_deliver_normal("+QMTSUB: 0,1,0", SYS_AT_RESULT_OK);
    sys_mqtt_get_snapshot(&snapshot);
    assert(snapshot.downlink_subscribed == BOOL_TRUE);
    assert(snapshot.upgrade_subscribed == BOOL_FALSE);
    assert(snapshot.ready == BOOL_FALSE);
    assert(snapshot.state == SYS_MQTT_STATE_SUBSCRIBE_UPGRADE);

    sys_mqtt_process();
    assert(host_submit_count == 2U);
    assert(strstr(host_request_command, "AT+QMTSUB=0,2") != NULL);
    host_deliver_normal("+QMTSUB: 0,2,0", SYS_AT_RESULT_OK);
    sys_mqtt_get_snapshot(&snapshot);
    assert(snapshot.downlink_subscribed == BOOL_TRUE);
    assert(snapshot.upgrade_subscribed == BOOL_TRUE);
    assert(snapshot.ready == BOOL_TRUE);
    assert(snapshot.state == SYS_MQTT_STATE_READY);
    assert(host_event_type_count(SYS_EVENT_MQTT_SUBSCRIBED) == 1U);
}

static u16 host_begin_publish(
    const u8 *payload,
    u16 payload_length,
    u16 source_id,
    u32 request_id)
{
    sys_mqtt_snapshot_st snapshot;

    _snapshot.ready = BOOL_TRUE;
    mqtt_set_state(SYS_MQTT_STATE_READY, host_now_ms, 0U);
    assert(sys_mqtt_publish(
               "UP-DEVICE",
               payload,
               payload_length,
               3U,
               source_id,
               request_id) == BOOL_TRUE);
    sys_mqtt_process();
    sys_mqtt_get_snapshot(&snapshot);
    assert(snapshot.state == SYS_MQTT_STATE_PUBLISH_HEADER);
    assert(snapshot.active_source_id == source_id);
    assert(snapshot.active_request_id == request_id);
    assert(snapshot.active_packet_id != 0U);
    sys_mqtt_process();
    assert(host_continuation_length == payload_length);
    assert(memcmp(host_continuation, payload, payload_length) == 0);
    assert(strcmp(host_continuation_expected, "+QMTPUBEX:") == 0);
    assert(host_continuation_timeout_ms == NETWORK_MQTT_PUBACK_TIMEOUT_MS);
    assert(host_continuation_request.expect_prompt == BOOL_TRUE);
    assert(strstr(host_request_command, "AT+QMTPUBEX=0,") != NULL);
    return snapshot.active_packet_id;
}

static void host_assert_publish_result(
    u16 source_id,
    u32 request_id,
    u16 packet_id,
    u16 session_generation)
{
    sys_mqtt_publish_result_st result;

    assert(sys_mqtt_get_publish_result(&result) == BOOL_TRUE);
    assert(result.result == SYS_MQTT_RESULT_SUCCESS);
    assert(result.source_id == source_id);
    assert(result.request_id == request_id);
    assert(result.packet_id == packet_id);
    assert(result.session_generation == session_generation);
    assert(sys_mqtt_get_publish_result(&result) == BOOL_FALSE);
}

static void test_publish_continuation_binding_and_puback_matching(void)
{
    u8 payload_410[410];
    u8 payload_600[600];
    u16 packet_id;
    u16 session_generation;
    u16 index;
    sys_mqtt_publish_result_st result;

    host_reset();
    for (index = 0U; index < (u16)sizeof(payload_410); index++)
    {
        payload_410[index] = (u8)((index % 251U) + 1U);
    }
    for (index = 0U; index < (u16)sizeof(payload_600); index++)
    {
        payload_600[index] = (u8)((index % 239U) + 1U);
    }
    session_generation = _snapshot.session_generation;

    packet_id = host_begin_publish(
        payload_410,
        (u16)sizeof(payload_410),
        (u16)0x1111U,
        (u32)0xABCDEF01UL);
    assert(host_continuation_submit_count == 1U);

    host_deliver_puback((u16)(packet_id + 1U));
    assert(sys_mqtt_get_publish_result(&result) == BOOL_FALSE);
    assert(_snapshot.state == SYS_MQTT_STATE_PUBLISH_HEADER);
    assert(_snapshot.active_packet_id == packet_id);

    host_now_ms += NETWORK_MQTT_PUBLISH_RETRY_DELAY_MS;
    sys_mqtt_process();
    assert(host_continuation_submit_count == 2U);
    assert(host_continuation_length == sizeof(payload_410));
    assert(memcmp(host_continuation, payload_410, sizeof(payload_410)) == 0);
    host_deliver_puback(packet_id);
    host_assert_publish_result(
        (u16)0x1111U,
        (u32)0xABCDEF01UL,
        packet_id,
        session_generation);

    packet_id = host_begin_publish(
        payload_600,
        (u16)sizeof(payload_600),
        (u16)0x2222U,
        (u32)0x12345678UL);
    assert(host_continuation_submit_count == 3U);
    assert(host_continuation_length == sizeof(payload_600));
    assert(memcmp(host_continuation, payload_600, sizeof(payload_600)) == 0);
    host_deliver_puback(packet_id);
    host_assert_publish_result(
        (u16)0x2222U,
        (u32)0x12345678UL,
        packet_id,
        session_generation);
    assert(host_event_type_count(SYS_EVENT_MQTT_PUBACK) == 2U);
}

static void test_qmtrecv_deep_copy_with_escaped_quote_once(void)
{
    char line[] =
        "+QMTRECV: 0,0,\"DL-DEVICE\",8,\"say \\\"hi\\\"\"";
    sys_mqtt_message_st message;
    u16 session_generation;

    host_reset();
    session_generation = _snapshot.session_generation;
    sys_mqtt_on_urc(
        (const u8 *)line,
        (u16)strlen(line),
        SYS_RESOURCE_OWNER_MQTT,
        NULL);
    memset(line, 'X', sizeof(line) - 1U);

    assert(sys_mqtt_get_message(&message) == BOOL_TRUE);
    assert(strcmp(message.topic, "DL-DEVICE") == 0);
    assert(message.payload_length == 8U);
    assert(memcmp(message.payload, "say \"hi\"", 8U) == 0);
    assert(message.payload[8] == '\0');
    assert(message.session_generation == session_generation);
    assert(sys_mqtt_get_message(&message) == BOOL_FALSE);
    assert(host_event_type_count(SYS_EVENT_MQTT_MESSAGE) == 1U);
}

static void test_qmtrecv_unquoted_explicit_length(void)
{
    static const char line[] =
        "+QMTRECV: 0,0,\"MS/dev/plt2dev\",13,{\"ok\":true}\\n";
    sys_mqtt_message_st message;

    host_reset();
    sys_mqtt_on_urc(
        (const u8 *)line,
        (u16)strlen(line),
        SYS_RESOURCE_OWNER_MQTT,
        NULL);
    assert(sys_mqtt_get_message(&message) == BOOL_TRUE);
    assert(strcmp(message.topic, "MS/dev/plt2dev") == 0);
    assert(message.payload_length == 13U);
    assert(memcmp(message.payload, "{\"ok\":true}\\n", 13U) == 0);
    assert(sys_mqtt_get_message(&message) == BOOL_FALSE);
}

static void test_qmtstat_enters_recovery(void)
{
    u16 session_generation;
    const char *line = "+QMTSTAT: 0,1";

    host_reset();
    _snapshot.opened = BOOL_TRUE;
    _snapshot.connected = BOOL_TRUE;
    _snapshot.downlink_subscribed = BOOL_TRUE;
    _snapshot.upgrade_subscribed = BOOL_TRUE;
    _snapshot.ready = BOOL_TRUE;
    mqtt_set_state(SYS_MQTT_STATE_READY, host_now_ms, 0U);
    session_generation = _snapshot.session_generation;

    sys_mqtt_on_urc(
        (const u8 *)line,
        (u16)strlen(line),
        SYS_RESOURCE_OWNER_MQTT,
        NULL);
    assert(_snapshot.opened == BOOL_FALSE);
    assert(_snapshot.connected == BOOL_FALSE);
    assert(_snapshot.downlink_subscribed == BOOL_FALSE);
    assert(_snapshot.upgrade_subscribed == BOOL_FALSE);
    assert(_snapshot.ready == BOOL_FALSE);
    assert(_snapshot.state == SYS_MQTT_STATE_DISCONNECTING);
    assert(_snapshot.session_generation == (u16)(session_generation + 1U));
    assert(host_cancel_count == 1U);
    assert(host_event_type_count(SYS_EVENT_MQTT_DISCONNECTED) == 1U);
}

int main(void)
{
    test_double_subscription_gate();
    test_publish_continuation_binding_and_puback_matching();
    test_qmtrecv_deep_copy_with_escaped_quote_once();
    test_qmtrecv_unquoted_explicit_length();
    test_qmtstat_enters_recovery();
    puts("phase3 MQTT production-C harness: PASS");
    return 0;
}
