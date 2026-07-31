/*************************************************************
程序功能：蜂窝模块、SIM、注册和模块级恢复状态机
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.7.31
*************************************************************/
#include "sys_cellular.h"
#include "network_config.h"
#include "sys_at_engine.h"
#include "sys_event.h"
#include "sys_time.h"
#include "hw_4g_io.h"

typedef enum
{
    CELLULAR_REQUEST_NONE = 0,
    CELLULAR_REQUEST_AT,
    CELLULAR_REQUEST_CPIN,
    CELLULAR_REQUEST_IMEI,
    CELLULAR_REQUEST_ICCID,
    CELLULAR_REQUEST_CEREG_ENABLE,
    CELLULAR_REQUEST_CEREG_QUERY,
    CELLULAR_REQUEST_QENG,
    CELLULAR_REQUEST_CFUN_DISABLE,
    CELLULAR_REQUEST_CFUN_ENABLE
} cellular_request_en;

static sys_cellular_snapshot_st _snapshot;
static cellular_request_en _request;
static boolean_en _request_pending;
static boolean_en _cancelling_request;
static boolean_en _request_line_valid;
static boolean_en _request_line_ready;
static s8 _request_cereg_stat;
static char _request_identity[21];
static u32 _registration_started_ms;
static u32 _last_health_query_ms;
static u32 _last_hard_reset_ms;
static u32 _hard_reset_window_started_ms;
static sys_cellular_recovery_reason_en _pending_reset_reason;
static boolean_en _signal_query_requested;
static boolean_en _reset_released;
static u32 _pwrkey_started_ms;

static boolean_en cellular_time_due(u32 now_ms, u32 deadline_ms)
{
    return (((s32)(now_ms - deadline_ms)) >= 0) ? BOOL_TRUE : BOOL_FALSE;
}

static void cellular_set_state(
    sys_cellular_state_en state,
    u32 now_ms,
    u32 delay_ms)
{
    _snapshot.state = state;
    _snapshot.state_since_ms = now_ms;
    _snapshot.next_action_ms = now_ms + delay_ms;
}

static void cellular_post_event(sys_event_type_en type, u32 value)
{
    sys_event_st event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.source_id = SYS_RESOURCE_OWNER_CELLULAR;
    event.timestamp_ms = sys_time_get_ms();
    event.value = value;
    (void)sys_event_post(&event);
}

static boolean_en cellular_line_starts_with(
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

static boolean_en cellular_copy_digits(
    char *destination,
    u16 capacity,
    const u8 *line,
    u16 length,
    u16 required_digits)
{
    u16 source_index;
    u16 destination_index;

    destination_index = 0U;
    for (source_index = 0U; source_index < length; source_index++)
    {
        if ((line[source_index] >= (u8)'0') &&
            (line[source_index] <= (u8)'9'))
        {
            if ((destination_index + 1U) >= capacity)
            {
                return BOOL_FALSE;
            }
            destination[destination_index++] = (char)line[source_index];
        }
    }
    destination[destination_index] = '\0';
    return (destination_index == required_digits) ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en cellular_parse_cereg(
    const u8 *line,
    u16 length,
    s8 *status)
{
    u16 index;
    s8 values[2];
    u8 value_count;

    if ((status == NULL) ||
        (cellular_line_starts_with(line, length, "+CEREG:") != BOOL_TRUE))
    {
        return BOOL_FALSE;
    }

    value_count = 0U;
    index = 7U;
    while ((index < length) && (value_count < 2U))
    {
        while ((index < length) &&
               ((line[index] == (u8)' ') || (line[index] == (u8)',')))
        {
            index++;
        }
        if ((index < length) &&
            (line[index] >= (u8)'0') && (line[index] <= (u8)'9'))
        {
            values[value_count++] = (s8)(line[index] - (u8)'0');
            while ((index < length) &&
                   (line[index] >= (u8)'0') && (line[index] <= (u8)'9'))
            {
                index++;
            }
        }
        else
        {
            index++;
        }
    }
    if (value_count == 0U)
    {
        return BOOL_FALSE;
    }
    *status = values[value_count - 1U];
    return BOOL_TRUE;
}

static boolean_en cellular_parse_rsrp(
    const u8 *line,
    u16 length,
    s16 *rsrp_dbm)
{
    u16 index;
    s32 value;
    s32 sign;

    if ((rsrp_dbm == NULL) ||
        (cellular_line_starts_with(line, length, "+QENG:") != BOOL_TRUE))
    {
        return BOOL_FALSE;
    }
    index = 0U;
    while (index < length)
    {
        while ((index < length) &&
               (line[index] != (u8)'-') &&
               ((line[index] < (u8)'0') || (line[index] > (u8)'9')))
        {
            index++;
        }
        sign = 1;
        if ((index < length) && (line[index] == (u8)'-'))
        {
            sign = -1;
            index++;
        }
        if ((index >= length) ||
            (line[index] < (u8)'0') || (line[index] > (u8)'9'))
        {
            continue;
        }
        value = 0;
        while ((index < length) &&
               (line[index] >= (u8)'0') && (line[index] <= (u8)'9'))
        {
            value = (value * 10) + (s32)(line[index] - (u8)'0');
            index++;
        }
        value *= sign;
        if ((value >= -160) && (value <= -40))
        {
            *rsrp_dbm = (s16)value;
            return BOOL_TRUE;
        }
    }
    return BOOL_FALSE;
}

static void cellular_line_handler(
    const u8 *line,
    u16 length,
    u16 owner_id,
    void *context)
{
    (void)owner_id;
    (void)context;

    if (_request == CELLULAR_REQUEST_CPIN)
    {
        if ((cellular_line_starts_with(line, length, "+CPIN:") == BOOL_TRUE) &&
            (length >= 12U) &&
            (memcmp(&line[length - 5U], "READY", 5U) == 0))
        {
            _request_line_ready = BOOL_TRUE;
            _request_line_valid = BOOL_TRUE;
        }
    }
    else if (_request == CELLULAR_REQUEST_IMEI)
    {
        if (cellular_copy_digits(
                _request_identity,
                sizeof(_request_identity),
                line,
                length,
                15U) == BOOL_TRUE)
        {
            _request_line_valid = BOOL_TRUE;
        }
    }
    else if (_request == CELLULAR_REQUEST_ICCID)
    {
        if ((cellular_line_starts_with(line, length, "+QCCID:") == BOOL_TRUE) &&
            (cellular_copy_digits(
                _request_identity,
                sizeof(_request_identity),
                line,
                length,
                20U) == BOOL_TRUE))
        {
            _request_line_valid = BOOL_TRUE;
        }
    }
    else if (_request == CELLULAR_REQUEST_CEREG_QUERY)
    {
        if (cellular_parse_cereg(line, length, &_request_cereg_stat) ==
            BOOL_TRUE)
        {
            _request_line_valid = BOOL_TRUE;
        }
    }
    else if (_request == CELLULAR_REQUEST_QENG)
    {
        if (cellular_parse_rsrp(line, length, &_snapshot.rsrp_dbm) ==
            BOOL_TRUE)
        {
            _request_line_valid = BOOL_TRUE;
        }
    }
}

static void cellular_complete_handler(
    sys_at_result_en result,
    u16 owner_id,
    u16 generation,
    void *context)
{
    u32 now_ms;
    cellular_request_en completed_request;

    (void)owner_id;
    (void)generation;
    (void)context;
    now_ms = sys_time_get_ms();
    completed_request = _request;
    _request = CELLULAR_REQUEST_NONE;
    _request_pending = BOOL_FALSE;

    if ((_snapshot.enabled != BOOL_TRUE) ||
        (_cancelling_request == BOOL_TRUE))
    {
        return;
    }

    if (result != SYS_AT_RESULT_OK)
    {
        if (completed_request == CELLULAR_REQUEST_CPIN)
        {
            _snapshot.sim_ready = BOOL_FALSE;
            _snapshot.recovery_reason = SYS_CELLULAR_RECOVERY_SIM;
            cellular_set_state(
                SYS_CELLULAR_STATE_SIM_ERROR_WAIT,
                now_ms,
                NETWORK_CELLULAR_SIM_RETRY_MS);
        }
        else if (completed_request == CELLULAR_REQUEST_QENG)
        {
            _signal_query_requested = BOOL_FALSE;
            _snapshot.signal_query_generation++;
            cellular_set_state(
                SYS_CELLULAR_STATE_NETWORK_READY,
                now_ms,
                0U);
        }
        else if (completed_request == CELLULAR_REQUEST_CEREG_QUERY)
        {
            _snapshot.registered = BOOL_FALSE;
            _snapshot.pdp_active = BOOL_FALSE;
            cellular_post_event(SYS_EVENT_NETWORK_LOST, (u32)result);
            cellular_set_state(
                SYS_CELLULAR_STATE_NETWORK_REGISTERING,
                now_ms,
                NETWORK_CELLULAR_QUERY_INTERVAL_MS);
        }
        else if ((completed_request == CELLULAR_REQUEST_CFUN_DISABLE) ||
                 (completed_request == CELLULAR_REQUEST_CFUN_ENABLE))
        {
            sys_cellular_request_hard_reset(SYS_CELLULAR_RECOVERY_CFUN);
        }
        else
        {
            if (_snapshot.at_probe_failures < 0xFFU)
            {
                _snapshot.at_probe_failures++;
            }
            cellular_post_event(SYS_EVENT_MODEM_NO_RESPONSE, (u32)result);
            sys_cellular_request_hard_reset(
                SYS_CELLULAR_RECOVERY_AT_TIMEOUT);
        }
        return;
    }

    _snapshot.last_at_ok_ms = now_ms;
    if (completed_request == CELLULAR_REQUEST_AT)
    {
        _snapshot.at_ready = BOOL_TRUE;
        _snapshot.at_probe_failures = 0U;
        cellular_post_event(SYS_EVENT_MODEM_READY, 0U);
        cellular_set_state(SYS_CELLULAR_STATE_SIM_CHECK, now_ms, 0U);
    }
    else if (completed_request == CELLULAR_REQUEST_CPIN)
    {
        if ((_request_line_valid == BOOL_TRUE) &&
            (_request_line_ready == BOOL_TRUE))
        {
            _snapshot.sim_ready = BOOL_TRUE;
            cellular_set_state(SYS_CELLULAR_STATE_READ_IMEI, now_ms, 0U);
        }
        else
        {
            _snapshot.sim_ready = BOOL_FALSE;
            _snapshot.recovery_reason = SYS_CELLULAR_RECOVERY_SIM;
            cellular_set_state(
                SYS_CELLULAR_STATE_SIM_ERROR_WAIT,
                now_ms,
                NETWORK_CELLULAR_SIM_RETRY_MS);
        }
    }
    else if (completed_request == CELLULAR_REQUEST_IMEI)
    {
        if (_request_line_valid == BOOL_TRUE)
        {
            memcpy(_snapshot.imei, _request_identity, sizeof(_snapshot.imei));
            _snapshot.imei_ready = BOOL_TRUE;
        }
        cellular_set_state(SYS_CELLULAR_STATE_READ_ICCID, now_ms, 0U);
    }
    else if (completed_request == CELLULAR_REQUEST_ICCID)
    {
        if (_request_line_valid == BOOL_TRUE)
        {
            memcpy(_snapshot.iccid, _request_identity, sizeof(_snapshot.iccid));
            _snapshot.iccid_ready = BOOL_TRUE;
        }
        cellular_set_state(SYS_CELLULAR_STATE_CEREG_ENABLE, now_ms, 0U);
    }
    else if (completed_request == CELLULAR_REQUEST_CEREG_ENABLE)
    {
        _registration_started_ms = now_ms;
        cellular_set_state(
            SYS_CELLULAR_STATE_NETWORK_REGISTERING,
            now_ms,
            0U);
    }
    else if (completed_request == CELLULAR_REQUEST_CEREG_QUERY)
    {
        if (_request_line_valid != BOOL_TRUE)
        {
            cellular_set_state(
                SYS_CELLULAR_STATE_NETWORK_REGISTERING,
                now_ms,
                NETWORK_CELLULAR_QUERY_INTERVAL_MS);
        }
        else
        {
            _snapshot.cereg_stat = _request_cereg_stat;
            if ((_request_cereg_stat == 1) || (_request_cereg_stat == 5))
            {
                if (_snapshot.registered != BOOL_TRUE)
                {
                    cellular_post_event(
                        SYS_EVENT_NETWORK_REGISTERED,
                        (u32)_request_cereg_stat);
                }
                _snapshot.registered = BOOL_TRUE;
                _snapshot.last_registered_ms = now_ms;
                _snapshot.cfun_attempts = 0U;
                _snapshot.recovery_reason = SYS_CELLULAR_RECOVERY_NONE;
                cellular_set_state(
                    SYS_CELLULAR_STATE_PDP_ACTIVATING,
                    now_ms,
                    0U);
            }
            else
            {
                _snapshot.registered = BOOL_FALSE;
                _snapshot.pdp_active = BOOL_FALSE;
                cellular_set_state(
                    SYS_CELLULAR_STATE_NETWORK_REGISTERING,
                    now_ms,
                    NETWORK_CELLULAR_QUERY_INTERVAL_MS);
            }
        }
    }
    else if (completed_request == CELLULAR_REQUEST_CFUN_DISABLE)
    {
        cellular_set_state(
            SYS_CELLULAR_STATE_CFUN_ENABLE,
            now_ms,
            1000U);
    }
    else if (completed_request == CELLULAR_REQUEST_CFUN_ENABLE)
    {
        _snapshot.at_ready = BOOL_FALSE;
        _snapshot.registered = BOOL_FALSE;
        _snapshot.pdp_active = BOOL_FALSE;
        _snapshot.generation++;
        cellular_set_state(
            SYS_CELLULAR_STATE_AT_PROBE,
            now_ms,
            NETWORK_CELLULAR_BOOT_WAIT_MS);
    }
    else if (completed_request == CELLULAR_REQUEST_QENG)
    {
        _signal_query_requested = BOOL_FALSE;
        _snapshot.signal_query_generation++;
        cellular_set_state(
            SYS_CELLULAR_STATE_NETWORK_READY,
            now_ms,
            0U);
    }
}

static boolean_en cellular_submit(
    cellular_request_en request_kind,
    const char *command,
    const char *expected_token,
    u32 timeout_ms,
    u8 retry_max)
{
    sys_at_request_st request;

    if (_request_pending == BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    memset(&request, 0, sizeof(request));
    request.command = command;
    request.command_length = (u16)strlen(command);
    request.expected_token = expected_token;
    request.error_token = "ERROR";
    request.timeout_ms = timeout_ms;
    request.retry_max = retry_max;
    request.priority = 2U;
    request.owner_id = SYS_RESOURCE_OWNER_CELLULAR;
    request.line_handler = cellular_line_handler;
    request.complete_handler = cellular_complete_handler;

    _request_line_valid = BOOL_FALSE;
    _request_line_ready = BOOL_FALSE;
    _request_cereg_stat = -1;
    memset(_request_identity, 0, sizeof(_request_identity));
    _request = request_kind;
    if (sys_at_engine_submit(&request) != BOOL_TRUE)
    {
        _request = CELLULAR_REQUEST_NONE;
        _snapshot.next_action_ms = sys_time_get_ms() +
            NETWORK_CELLULAR_QUERY_INTERVAL_MS;
        return BOOL_FALSE;
    }
    _request_pending = BOOL_TRUE;
    return BOOL_TRUE;
}

static void cellular_begin_hard_reset(u32 now_ms)
{
    if ((_snapshot.state == SYS_CELLULAR_STATE_HARD_RESET_ASSERT) ||
        (_snapshot.state == SYS_CELLULAR_STATE_HARD_RESET_PWRKEY) ||
        (_snapshot.state == SYS_CELLULAR_STATE_HARD_RESET_BOOT_WAIT))
    {
        return;
    }
    if ((now_ms - _hard_reset_window_started_ms) >=
        NETWORK_CELLULAR_RESET_WINDOW_MS)
    {
        _hard_reset_window_started_ms = now_ms;
        _snapshot.hard_resets_in_window = 0U;
    }
    if (((_snapshot.hard_resets_in_window > 0U) &&
         ((now_ms - _last_hard_reset_ms) <
          NETWORK_CELLULAR_RESET_COOLDOWN_MS)) ||
        (_snapshot.hard_resets_in_window >=
         NETWORK_CELLULAR_RESET_MAX_PER_WINDOW))
    {
        _snapshot.recovery_reason =
            SYS_CELLULAR_RECOVERY_HARD_RESET_LIMIT;
        cellular_set_state(
            SYS_CELLULAR_STATE_RECOVERY_WAIT,
            now_ms,
            NETWORK_CELLULAR_RESET_COOLDOWN_MS);
        return;
    }

    _request = CELLULAR_REQUEST_NONE;
    _request_pending = BOOL_FALSE;
    _cancelling_request = BOOL_TRUE;
    (void)sys_at_engine_cancel_owner(SYS_RESOURCE_OWNER_CELLULAR);
    _cancelling_request = BOOL_FALSE;
    _snapshot.at_ready = BOOL_FALSE;
    _snapshot.registered = BOOL_FALSE;
    _snapshot.pdp_active = BOOL_FALSE;
    _snapshot.recovery_reason = _pending_reset_reason;
    _snapshot.hard_resets_in_window++;
    _last_hard_reset_ms = now_ms;
    reset_on();
    _reset_released = BOOL_FALSE;
    cellular_set_state(
        SYS_CELLULAR_STATE_HARD_RESET_ASSERT,
        now_ms,
        NETWORK_CELLULAR_RESET_TO_PWRKEY_MS);
}

void sys_cellular_init(void)
{
    u32 now_ms;

    now_ms = sys_time_get_ms();
    memset(&_snapshot, 0, sizeof(_snapshot));
    _snapshot.enabled = BOOL_TRUE;
    _snapshot.cereg_stat = -1;
    _snapshot.rsrp_dbm = (s16)-32768;
    _snapshot.generation = 1U;
    _request = CELLULAR_REQUEST_NONE;
    _request_pending = BOOL_FALSE;
    _cancelling_request = BOOL_FALSE;
    _registration_started_ms = now_ms;
    _last_health_query_ms = now_ms;
    _last_hard_reset_ms = now_ms - NETWORK_CELLULAR_RESET_COOLDOWN_MS;
    _hard_reset_window_started_ms = now_ms;
    _pending_reset_reason = SYS_CELLULAR_RECOVERY_NONE;
    _signal_query_requested = BOOL_FALSE;
    _reset_released = BOOL_TRUE;
    _pwrkey_started_ms = 0U;
    cellular_set_state(SYS_CELLULAR_STATE_AT_PROBE, now_ms, 0U);
    (void)sys_at_engine_add_urc_handler(sys_cellular_on_urc, NULL);
}

void sys_cellular_set_enabled(boolean_en enabled)
{
    u32 now_ms;

    now_ms = sys_time_get_ms();
    if (enabled != BOOL_TRUE)
    {
        _request = CELLULAR_REQUEST_NONE;
        _request_pending = BOOL_FALSE;
        _cancelling_request = BOOL_TRUE;
        (void)sys_at_engine_cancel_owner(SYS_RESOURCE_OWNER_CELLULAR);
        _cancelling_request = BOOL_FALSE;
        _snapshot.enabled = BOOL_FALSE;
        if (_snapshot.state == SYS_CELLULAR_STATE_HARD_RESET_ASSERT)
        {
            reset_off();
        }
        pwr_off();
        cellular_set_state(SYS_CELLULAR_STATE_DISABLED, now_ms, 0U);
        return;
    }
    if (_snapshot.enabled != BOOL_TRUE)
    {
        _snapshot.enabled = BOOL_TRUE;
        _snapshot.at_ready = BOOL_FALSE;
        _snapshot.registered = BOOL_FALSE;
        _snapshot.pdp_active = BOOL_FALSE;
        pwr_off();
        reset_off();
        cellular_set_state(SYS_CELLULAR_STATE_AT_PROBE, now_ms, 0U);
    }
}

void sys_cellular_request_network_recovery(
    sys_cellular_recovery_reason_en reason)
{
    u32 now_ms;

    if (_snapshot.enabled != BOOL_TRUE)
    {
        return;
    }
    if ((_snapshot.state == SYS_CELLULAR_STATE_CFUN_DISABLE) ||
        (_snapshot.state == SYS_CELLULAR_STATE_CFUN_ENABLE) ||
        (_snapshot.state == SYS_CELLULAR_STATE_HARD_RESET_ASSERT) ||
        (_snapshot.state == SYS_CELLULAR_STATE_HARD_RESET_PWRKEY) ||
        (_snapshot.state == SYS_CELLULAR_STATE_HARD_RESET_BOOT_WAIT))
    {
        return;
    }
    now_ms = sys_time_get_ms();
    _request = CELLULAR_REQUEST_NONE;
    _request_pending = BOOL_FALSE;
    _cancelling_request = BOOL_TRUE;
    (void)sys_at_engine_cancel_owner(SYS_RESOURCE_OWNER_CELLULAR);
    _cancelling_request = BOOL_FALSE;
    _snapshot.registered = BOOL_FALSE;
    _snapshot.pdp_active = BOOL_FALSE;
    _snapshot.recovery_reason = reason;
    if (_snapshot.cfun_attempts < NETWORK_CELLULAR_CFUN_MAX)
    {
        _snapshot.cfun_attempts++;
        cellular_set_state(
            SYS_CELLULAR_STATE_CFUN_DISABLE,
            now_ms,
            0U);
    }
    else
    {
        sys_cellular_request_hard_reset(reason);
    }
}

void sys_cellular_request_hard_reset(
    sys_cellular_recovery_reason_en reason)
{
    if (_snapshot.enabled != BOOL_TRUE)
    {
        return;
    }
    _pending_reset_reason = reason;
    cellular_begin_hard_reset(sys_time_get_ms());
}

void sys_cellular_notify_transport_opened(void)
{
    u32 now_ms;

    if ((_snapshot.enabled != BOOL_TRUE) ||
        (_snapshot.registered != BOOL_TRUE))
    {
        return;
    }
    now_ms = sys_time_get_ms();
    _snapshot.pdp_active = BOOL_TRUE;
    _snapshot.last_pdp_active_ms = now_ms;
    _snapshot.recovery_reason = SYS_CELLULAR_RECOVERY_NONE;
    cellular_set_state(SYS_CELLULAR_STATE_NETWORK_READY, now_ms, 0U);
}

boolean_en sys_cellular_request_signal_query(void)
{
    if ((_snapshot.enabled != BOOL_TRUE) ||
        (_snapshot.registered != BOOL_TRUE) ||
        (_signal_query_requested == BOOL_TRUE))
    {
        return BOOL_FALSE;
    }
    _signal_query_requested = BOOL_TRUE;
    return BOOL_TRUE;
}

void sys_cellular_get_snapshot(sys_cellular_snapshot_st *snapshot)
{
    if (snapshot != NULL)
    {
        *snapshot = _snapshot;
    }
}

void sys_cellular_on_urc(
    const u8 *line,
    u16 length,
    u16 owner_id,
    void *context)
{
    s8 status;
    u32 now_ms;

    (void)owner_id;
    (void)context;
    if ((_snapshot.enabled != BOOL_TRUE) || (line == NULL))
    {
        return;
    }
    now_ms = sys_time_get_ms();
    if (cellular_parse_cereg(line, length, &status) == BOOL_TRUE)
    {
        _snapshot.cereg_stat = status;
        if ((status == 1) || (status == 5))
        {
            if (_snapshot.registered != BOOL_TRUE)
            {
                cellular_post_event(SYS_EVENT_NETWORK_REGISTERED, (u32)status);
            }
            _snapshot.registered = BOOL_TRUE;
            _snapshot.last_registered_ms = now_ms;
            if ((_snapshot.state == SYS_CELLULAR_STATE_NETWORK_REGISTERING) ||
                (_snapshot.state == SYS_CELLULAR_STATE_NETWORK_HEALTH_CHECK))
            {
                cellular_set_state(
                    SYS_CELLULAR_STATE_PDP_ACTIVATING,
                    now_ms,
                    0U);
            }
        }
        else if (_snapshot.registered == BOOL_TRUE)
        {
            _snapshot.registered = BOOL_FALSE;
            _snapshot.pdp_active = BOOL_FALSE;
            cellular_post_event(SYS_EVENT_NETWORK_LOST, (u32)status);
            cellular_set_state(
                SYS_CELLULAR_STATE_NETWORK_REGISTERING,
                now_ms,
                NETWORK_CELLULAR_QUERY_INTERVAL_MS);
        }
    }
    else if ((cellular_line_starts_with(
                  line,
                  length,
                  "+QIURC: \"pdpdeact\"") == BOOL_TRUE) ||
             (cellular_line_starts_with(
                  line,
                  length,
                  "+QIURC:\"pdpdeact\"") == BOOL_TRUE))
    {
        _snapshot.pdp_active = BOOL_FALSE;
        _snapshot.recovery_reason = SYS_CELLULAR_RECOVERY_PDP_DEACT;
        cellular_post_event(
            SYS_EVENT_NETWORK_LOST,
            (u32)SYS_CELLULAR_RECOVERY_PDP_DEACT);
        cellular_set_state(
            SYS_CELLULAR_STATE_NETWORK_REGISTERING,
            now_ms,
            0U);
    }
}

void sys_cellular_process(void)
{
    u32 now_ms;

    now_ms = sys_time_get_ms();
    if ((_snapshot.enabled != BOOL_TRUE) ||
        (_request_pending == BOOL_TRUE) ||
        (cellular_time_due(now_ms, _snapshot.next_action_ms) != BOOL_TRUE))
    {
        return;
    }

    switch (_snapshot.state)
    {
        case SYS_CELLULAR_STATE_AT_PROBE:
            (void)cellular_submit(
                CELLULAR_REQUEST_AT,
                "AT\r\n",
                "OK",
                NETWORK_CELLULAR_AT_TIMEOUT_MS,
                NETWORK_CELLULAR_AT_RETRY_MAX);
            break;

        case SYS_CELLULAR_STATE_SIM_CHECK:
        case SYS_CELLULAR_STATE_SIM_ERROR_WAIT:
            cellular_set_state(SYS_CELLULAR_STATE_SIM_CHECK, now_ms, 0U);
            (void)cellular_submit(
                CELLULAR_REQUEST_CPIN,
                "AT+CPIN?\r\n",
                "OK",
                NETWORK_CELLULAR_AT_TIMEOUT_MS,
                1U);
            break;

        case SYS_CELLULAR_STATE_READ_IMEI:
            (void)cellular_submit(
                CELLULAR_REQUEST_IMEI,
                "AT+CGSN\r\n",
                "OK",
                NETWORK_CELLULAR_AT_TIMEOUT_MS,
                1U);
            break;

        case SYS_CELLULAR_STATE_READ_ICCID:
            (void)cellular_submit(
                CELLULAR_REQUEST_ICCID,
                "AT+QCCID\r\n",
                "OK",
                NETWORK_CELLULAR_AT_TIMEOUT_MS,
                1U);
            break;

        case SYS_CELLULAR_STATE_CEREG_ENABLE:
            (void)cellular_submit(
                CELLULAR_REQUEST_CEREG_ENABLE,
                "AT+CEREG=2\r\n",
                "OK",
                NETWORK_CELLULAR_AT_TIMEOUT_MS,
                1U);
            break;

        case SYS_CELLULAR_STATE_NETWORK_REGISTERING:
            if ((now_ms - _registration_started_ms) >=
                NETWORK_CELLULAR_REGISTRATION_TIMEOUT_MS)
            {
                sys_cellular_request_network_recovery(
                    SYS_CELLULAR_RECOVERY_REGISTRATION);
            }
            else
            {
                (void)cellular_submit(
                    CELLULAR_REQUEST_CEREG_QUERY,
                    "AT+CEREG?\r\n",
                    "OK",
                    NETWORK_CELLULAR_AT_TIMEOUT_MS,
                    1U);
            }
            break;

        case SYS_CELLULAR_STATE_PDP_ACTIVATING:
            /*
             * 不猜测未获手册/实机确认的 PDP 激活命令。
             * MQTT QMTOPEN 成功后会调用 notify_transport_opened。
             */
            cellular_set_state(
                SYS_CELLULAR_STATE_NETWORK_READY,
                now_ms,
                0U);
            break;

        case SYS_CELLULAR_STATE_NETWORK_READY:
            if (_signal_query_requested == BOOL_TRUE)
            {
                (void)cellular_submit(
                    CELLULAR_REQUEST_QENG,
                    "AT+QENG=\"servingcell\"\r\n",
                    "OK",
                    NETWORK_CELLULAR_AT_TIMEOUT_MS,
                    0U);
            }
            else if ((now_ms - _last_health_query_ms) >=
                NETWORK_CELLULAR_HEALTH_INTERVAL_MS)
            {
                _last_health_query_ms = now_ms;
                cellular_set_state(
                    SYS_CELLULAR_STATE_NETWORK_HEALTH_CHECK,
                    now_ms,
                    0U);
            }
            break;

        case SYS_CELLULAR_STATE_NETWORK_HEALTH_CHECK:
            (void)cellular_submit(
                CELLULAR_REQUEST_CEREG_QUERY,
                "AT+CEREG?\r\n",
                "OK",
                NETWORK_CELLULAR_AT_TIMEOUT_MS,
                1U);
            break;

        case SYS_CELLULAR_STATE_CFUN_DISABLE:
            (void)cellular_submit(
                CELLULAR_REQUEST_CFUN_DISABLE,
                "AT+CFUN=0\r\n",
                "OK",
                NETWORK_CELLULAR_CFUN_TIMEOUT_MS,
                0U);
            break;

        case SYS_CELLULAR_STATE_CFUN_ENABLE:
            (void)cellular_submit(
                CELLULAR_REQUEST_CFUN_ENABLE,
                "AT+CFUN=1\r\n",
                "OK",
                NETWORK_CELLULAR_CFUN_TIMEOUT_MS,
                0U);
            break;

        case SYS_CELLULAR_STATE_HARD_RESET_ASSERT:
            pwr_on();
            _pwrkey_started_ms = now_ms;
            cellular_set_state(
                SYS_CELLULAR_STATE_HARD_RESET_PWRKEY,
                now_ms,
                NETWORK_CELLULAR_RESET_ASSERT_MS -
                    NETWORK_CELLULAR_RESET_TO_PWRKEY_MS);
            break;

        case SYS_CELLULAR_STATE_HARD_RESET_PWRKEY:
            if (_reset_released != BOOL_TRUE)
            {
                reset_off();
                _reset_released = BOOL_TRUE;
                _snapshot.next_action_ms = _pwrkey_started_ms +
                    NETWORK_CELLULAR_PWRKEY_PULSE_MS;
            }
            else
            {
                pwr_off();
                _snapshot.generation++;
                cellular_set_state(
                    SYS_CELLULAR_STATE_HARD_RESET_BOOT_WAIT,
                    now_ms,
                    NETWORK_CELLULAR_BOOT_WAIT_MS);
            }
            break;

        case SYS_CELLULAR_STATE_HARD_RESET_BOOT_WAIT:
            cellular_set_state(SYS_CELLULAR_STATE_AT_PROBE, now_ms, 0U);
            break;

        case SYS_CELLULAR_STATE_RECOVERY_WAIT:
            sys_cellular_request_hard_reset(_pending_reset_reason);
            break;

        default:
            break;
    }
}
