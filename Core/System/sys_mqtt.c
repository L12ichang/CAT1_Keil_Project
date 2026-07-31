/*************************************************************
程序功能：EC800E MQTT传输、订阅、发布确认和消息值拷贝
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.7.31
*************************************************************/
#include "sys_mqtt.h"
#include "sys_at_engine.h"
#include "sys_cellular.h"
#include "sys_event.h"
#include "sys_time.h"

typedef enum
{
    MQTT_REQUEST_NONE = 0,
    MQTT_REQUEST_CONFIG_RECV,
    MQTT_REQUEST_CONFIG_VERSION,
    MQTT_REQUEST_CONFIG_KEEPALIVE,
    MQTT_REQUEST_CONFIG_SESSION,
    MQTT_REQUEST_CONFIG_TIMEOUT,
    MQTT_REQUEST_CONFIG_WILL,
    MQTT_REQUEST_OPEN,
    MQTT_REQUEST_CONNECT,
    MQTT_REQUEST_SUB_DOWNLINK,
    MQTT_REQUEST_SUB_UPGRADE,
    MQTT_REQUEST_PUBLISH_HEADER,
    MQTT_REQUEST_PUBLISH_PAYLOAD,
    MQTT_REQUEST_DISCONNECT,
    MQTT_REQUEST_CLOSE
} mqtt_request_en;

typedef struct
{
    char topic[NETWORK_MQTT_TOPIC_CAPACITY];
    u8 payload[NETWORK_MQTT_PAYLOAD_CAPACITY];
    u16 payload_length;
    u8 priority;
    u16 source_id;
    u32 request_id;
    u16 packet_id;
    u16 session_generation;
    u8 retry_count;
} mqtt_publish_slot_st;

static sys_mqtt_snapshot_st _snapshot;
static sys_mqtt_config_st _config;
static mqtt_publish_slot_st _publish_queue[
    NETWORK_MQTT_PUBLISH_QUEUE_CAPACITY];
static u8 _publish_queue_count;
static mqtt_publish_slot_st _active_publish;
static boolean_en _active_publish_valid;
static sys_mqtt_message_st _message_queue[
    NETWORK_MQTT_MESSAGE_QUEUE_CAPACITY];
static u8 _message_queue_count;
static sys_mqtt_publish_result_st _result_queue[
    NETWORK_MQTT_RESULT_QUEUE_CAPACITY];
static u8 _result_queue_count;
static mqtt_request_en _request;
static boolean_en _request_pending;
static boolean_en _cancelling_request;
static boolean_en _request_response_valid;
static boolean_en _request_response_success;
static u16 _request_response_packet_id;
static u32 _next_action_ms;
static char _command[256];
static u16 _cellular_generation;

static boolean_en mqtt_time_due(u32 now_ms, u32 deadline_ms)
{
    return (((s32)(now_ms - deadline_ms)) >= 0) ? BOOL_TRUE : BOOL_FALSE;
}

static void mqtt_set_state(
    sys_mqtt_state_en state,
    u32 now_ms,
    u32 delay_ms)
{
    _snapshot.state = state;
    _snapshot.state_since_ms = now_ms;
    _next_action_ms = now_ms + delay_ms;
}

static void mqtt_post_event(
    sys_event_type_en type,
    u16 data_id,
    u32 value)
{
    sys_event_st event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.source_id = SYS_RESOURCE_OWNER_MQTT;
    event.data_id = data_id;
    event.timestamp_ms = sys_time_get_ms();
    event.value = value;
    (void)sys_event_post(&event);
}

static boolean_en mqtt_line_starts_with(
    const u8 *line,
    u16 length,
    const char *prefix)
{
    u16 prefix_length;

    prefix_length = (u16)strlen(prefix);
    if ((line == NULL) || (length < prefix_length))
    {
        return BOOL_FALSE;
    }
    return (memcmp(line, prefix, prefix_length) == 0) ?
        BOOL_TRUE : BOOL_FALSE;
}

static boolean_en mqtt_parse_three_values(
    const u8 *line,
    const char *prefix,
    unsigned int *first,
    unsigned int *second,
    int *third)
{
    const char *text;

    text = (const char *)line;
    if ((text == NULL) || (strstr(text, prefix) == NULL))
    {
        return BOOL_FALSE;
    }
    text = strstr(text, prefix);
    return (sscanf(
        text + strlen(prefix),
        " %u,%u,%d",
        first,
        second,
        third) == 3) ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en mqtt_parse_two_values(
    const u8 *line,
    const char *prefix,
    unsigned int *first,
    int *second)
{
    const char *text;

    text = (const char *)line;
    if ((text == NULL) || (strstr(text, prefix) == NULL))
    {
        return BOOL_FALSE;
    }
    text = strstr(text, prefix);
    return (sscanf(
        text + strlen(prefix),
        " %u,%d",
        first,
        second) == 2) ? BOOL_TRUE : BOOL_FALSE;
}

static void mqtt_queue_result(sys_mqtt_result_en result)
{
    sys_mqtt_publish_result_st item;

    if (_active_publish_valid != BOOL_TRUE)
    {
        return;
    }
    memset(&item, 0, sizeof(item));
    item.result = result;
    item.source_id = _active_publish.source_id;
    item.request_id = _active_publish.request_id;
    item.packet_id = _active_publish.packet_id;
    item.session_generation = _active_publish.session_generation;
    item.timestamp_ms = sys_time_get_ms();
    if (_result_queue_count >= NETWORK_MQTT_RESULT_QUEUE_CAPACITY)
    {
        memmove(
            &_result_queue[0],
            &_result_queue[1],
            sizeof(_result_queue[0]) *
                (NETWORK_MQTT_RESULT_QUEUE_CAPACITY - 1U));
        _result_queue_count--;
    }
    _result_queue[_result_queue_count++] = item;
    _snapshot.queued_result_count = _result_queue_count;

    if (result == SYS_MQTT_RESULT_SUCCESS)
    {
        _snapshot.publish_success_count++;
        _snapshot.last_puback_ms = item.timestamp_ms;
        mqtt_post_event(
            SYS_EVENT_MQTT_PUBACK,
            item.packet_id,
            item.request_id);
    }
    else
    {
        _snapshot.publish_fail_count++;
        if (result == SYS_MQTT_RESULT_TIMEOUT)
        {
            _snapshot.publish_timeout_count++;
        }
        mqtt_post_event(
            SYS_EVENT_MQTT_PUBLISH_FAILED,
            item.packet_id,
            (u32)result);
    }
    memset(&_active_publish, 0, sizeof(_active_publish));
    _active_publish_valid = BOOL_FALSE;
    _snapshot.active_packet_id = 0U;
    _snapshot.active_source_id = 0U;
    _snapshot.active_request_id = 0U;
}

static void mqtt_mark_link_lost(u32 reason)
{
    u32 now_ms;

    now_ms = sys_time_get_ms();
    if (_active_publish_valid == BOOL_TRUE)
    {
        mqtt_queue_result(SYS_MQTT_RESULT_LINK_LOST);
    }
    _request = MQTT_REQUEST_NONE;
    _request_pending = BOOL_FALSE;
    _cancelling_request = BOOL_TRUE;
    (void)sys_at_engine_cancel_owner(SYS_RESOURCE_OWNER_MQTT);
    _cancelling_request = BOOL_FALSE;
    _snapshot.opened = BOOL_FALSE;
    _snapshot.connected = BOOL_FALSE;
    _snapshot.downlink_subscribed = BOOL_FALSE;
    _snapshot.upgrade_subscribed = BOOL_FALSE;
    _snapshot.ready = BOOL_FALSE;
    _snapshot.session_generation++;
    mqtt_post_event(SYS_EVENT_MQTT_DISCONNECTED, 0U, reason);
    mqtt_set_state(SYS_MQTT_STATE_DISCONNECTING, now_ms, 0U);
    if (_snapshot.recovery_count < 0xFFU)
    {
        _snapshot.recovery_count++;
    }
}

static void mqtt_suspend_for_network_loss(u32 reason)
{
    u32 now_ms;

    now_ms = sys_time_get_ms();
    if (_active_publish_valid == BOOL_TRUE)
    {
        mqtt_queue_result(SYS_MQTT_RESULT_LINK_LOST);
    }
    _request = MQTT_REQUEST_NONE;
    _request_pending = BOOL_FALSE;
    _cancelling_request = BOOL_TRUE;
    (void)sys_at_engine_cancel_owner(SYS_RESOURCE_OWNER_MQTT);
    _cancelling_request = BOOL_FALSE;
    _snapshot.opened = BOOL_FALSE;
    _snapshot.connected = BOOL_FALSE;
    _snapshot.downlink_subscribed = BOOL_FALSE;
    _snapshot.upgrade_subscribed = BOOL_FALSE;
    _snapshot.ready = BOOL_FALSE;
    _snapshot.session_generation++;
    mqtt_post_event(SYS_EVENT_MQTT_DISCONNECTED, 0U, reason);
    mqtt_set_state(SYS_MQTT_STATE_WAIT_NETWORK, now_ms, 0U);
}

static void mqtt_close_completed(void)
{
    u32 now_ms;

    now_ms = sys_time_get_ms();
    mqtt_set_state(
        SYS_MQTT_STATE_RECOVERY_WAIT,
        now_ms,
        (_snapshot.recovery_count <= 1U) ?
            NETWORK_MQTT_RECOVERY_BACKOFF_FIRST_MS :
            NETWORK_MQTT_RECOVERY_BACKOFF_NEXT_MS);
}

static void mqtt_line_handler(
    const u8 *line,
    u16 length,
    u16 owner_id,
    void *context)
{
    unsigned int first;
    unsigned int second;
    int third;

    (void)owner_id;
    (void)context;
    if ((_request == MQTT_REQUEST_OPEN) &&
        (mqtt_parse_three_values(
            line,
            "+QMTOPEN:",
            &first,
            &second,
            &third) == BOOL_FALSE))
    {
        if ((sscanf((const char *)line,
                    "+QMTOPEN: %u,%d",
                    &first,
                    &third) == 2) &&
            (first == NETWORK_MQTT_CLIENT_INDEX))
        {
            _request_response_valid = BOOL_TRUE;
            _request_response_success = (third == 0) ?
                BOOL_TRUE : BOOL_FALSE;
        }
    }
    else if (_request == MQTT_REQUEST_CONNECT)
    {
        if ((mqtt_parse_three_values(
                line,
                "+QMTCONN:",
                &first,
                &second,
                &third) == BOOL_TRUE) &&
            (first == NETWORK_MQTT_CLIENT_INDEX))
        {
            _request_response_valid = BOOL_TRUE;
            _request_response_success =
                ((second == 0U) && (third == 0)) ?
                    BOOL_TRUE : BOOL_FALSE;
        }
    }
    else if ((_request == MQTT_REQUEST_SUB_DOWNLINK) ||
             (_request == MQTT_REQUEST_SUB_UPGRADE))
    {
        if ((mqtt_parse_three_values(
                line,
                "+QMTSUB:",
                &first,
                &second,
                &third) == BOOL_TRUE) &&
            (first == NETWORK_MQTT_CLIENT_INDEX))
        {
            _request_response_valid = BOOL_TRUE;
            _request_response_packet_id = (u16)second;
            _request_response_success = (third == 0) ?
                BOOL_TRUE : BOOL_FALSE;
        }
    }
    else if (_request == MQTT_REQUEST_PUBLISH_PAYLOAD)
    {
        if ((mqtt_parse_three_values(
                line,
                "+QMTPUBEX:",
                &first,
                &second,
                &third) == BOOL_TRUE) &&
            (first == NETWORK_MQTT_CLIENT_INDEX))
        {
            _request_response_valid = BOOL_TRUE;
            _request_response_packet_id = (u16)second;
            _request_response_success =
                ((_active_publish_valid == BOOL_TRUE) &&
                 ((u16)second == _active_publish.packet_id) &&
                 (third == 0)) ? BOOL_TRUE : BOOL_FALSE;
        }
    }
    else if ((_request == MQTT_REQUEST_DISCONNECT) ||
             (_request == MQTT_REQUEST_CLOSE))
    {
        const char *prefix;

        prefix = (_request == MQTT_REQUEST_DISCONNECT) ?
            "+QMTDISC:" : "+QMTCLOSE:";
        if ((mqtt_parse_two_values(
                line,
                prefix,
                &first,
                &third) == BOOL_TRUE) &&
            (first == NETWORK_MQTT_CLIENT_INDEX))
        {
            _request_response_valid = BOOL_TRUE;
            _request_response_success = (third == 0) ?
                BOOL_TRUE : BOOL_FALSE;
        }
    }
    (void)length;
}

static void mqtt_complete_handler(
    sys_at_result_en result,
    u16 owner_id,
    u16 generation,
    void *context)
{
    mqtt_request_en completed_request;
    u32 now_ms;

    (void)owner_id;
    (void)generation;
    (void)context;
    completed_request = _request;
    _request = MQTT_REQUEST_NONE;
    _request_pending = BOOL_FALSE;
    now_ms = sys_time_get_ms();
    if ((_snapshot.enabled != BOOL_TRUE) ||
        (_cancelling_request == BOOL_TRUE))
    {
        return;
    }

    if ((result != SYS_AT_RESULT_OK) ||
        (((completed_request == MQTT_REQUEST_OPEN) ||
          (completed_request == MQTT_REQUEST_CONNECT) ||
          (completed_request == MQTT_REQUEST_SUB_DOWNLINK) ||
          (completed_request == MQTT_REQUEST_SUB_UPGRADE) ||
          (completed_request == MQTT_REQUEST_PUBLISH_PAYLOAD) ||
          (completed_request == MQTT_REQUEST_DISCONNECT) ||
          (completed_request == MQTT_REQUEST_CLOSE)) &&
         ((_request_response_valid != BOOL_TRUE) ||
          (_request_response_success != BOOL_TRUE))))
    {
        if (completed_request == MQTT_REQUEST_DISCONNECT)
        {
            mqtt_set_state(SYS_MQTT_STATE_CLOSING, now_ms, 0U);
            return;
        }
        if (completed_request == MQTT_REQUEST_CLOSE)
        {
            mqtt_close_completed();
            return;
        }
        if ((completed_request == MQTT_REQUEST_PUBLISH_HEADER) ||
            (completed_request == MQTT_REQUEST_PUBLISH_PAYLOAD))
        {
            if ((_active_publish_valid == BOOL_TRUE) &&
                (_active_publish.retry_count == 0U))
            {
                _active_publish.retry_count = 1U;
                mqtt_set_state(
                    SYS_MQTT_STATE_PUBLISH_HEADER,
                    now_ms,
                    NETWORK_MQTT_PUBLISH_RETRY_DELAY_MS);
                return;
            }
            mqtt_queue_result(
                (result == SYS_AT_RESULT_TIMEOUT) ?
                    SYS_MQTT_RESULT_TIMEOUT :
                    SYS_MQTT_RESULT_REJECTED);
        }
        mqtt_mark_link_lost((u32)result);
        return;
    }

    switch (completed_request)
    {
        case MQTT_REQUEST_CONFIG_RECV:
            mqtt_set_state(
                SYS_MQTT_STATE_CONFIG_VERSION,
                now_ms,
                0U);
            break;
        case MQTT_REQUEST_CONFIG_VERSION:
            mqtt_set_state(
                SYS_MQTT_STATE_CONFIG_KEEPALIVE,
                now_ms,
                0U);
            break;
        case MQTT_REQUEST_CONFIG_KEEPALIVE:
            mqtt_set_state(
                SYS_MQTT_STATE_CONFIG_SESSION,
                now_ms,
                0U);
            break;
        case MQTT_REQUEST_CONFIG_SESSION:
            mqtt_set_state(
                SYS_MQTT_STATE_CONFIG_TIMEOUT,
                now_ms,
                0U);
            break;
        case MQTT_REQUEST_CONFIG_TIMEOUT:
            mqtt_set_state(
                SYS_MQTT_STATE_CONFIG_WILL,
                now_ms,
                0U);
            break;
        case MQTT_REQUEST_CONFIG_WILL:
            mqtt_set_state(SYS_MQTT_STATE_OPENING, now_ms, 0U);
            break;
        case MQTT_REQUEST_OPEN:
            _snapshot.opened = BOOL_TRUE;
            sys_cellular_notify_transport_opened();
            mqtt_set_state(SYS_MQTT_STATE_CONNECTING, now_ms, 0U);
            break;
        case MQTT_REQUEST_CONNECT:
            _snapshot.connected = BOOL_TRUE;
            mqtt_post_event(SYS_EVENT_MQTT_CONNECTED, 0U, 0U);
            mqtt_set_state(
                SYS_MQTT_STATE_SUBSCRIBE_DOWNLINK,
                now_ms,
                0U);
            break;
        case MQTT_REQUEST_SUB_DOWNLINK:
            if (_request_response_packet_id != 1U)
            {
                mqtt_mark_link_lost(1U);
                break;
            }
            _snapshot.downlink_subscribed = BOOL_TRUE;
            mqtt_set_state(
                SYS_MQTT_STATE_SUBSCRIBE_UPGRADE,
                now_ms,
                0U);
            break;
        case MQTT_REQUEST_SUB_UPGRADE:
            if (_request_response_packet_id != 2U)
            {
                mqtt_mark_link_lost(2U);
                break;
            }
            _snapshot.upgrade_subscribed = BOOL_TRUE;
            _snapshot.ready = BOOL_TRUE;
            _snapshot.recovery_count = 0U;
            _snapshot.last_status_code = 0;
            mqtt_post_event(SYS_EVENT_MQTT_SUBSCRIBED, 0U, 3U);
            mqtt_set_state(SYS_MQTT_STATE_READY, now_ms, 0U);
            break;
        case MQTT_REQUEST_PUBLISH_HEADER:
            mqtt_set_state(
                SYS_MQTT_STATE_PUBLISH_PAYLOAD,
                now_ms,
                0U);
            break;
        case MQTT_REQUEST_PUBLISH_PAYLOAD:
            mqtt_queue_result(SYS_MQTT_RESULT_SUCCESS);
            mqtt_set_state(SYS_MQTT_STATE_READY, now_ms, 0U);
            break;
        case MQTT_REQUEST_DISCONNECT:
            mqtt_set_state(SYS_MQTT_STATE_CLOSING, now_ms, 0U);
            break;
        case MQTT_REQUEST_CLOSE:
            mqtt_close_completed();
            break;
        default:
            break;
    }
}

static boolean_en mqtt_submit(
    mqtt_request_en request_kind,
    const char *command,
    u16 command_length,
    const char *expected,
    u32 timeout_ms,
    u8 priority,
    boolean_en expect_prompt)
{
    sys_at_request_st request;

    if (_request_pending == BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    memset(&request, 0, sizeof(request));
    request.command = command;
    request.command_length = command_length;
    request.expected_token = expected;
    request.error_token = "ERROR";
    request.timeout_ms = timeout_ms;
    request.retry_max = 0U;
    request.priority = priority;
    request.owner_id = SYS_RESOURCE_OWNER_MQTT;
    request.expect_prompt = expect_prompt;
    request.line_handler = mqtt_line_handler;
    request.complete_handler = mqtt_complete_handler;

    _request_response_valid = BOOL_FALSE;
    _request_response_success = BOOL_FALSE;
    _request_response_packet_id = 0U;
    _request = request_kind;
    if (sys_at_engine_submit(&request) != BOOL_TRUE)
    {
        _request = MQTT_REQUEST_NONE;
        _next_action_ms = sys_time_get_ms() +
            NETWORK_MQTT_PUBLISH_RETRY_DELAY_MS;
        return BOOL_FALSE;
    }
    _request_pending = BOOL_TRUE;
    return BOOL_TRUE;
}

static boolean_en mqtt_submit_publish_transaction(void)
{
    sys_at_request_st request;
    int command_length;

    command_length = snprintf(
        _command,
        sizeof(_command),
        "AT+QMTPUBEX=0,%u,1,0,\"%s\",%u\r\n",
        (unsigned int)_active_publish.packet_id,
        _active_publish.topic,
        (unsigned int)_active_publish.payload_length);
    if ((command_length <= 0) ||
        (command_length >= (int)sizeof(_command)))
    {
        mqtt_queue_result(SYS_MQTT_RESULT_REJECTED);
        mqtt_mark_link_lost(0U);
        return BOOL_FALSE;
    }

    memset(&request, 0, sizeof(request));
    request.command = _command;
    request.command_length = (u16)command_length;
    request.expected_token = ">";
    request.error_token = "ERROR";
    request.timeout_ms = NETWORK_MQTT_PROMPT_TIMEOUT_MS;
    request.retry_max = 0U;
    request.priority = 0U;
    request.owner_id = SYS_RESOURCE_OWNER_MQTT;
    request.expect_prompt = BOOL_TRUE;
    request.line_handler = mqtt_line_handler;
    request.complete_handler = mqtt_complete_handler;
    _request_response_valid = BOOL_FALSE;
    _request_response_success = BOOL_FALSE;
    _request_response_packet_id = 0U;
    _request = MQTT_REQUEST_PUBLISH_PAYLOAD;
    if (sys_at_engine_submit_continuation(
            &request,
            _active_publish.payload,
            _active_publish.payload_length,
            "+QMTPUBEX:",
            NETWORK_MQTT_PUBACK_TIMEOUT_MS) != BOOL_TRUE)
    {
        _request = MQTT_REQUEST_NONE;
        _next_action_ms = sys_time_get_ms() +
            NETWORK_MQTT_PUBLISH_RETRY_DELAY_MS;
        return BOOL_FALSE;
    }
    _request_pending = BOOL_TRUE;
    mqtt_set_state(
        SYS_MQTT_STATE_PUBLISH_PAYLOAD,
        sys_time_get_ms(),
        0U);
    return BOOL_TRUE;
}

static boolean_en mqtt_submit_will_transaction(void)
{
    sys_at_request_st request;
    int command_length;

    command_length = snprintf(
        _command,
        sizeof(_command),
        "AT+QMTCFG=\"willex\",0,1,1,0,\"%s\",%u\r\n",
        _config.will_topic,
        (unsigned int)_config.will_payload_length);
    if ((command_length <= 0) ||
        (command_length >= (int)sizeof(_command)))
    {
        mqtt_mark_link_lost(0U);
        return BOOL_FALSE;
    }
    memset(&request, 0, sizeof(request));
    request.command = _command;
    request.command_length = (u16)command_length;
    request.expected_token = ">";
    request.error_token = "ERROR";
    request.timeout_ms = NETWORK_MQTT_PROMPT_TIMEOUT_MS;
    request.retry_max = 0U;
    request.priority = 2U;
    request.owner_id = SYS_RESOURCE_OWNER_MQTT;
    request.expect_prompt = BOOL_TRUE;
    request.line_handler = mqtt_line_handler;
    request.complete_handler = mqtt_complete_handler;
    _request = MQTT_REQUEST_CONFIG_WILL;
    if (sys_at_engine_submit_continuation(
            &request,
            _config.will_payload,
            _config.will_payload_length,
            "OK",
            NETWORK_MQTT_CONFIG_TIMEOUT_MS) != BOOL_TRUE)
    {
        _request = MQTT_REQUEST_NONE;
        _next_action_ms = sys_time_get_ms() +
            NETWORK_MQTT_PUBLISH_RETRY_DELAY_MS;
        return BOOL_FALSE;
    }
    _request_pending = BOOL_TRUE;
    return BOOL_TRUE;
}

static boolean_en mqtt_copy_config_text(
    char *destination,
    u16 capacity,
    const char *source)
{
    u16 length;

    if ((destination == NULL) || (source == NULL))
    {
        return BOOL_FALSE;
    }
    length = (u16)strlen(source);
    if ((length == 0U) || (length >= capacity))
    {
        return BOOL_FALSE;
    }
    memcpy(destination, source, length + 1U);
    return BOOL_TRUE;
}

static boolean_en mqtt_extract_quoted(
    const u8 *line,
    u16 length,
    u16 *index,
    char *destination,
    u16 capacity,
    u16 *copied_length)
{
    u16 output;
    u8 byte;

    while ((*index < length) && (line[*index] != (u8)'\"'))
    {
        (*index)++;
    }
    if (*index >= length)
    {
        return BOOL_FALSE;
    }
    (*index)++;
    output = 0U;
    while ((*index < length) && (line[*index] != (u8)'\"'))
    {
        byte = line[*index];
        if ((byte == (u8)'\\') &&
            ((*index + 1U) < length) &&
            ((line[*index + 1U] == (u8)'\"') ||
             (line[*index + 1U] == (u8)'\\')))
        {
            (*index)++;
            byte = line[*index];
        }
        if ((output + 1U) >= capacity)
        {
            return BOOL_FALSE;
        }
        destination[output++] = (char)byte;
        (*index)++;
    }
    if (*index >= length)
    {
        return BOOL_FALSE;
    }
    destination[output] = '\0';
    *copied_length = output;
    (*index)++;
    return BOOL_TRUE;
}

static void mqtt_copy_received_message(const u8 *line, u16 length)
{
    sys_mqtt_message_st message;
    u16 index;
    u16 topic_length;
    u16 payload_length;
    u16 explicit_length;
    boolean_en explicit_length_seen;

    if (mqtt_line_starts_with(line, length, "+QMTRECV:") != BOOL_TRUE)
    {
        return;
    }
    memset(&message, 0, sizeof(message));
    index = 9U;
    if (mqtt_extract_quoted(
            line,
            length,
            &index,
            message.topic,
            sizeof(message.topic),
            &topic_length) != BOOL_TRUE)
    {
        return;
    }
    explicit_length = 0U;
    explicit_length_seen = BOOL_FALSE;
    while ((index < length) &&
           ((line[index] == (u8)' ') || (line[index] == (u8)',)))
    {
        index++;
    }
    while ((index < length) &&
           (line[index] >= (u8)'0') && (line[index] <= (u8)'9'))
    {
        explicit_length_seen = BOOL_TRUE;
        explicit_length = (u16)((explicit_length * 10U) +
            (u16)(line[index] - (u8)'0'));
        index++;
    }
    while ((index < length) &&
           ((line[index] == (u8)' ') || (line[index] == (u8)',)))
    {
        index++;
    }
    if ((index < length) && (line[index] == (u8)'\"'))
    {
        if (mqtt_extract_quoted(
                line,
                length,
                &index,
                (char *)message.payload,
                sizeof(message.payload),
                &payload_length) != BOOL_TRUE)
        {
            return;
        }
    }
    else
    {
        if ((explicit_length_seen != BOOL_TRUE) ||
            (explicit_length > NETWORK_MQTT_PAYLOAD_CAPACITY) ||
            ((u32)index + (u32)explicit_length > (u32)length))
        {
            return;
        }
        memcpy(message.payload, &line[index], explicit_length);
        message.payload[explicit_length] = '\0';
        payload_length = explicit_length;
    }
    if ((explicit_length_seen == BOOL_TRUE) &&
        (explicit_length != payload_length))
    {
        return;
    }
    (void)topic_length;
    message.payload_length = payload_length;
    message.session_generation = _snapshot.session_generation;
    if (_message_queue_count >= NETWORK_MQTT_MESSAGE_QUEUE_CAPACITY)
    {
        _snapshot.message_drop_count++;
        return;
    }
    _message_queue[_message_queue_count++] = message;
    _snapshot.queued_message_count = _message_queue_count;
    _snapshot.last_message_ms = sys_time_get_ms();
    mqtt_post_event(
        SYS_EVENT_MQTT_MESSAGE,
        _message_queue_count,
        payload_length);
}

void sys_mqtt_init(void)
{
    u32 now_ms;
    sys_cellular_snapshot_st cellular;

    now_ms = sys_time_get_ms();
    memset(&_snapshot, 0, sizeof(_snapshot));
    memset(&_config, 0, sizeof(_config));
    memset(_publish_queue, 0, sizeof(_publish_queue));
    memset(_message_queue, 0, sizeof(_message_queue));
    memset(_result_queue, 0, sizeof(_result_queue));
    _snapshot.enabled = BOOL_TRUE;
    _snapshot.next_packet_id = 1U;
    _snapshot.session_generation = 1U;
    _publish_queue_count = 0U;
    _message_queue_count = 0U;
    _result_queue_count = 0U;
    _active_publish_valid = BOOL_FALSE;
    _request = MQTT_REQUEST_NONE;
    _request_pending = BOOL_FALSE;
    _cancelling_request = BOOL_FALSE;
    sys_cellular_get_snapshot(&cellular);
    _cellular_generation = cellular.generation;
    mqtt_set_state(SYS_MQTT_STATE_WAIT_CONFIG, now_ms, 0U);
    (void)sys_at_engine_add_urc_handler(sys_mqtt_on_urc, NULL);
}

void sys_mqtt_set_enabled(boolean_en enabled)
{
    u32 now_ms;

    now_ms = sys_time_get_ms();
    if (enabled != BOOL_TRUE)
    {
        if (_active_publish_valid == BOOL_TRUE)
        {
            mqtt_queue_result(SYS_MQTT_RESULT_CANCELLED);
        }
        _request = MQTT_REQUEST_NONE;
        _request_pending = BOOL_FALSE;
        _cancelling_request = BOOL_TRUE;
        (void)sys_at_engine_cancel_owner(SYS_RESOURCE_OWNER_MQTT);
        _cancelling_request = BOOL_FALSE;
        _snapshot.enabled = BOOL_FALSE;
        _snapshot.ready = BOOL_FALSE;
        mqtt_set_state(SYS_MQTT_STATE_DISABLED, now_ms, 0U);
    }
    else if (_snapshot.enabled != BOOL_TRUE)
    {
        _snapshot.enabled = BOOL_TRUE;
        _snapshot.opened = BOOL_FALSE;
        _snapshot.connected = BOOL_FALSE;
        _snapshot.downlink_subscribed = BOOL_FALSE;
        _snapshot.upgrade_subscribed = BOOL_FALSE;
        _snapshot.ready = BOOL_FALSE;
        _snapshot.session_generation++;
        mqtt_set_state(
            (_snapshot.configured == BOOL_TRUE) ?
                SYS_MQTT_STATE_WAIT_NETWORK :
                SYS_MQTT_STATE_WAIT_CONFIG,
            now_ms,
            0U);
    }
}

boolean_en sys_mqtt_configure(const sys_mqtt_config_st *config)
{
    if ((config == NULL) ||
        (mqtt_copy_config_text(
            _config.server_host,
            sizeof(_config.server_host),
            config->server_host) != BOOL_TRUE) ||
        (config->server_port == 0U) ||
        (mqtt_copy_config_text(
            _config.client_id,
            sizeof(_config.client_id),
            config->client_id) != BOOL_TRUE) ||
        (mqtt_copy_config_text(
            _config.username,
            sizeof(_config.username),
            config->username) != BOOL_TRUE) ||
        (mqtt_copy_config_text(
            _config.password,
            sizeof(_config.password),
            config->password) != BOOL_TRUE) ||
        (mqtt_copy_config_text(
            _config.downlink_topic,
            sizeof(_config.downlink_topic),
            config->downlink_topic) != BOOL_TRUE) ||
        (mqtt_copy_config_text(
            _config.upgrade_topic,
            sizeof(_config.upgrade_topic),
            config->upgrade_topic) != BOOL_TRUE) ||
        (mqtt_copy_config_text(
            _config.will_topic,
            sizeof(_config.will_topic),
            config->will_topic) != BOOL_TRUE) ||
        (config->will_payload_length == 0U) ||
        (config->will_payload_length >
         NETWORK_MQTT_WILL_PAYLOAD_CAPACITY))
    {
        memset(&_config, 0, sizeof(_config));
        _snapshot.configured = BOOL_FALSE;
        return BOOL_FALSE;
    }
    memcpy(
        _config.will_payload,
        config->will_payload,
        config->will_payload_length);
    _config.will_payload_length = config->will_payload_length;
    _config.server_port = config->server_port;
    _snapshot.configured = BOOL_TRUE;
    if ((_snapshot.enabled == BOOL_TRUE) &&
        (_snapshot.state == SYS_MQTT_STATE_WAIT_CONFIG))
    {
        mqtt_set_state(
            SYS_MQTT_STATE_WAIT_NETWORK,
            sys_time_get_ms(),
            0U);
    }
    return BOOL_TRUE;
}

boolean_en sys_mqtt_publish(
    const char *topic,
    const u8 *payload,
    u16 payload_length,
    u8 priority,
    u16 source_id,
    u32 request_id)
{
    mqtt_publish_slot_st item;
    u8 index;
    u8 victim;

    if ((_snapshot.enabled != BOOL_TRUE) ||
        (_snapshot.ready != BOOL_TRUE) ||
        (topic == NULL) ||
        (payload == NULL) ||
        (payload_length == 0U) ||
        (payload_length > NETWORK_MQTT_PAYLOAD_CAPACITY) ||
        (strlen(topic) >= NETWORK_MQTT_TOPIC_CAPACITY) ||
        (source_id == 0U))
    {
        return BOOL_FALSE;
    }
    memset(&item, 0, sizeof(item));
    strcpy(item.topic, topic);
    memcpy(item.payload, payload, payload_length);
    item.payload_length = payload_length;
    item.priority = priority;
    item.source_id = source_id;
    item.request_id = request_id;
    item.packet_id = _snapshot.next_packet_id++;
    if (_snapshot.next_packet_id == 0U)
    {
        _snapshot.next_packet_id = 1U;
    }
    item.session_generation = _snapshot.session_generation;

    if (_publish_queue_count >= NETWORK_MQTT_PUBLISH_QUEUE_CAPACITY)
    {
        if (priority > 4U)
        {
            return BOOL_FALSE;
        }
        victim = NETWORK_MQTT_PUBLISH_QUEUE_CAPACITY;
        for (index = 0U; index < _publish_queue_count; index++)
        {
            if ((_publish_queue[index].priority > 4U) &&
                ((victim == NETWORK_MQTT_PUBLISH_QUEUE_CAPACITY) ||
                 (_publish_queue[index].priority >
                  _publish_queue[victim].priority)))
            {
                victim = index;
            }
        }
        if (victim == NETWORK_MQTT_PUBLISH_QUEUE_CAPACITY)
        {
            return BOOL_FALSE;
        }
        memmove(
            &_publish_queue[victim],
            &_publish_queue[victim + 1U],
            sizeof(_publish_queue[0]) *
                (_publish_queue_count - victim - 1U));
        _publish_queue_count--;
    }

    index = _publish_queue_count;
    while ((index > 0U) &&
           (_publish_queue[index - 1U].priority > item.priority))
    {
        _publish_queue[index] = _publish_queue[index - 1U];
        index--;
    }
    _publish_queue[index] = item;
    _publish_queue_count++;
    _snapshot.queued_publish_count = _publish_queue_count;
    return BOOL_TRUE;
}

boolean_en sys_mqtt_get_message(sys_mqtt_message_st *message)
{
    if ((message == NULL) || (_message_queue_count == 0U))
    {
        return BOOL_FALSE;
    }
    *message = _message_queue[0];
    memmove(
        &_message_queue[0],
        &_message_queue[1],
        sizeof(_message_queue[0]) * (_message_queue_count - 1U));
    _message_queue_count--;
    _snapshot.queued_message_count = _message_queue_count;
    return BOOL_TRUE;
}

boolean_en sys_mqtt_get_publish_result(
    sys_mqtt_publish_result_st *result)
{
    if ((result == NULL) || (_result_queue_count == 0U))
    {
        return BOOL_FALSE;
    }
    *result = _result_queue[0];
    memmove(
        &_result_queue[0],
        &_result_queue[1],
        sizeof(_result_queue[0]) * (_result_queue_count - 1U));
    _result_queue_count--;
    _snapshot.queued_result_count = _result_queue_count;
    return BOOL_TRUE;
}

void sys_mqtt_request_recovery(void)
{
    if (_snapshot.enabled == BOOL_TRUE)
    {
        mqtt_mark_link_lost(0U);
    }
}

void sys_mqtt_get_snapshot(sys_mqtt_snapshot_st *snapshot)
{
    if (snapshot != NULL)
    {
        *snapshot = _snapshot;
    }
}

void sys_mqtt_on_urc(
    const u8 *line,
    u16 length,
    u16 owner_id,
    void *context)
{
    (void)owner_id;
    (void)context;
    if ((_snapshot.enabled != BOOL_TRUE) || (line == NULL))
    {
        return;
    }
    if (mqtt_line_starts_with(line, length, "+QMTRECV:") == BOOL_TRUE)
    {
        mqtt_copy_received_message(line, length);
    }
    else if (mqtt_line_starts_with(line, length, "+QMTSTAT:") == BOOL_TRUE)
    {
        unsigned int client_index;
        int status_code;

        status_code = -1;
        if ((mqtt_parse_two_values(
                line,
                "+QMTSTAT:",
                &client_index,
                &status_code) == BOOL_TRUE) &&
            (client_index == NETWORK_MQTT_CLIENT_INDEX))
        {
            _snapshot.last_status_code = (s16)status_code;
        }
        mqtt_mark_link_lost((u32)status_code);
    }
}

void sys_mqtt_process(void)
{
    sys_cellular_snapshot_st cellular;
    u32 now_ms;
    int command_length;

    now_ms = sys_time_get_ms();
    if (_snapshot.enabled != BOOL_TRUE)
    {
        return;
    }
    sys_cellular_get_snapshot(&cellular);
    if ((_snapshot.state != SYS_MQTT_STATE_WAIT_CONFIG) &&
        (_snapshot.state != SYS_MQTT_STATE_WAIT_NETWORK) &&
        ((_cellular_generation != cellular.generation) ||
         (cellular.at_ready != BOOL_TRUE) ||
         (cellular.registered != BOOL_TRUE) ||
         ((_snapshot.opened == BOOL_TRUE) &&
          (cellular.pdp_active != BOOL_TRUE))))
    {
        _cellular_generation = cellular.generation;
        mqtt_suspend_for_network_loss(
            (u32)cellular.recovery_reason);
        return;
    }
    _cellular_generation = cellular.generation;
    if ((_request_pending == BOOL_TRUE) ||
        (mqtt_time_due(now_ms, _next_action_ms) != BOOL_TRUE))
    {
        return;
    }

    switch (_snapshot.state)
    {
        case SYS_MQTT_STATE_WAIT_CONFIG:
            break;

        case SYS_MQTT_STATE_WAIT_NETWORK:
            if ((cellular.at_ready == BOOL_TRUE) &&
                (cellular.registered == BOOL_TRUE))
            {
                mqtt_set_state(SYS_MQTT_STATE_CONFIG_RECV, now_ms, 0U);
            }
            break;

        case SYS_MQTT_STATE_CONFIG_RECV:
            (void)mqtt_submit(
                MQTT_REQUEST_CONFIG_RECV,
                "AT+QMTCFG=\"recv/mode\",0,0,0\r\n",
                0U,
                "OK",
                NETWORK_MQTT_CONFIG_TIMEOUT_MS,
                3U,
                BOOL_FALSE);
            break;

        case SYS_MQTT_STATE_CONFIG_VERSION:
            (void)mqtt_submit(
                MQTT_REQUEST_CONFIG_VERSION,
                "AT+QMTCFG=\"version\",0,4\r\n",
                0U,
                "OK",
                NETWORK_MQTT_CONFIG_TIMEOUT_MS,
                3U,
                BOOL_FALSE);
            break;

        case SYS_MQTT_STATE_CONFIG_KEEPALIVE:
            command_length = snprintf(
                _command,
                sizeof(_command),
                "AT+QMTCFG=\"keepalive\",0,%u\r\n",
                (unsigned int)NETWORK_MQTT_KEEPALIVE_SEC);
            if ((command_length > 0) &&
                (command_length < (int)sizeof(_command)))
            {
                (void)mqtt_submit(
                    MQTT_REQUEST_CONFIG_KEEPALIVE,
                    _command,
                    (u16)command_length,
                    "OK",
                    NETWORK_MQTT_CONFIG_TIMEOUT_MS,
                    3U,
                    BOOL_FALSE);
            }
            break;

        case SYS_MQTT_STATE_CONFIG_SESSION:
            (void)mqtt_submit(
                MQTT_REQUEST_CONFIG_SESSION,
                "AT+QMTCFG=\"session\",0,1\r\n",
                0U,
                "OK",
                NETWORK_MQTT_CONFIG_TIMEOUT_MS,
                3U,
                BOOL_FALSE);
            break;

        case SYS_MQTT_STATE_CONFIG_TIMEOUT:
            (void)mqtt_submit(
                MQTT_REQUEST_CONFIG_TIMEOUT,
                "AT+QMTCFG=\"timeout\",0,5,3,1\r\n",
                0U,
                "OK",
                NETWORK_MQTT_CONFIG_TIMEOUT_MS,
                3U,
                BOOL_FALSE);
            break;

        case SYS_MQTT_STATE_CONFIG_WILL:
            (void)mqtt_submit_will_transaction();
            break;

        case SYS_MQTT_STATE_OPENING:
            command_length = snprintf(
                _command,
                sizeof(_command),
                "AT+QMTOPEN=0,\"%s\",%u\r\n",
                _config.server_host,
                (unsigned int)_config.server_port);
            if ((command_length > 0) &&
                (command_length < (int)sizeof(_command)))
            {
                (void)mqtt_submit(
                    MQTT_REQUEST_OPEN,
                    _command,
                    (u16)command_length,
                    "+QMTOPEN:",
                    NETWORK_MQTT_OPEN_TIMEOUT_MS,
                    3U,
                    BOOL_FALSE);
            }
            break;

        case SYS_MQTT_STATE_CONNECTING:
            command_length = snprintf(
                _command,
                sizeof(_command),
                "AT+QMTCONN=0,\"%s\",\"%s\",\"%s\"\r\n",
                _config.client_id,
                _config.username,
                _config.password);
            if ((command_length > 0) &&
                (command_length < (int)sizeof(_command)))
            {
                (void)mqtt_submit(
                    MQTT_REQUEST_CONNECT,
                    _command,
                    (u16)command_length,
                    "+QMTCONN:",
                    NETWORK_MQTT_CONNECT_TIMEOUT_MS,
                    3U,
                    BOOL_FALSE);
            }
            break;

        case SYS_MQTT_STATE_SUBSCRIBE_DOWNLINK:
            command_length = snprintf(
                _command,
                sizeof(_command),
                "AT+QMTSUB=0,1,\"%s\",1\r\n",
                _config.downlink_topic);
            if ((command_length > 0) &&
                (command_length < (int)sizeof(_command)))
            {
                (void)mqtt_submit(
                    MQTT_REQUEST_SUB_DOWNLINK,
                    _command,
                    (u16)command_length,
                    "+QMTSUB:",
                    NETWORK_MQTT_SUBSCRIBE_TIMEOUT_MS,
                    3U,
                    BOOL_FALSE);
            }
            break;

        case SYS_MQTT_STATE_SUBSCRIBE_UPGRADE:
            command_length = snprintf(
                _command,
                sizeof(_command),
                "AT+QMTSUB=0,2,\"%s\",1\r\n",
                _config.upgrade_topic);
            if ((command_length > 0) &&
                (command_length < (int)sizeof(_command)))
            {
                (void)mqtt_submit(
                    MQTT_REQUEST_SUB_UPGRADE,
                    _command,
                    (u16)command_length,
                    "+QMTSUB:",
                    NETWORK_MQTT_SUBSCRIBE_TIMEOUT_MS,
                    3U,
                    BOOL_FALSE);
            }
            break;

        case SYS_MQTT_STATE_READY:
            if ((_active_publish_valid != BOOL_TRUE) &&
                (_publish_queue_count > 0U))
            {
                _active_publish = _publish_queue[0];
                _active_publish_valid = BOOL_TRUE;
                memmove(
                    &_publish_queue[0],
                    &_publish_queue[1],
                    sizeof(_publish_queue[0]) *
                        (_publish_queue_count - 1U));
                _publish_queue_count--;
                _snapshot.queued_publish_count = _publish_queue_count;
                _snapshot.active_packet_id = _active_publish.packet_id;
                _snapshot.active_source_id = _active_publish.source_id;
                _snapshot.active_request_id = _active_publish.request_id;
                mqtt_set_state(
                    SYS_MQTT_STATE_PUBLISH_HEADER,
                    now_ms,
                    0U);
            }
            break;

        case SYS_MQTT_STATE_PUBLISH_HEADER:
            (void)mqtt_submit_publish_transaction();
            break;

        case SYS_MQTT_STATE_PUBLISH_PAYLOAD:
            /* 同一 AT transaction/token 正在等待匹配 PUBACK。 */
            break;

        case SYS_MQTT_STATE_DISCONNECTING:
            (void)mqtt_submit(
                MQTT_REQUEST_DISCONNECT,
                "AT+QMTDISC=0\r\n",
                0U,
                "+QMTDISC:",
                NETWORK_MQTT_CLOSE_TIMEOUT_MS,
                1U,
                BOOL_FALSE);
            break;

        case SYS_MQTT_STATE_CLOSING:
            (void)mqtt_submit(
                MQTT_REQUEST_CLOSE,
                "AT+QMTCLOSE=0\r\n",
                0U,
                "+QMTCLOSE:",
                NETWORK_MQTT_CLOSE_TIMEOUT_MS,
                1U,
                BOOL_FALSE);
            break;

        case SYS_MQTT_STATE_RECOVERY_WAIT:
            mqtt_set_state(
                (_snapshot.configured == BOOL_TRUE) ?
                    SYS_MQTT_STATE_WAIT_NETWORK :
                    SYS_MQTT_STATE_WAIT_CONFIG,
                now_ms,
                0U);
            break;

        default:
            break;
    }
}
