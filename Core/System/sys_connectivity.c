/*************************************************************
程序功能：蜂窝、MQTT、订阅和PUBACK租约的唯一在线判据及分级恢复
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.7.31
*************************************************************/
#include "sys_connectivity.h"
#include "sys_cellular.h"
#include "sys_time.h"

static sys_connectivity_snapshot_st _snapshot;
static char _probe_topic[NETWORK_MQTT_TOPIC_CAPACITY];
static sys_mqtt_publish_result_st _forward_results[
    NETWORK_MQTT_RESULT_QUEUE_CAPACITY];
static u8 _forward_result_count;
static boolean_en _mqtt_recovery_requested;
static boolean_en _network_recovery_requested;
static u32 _next_probe_ms;

static boolean_en connectivity_time_due(u32 now_ms, u32 deadline_ms)
{
    return (((s32)(now_ms - deadline_ms)) >= 0) ?
        BOOL_TRUE : BOOL_FALSE;
}

static void connectivity_set_state(
    sys_connectivity_state_en state,
    u32 now_ms)
{
    if (_snapshot.state != state)
    {
        _snapshot.state = state;
        _snapshot.state_since_ms = now_ms;
    }
}

static boolean_en connectivity_lease_valid(
    u32 now_ms,
    u32 evidence_ms,
    u32 lease_ms)
{
    if (evidence_ms == 0U)
    {
        return BOOL_FALSE;
    }
    return ((now_ms - evidence_ms) <= lease_ms) ?
        BOOL_TRUE : BOOL_FALSE;
}

static void connectivity_forward_result(
    const sys_mqtt_publish_result_st *result)
{
    if (_forward_result_count >= NETWORK_MQTT_RESULT_QUEUE_CAPACITY)
    {
        memmove(
            &_forward_results[0],
            &_forward_results[1],
            sizeof(_forward_results[0]) *
                (NETWORK_MQTT_RESULT_QUEUE_CAPACITY - 1U));
        _forward_result_count--;
    }
    _forward_results[_forward_result_count++] = *result;
}

static void connectivity_consume_results(u32 now_ms)
{
    sys_mqtt_publish_result_st result;

    while (sys_mqtt_get_publish_result(&result) == BOOL_TRUE)
    {
        connectivity_forward_result(&result);
        if (result.session_generation != _snapshot.session_generation)
        {
            continue;
        }
        if (result.result == SYS_MQTT_RESULT_SUCCESS)
        {
            _snapshot.last_any_puback_ms = result.timestamp_ms;
            _snapshot.consecutive_failures = 0U;
            if (result.source_id == NETWORK_SOURCE_CONNECTIVITY_PROBE)
            {
                _snapshot.last_probe_success_ms = result.timestamp_ms;
            }
            else
            {
                _snapshot.last_business_puback_ms = result.timestamp_ms;
            }
        }
        else if (result.source_id == NETWORK_SOURCE_CONNECTIVITY_PROBE)
        {
            if (_snapshot.consecutive_failures < 0xFFU)
            {
                _snapshot.consecutive_failures++;
            }
        }
    }
    (void)now_ms;
}

static boolean_en connectivity_send_probe(u32 now_ms)
{
    static const u8 probe_payload[] = NETWORK_CONNECTIVITY_PROBE_PAYLOAD;

    if ((_probe_topic[0] == '\0') ||
        (sys_mqtt_publish(
            _probe_topic,
            probe_payload,
            (u16)(sizeof(probe_payload) - 1U),
            1U,
            NETWORK_SOURCE_CONNECTIVITY_PROBE,
            _snapshot.next_probe_request_id) != BOOL_TRUE))
    {
        /*
         * 本地队列暂满不是 Broker/PUBACK 失败，不计入链路失败阈值。
         * 仅做有界的短退避，避免每轮高速重试。
         */
        _next_probe_ms = now_ms +
            NETWORK_CONNECTIVITY_PROBE_ENQUEUE_RETRY_MS;
        return BOOL_FALSE;
    }
    else
    {
        _snapshot.last_probe_ms = now_ms;
        _snapshot.next_probe_request_id++;
        if (_snapshot.next_probe_request_id == 0U)
        {
            _snapshot.next_probe_request_id = 1U;
        }
        _next_probe_ms = now_ms +
            NETWORK_CONNECTIVITY_PROBE_INTERVAL_MS;
    }
    return BOOL_TRUE;
}

void sys_connectivity_init(void)
{
    u32 now_ms;

    now_ms = sys_time_get_ms();
    memset(&_snapshot, 0, sizeof(_snapshot));
    memset(_probe_topic, 0, sizeof(_probe_topic));
    memset(_forward_results, 0, sizeof(_forward_results));
    _snapshot.enabled = BOOL_TRUE;
    _snapshot.next_probe_request_id = 1U;
    _forward_result_count = 0U;
    _mqtt_recovery_requested = BOOL_FALSE;
    _network_recovery_requested = BOOL_FALSE;
    _next_probe_ms = now_ms;
    connectivity_set_state(SYS_CONNECTIVITY_STATE_OFFLINE, now_ms);
}

void sys_connectivity_set_enabled(boolean_en enabled)
{
    u32 now_ms;

    now_ms = sys_time_get_ms();
    _snapshot.enabled = (enabled == BOOL_TRUE) ? BOOL_TRUE : BOOL_FALSE;
    if (enabled != BOOL_TRUE)
    {
        _snapshot.transport_ready = BOOL_FALSE;
        _snapshot.online = BOOL_FALSE;
        connectivity_set_state(SYS_CONNECTIVITY_STATE_DISABLED, now_ms);
    }
    else
    {
        _snapshot.consecutive_failures = 0U;
        _mqtt_recovery_requested = BOOL_FALSE;
        _network_recovery_requested = BOOL_FALSE;
        connectivity_set_state(SYS_CONNECTIVITY_STATE_OFFLINE, now_ms);
    }
}

boolean_en sys_connectivity_set_probe_topic(const char *topic)
{
    u16 length;

    if (topic == NULL)
    {
        return BOOL_FALSE;
    }
    length = (u16)strlen(topic);
    if ((length == 0U) || (length >= sizeof(_probe_topic)))
    {
        return BOOL_FALSE;
    }
    memcpy(_probe_topic, topic, length + 1U);
    return BOOL_TRUE;
}

boolean_en sys_connectivity_get_publish_result(
    sys_mqtt_publish_result_st *result)
{
    if ((result == NULL) || (_forward_result_count == 0U))
    {
        return BOOL_FALSE;
    }
    *result = _forward_results[0];
    memmove(
        &_forward_results[0],
        &_forward_results[1],
        sizeof(_forward_results[0]) * (_forward_result_count - 1U));
    _forward_result_count--;
    return BOOL_TRUE;
}

void sys_connectivity_get_snapshot(
    sys_connectivity_snapshot_st *snapshot)
{
    if (snapshot != NULL)
    {
        *snapshot = _snapshot;
    }
}

void sys_connectivity_process(void)
{
    sys_cellular_snapshot_st cellular;
    sys_mqtt_snapshot_st mqtt;
    u32 now_ms;
    boolean_en combined_evidence;

    now_ms = sys_time_get_ms();
    if (_snapshot.enabled != BOOL_TRUE)
    {
        return;
    }
    sys_cellular_get_snapshot(&cellular);
    sys_mqtt_get_snapshot(&mqtt);
    if (_snapshot.session_generation != mqtt.session_generation)
    {
        _snapshot.session_generation = mqtt.session_generation;
        _snapshot.last_any_puback_ms = 0U;
        _snapshot.last_probe_success_ms = 0U;
        _snapshot.last_probe_ms = 0U;
        _snapshot.consecutive_failures = 0U;
        _next_probe_ms = now_ms;
    }
    connectivity_consume_results(now_ms);

    _snapshot.transport_ready =
        ((mqtt.ready == BOOL_TRUE) &&
         (mqtt.connected == BOOL_TRUE) &&
         (mqtt.downlink_subscribed == BOOL_TRUE) &&
         (mqtt.upgrade_subscribed == BOOL_TRUE)) ?
            BOOL_TRUE : BOOL_FALSE;
    _snapshot.at_lease_valid =
        ((cellular.at_ready == BOOL_TRUE) &&
         (connectivity_lease_valid(
              now_ms,
              cellular.last_at_ok_ms,
              NETWORK_CONNECTIVITY_AT_LEASE_MS) == BOOL_TRUE)) ?
            BOOL_TRUE : BOOL_FALSE;
    _snapshot.registration_lease_valid =
        ((cellular.registered == BOOL_TRUE) &&
         (connectivity_lease_valid(
              now_ms,
              cellular.last_registered_ms,
              NETWORK_CONNECTIVITY_REGISTER_LEASE_MS) == BOOL_TRUE)) ?
            BOOL_TRUE : BOOL_FALSE;
    _snapshot.puback_lease_valid = connectivity_lease_valid(
        now_ms,
        _snapshot.last_any_puback_ms,
        NETWORK_CONNECTIVITY_PUBACK_LEASE_MS);
    combined_evidence =
        ((_snapshot.at_lease_valid == BOOL_TRUE) &&
         (_snapshot.registration_lease_valid == BOOL_TRUE) &&
         (cellular.pdp_active == BOOL_TRUE) &&
         (_snapshot.transport_ready == BOOL_TRUE) &&
         (_snapshot.puback_lease_valid == BOOL_TRUE)) ?
            BOOL_TRUE : BOOL_FALSE;
    _snapshot.online = combined_evidence;

    if ((cellular.at_ready != BOOL_TRUE) ||
        (_snapshot.at_lease_valid != BOOL_TRUE))
    {
        /*
         * 上电 AT 探测、CFUN 和硬复位都由 cellular 自己定时推进。
         * connectivity 在采集期间只观察，不能每轮重复触发复位。
         */
        connectivity_set_state(SYS_CONNECTIVITY_STATE_OFFLINE, now_ms);
        return;
    }
    if ((cellular.registered != BOOL_TRUE) ||
        (_snapshot.registration_lease_valid != BOOL_TRUE))
    {
        if ((cellular.recovery_reason ==
             SYS_CELLULAR_RECOVERY_REGISTRATION) &&
            (_network_recovery_requested != BOOL_TRUE))
        {
            _network_recovery_requested = BOOL_TRUE;
            connectivity_set_state(
                SYS_CONNECTIVITY_STATE_RECOVERING_NETWORK,
                now_ms);
            _snapshot.network_recovery_count++;
            sys_cellular_request_network_recovery(
                SYS_CELLULAR_RECOVERY_REGISTRATION);
        }
        else
        {
            connectivity_set_state(SYS_CONNECTIVITY_STATE_OFFLINE, now_ms);
        }
        return;
    }
    if (cellular.pdp_active != BOOL_TRUE)
    {
        /*
         * PDP 显式命令尚待手册/实机确认。QMTOPEN 进行中只观察；
         * 仅 pdpdeact 明确证据允许一次网络层恢复。
         */
        if ((cellular.recovery_reason ==
             SYS_CELLULAR_RECOVERY_PDP_DEACT) &&
            (_network_recovery_requested != BOOL_TRUE))
        {
            _network_recovery_requested = BOOL_TRUE;
            connectivity_set_state(
                SYS_CONNECTIVITY_STATE_RECOVERING_NETWORK,
                now_ms);
            _snapshot.network_recovery_count++;
            sys_cellular_request_network_recovery(
                SYS_CELLULAR_RECOVERY_PDP_DEACT);
        }
        else
        {
            connectivity_set_state(
                SYS_CONNECTIVITY_STATE_VALIDATING,
                now_ms);
        }
        return;
    }
    _network_recovery_requested = BOOL_FALSE;
    if (_snapshot.transport_ready != BOOL_TRUE)
    {
        if ((mqtt.last_status_code == 2) &&
            (_network_recovery_requested != BOOL_TRUE))
        {
            _network_recovery_requested = BOOL_TRUE;
            _snapshot.network_recovery_count++;
            connectivity_set_state(
                SYS_CONNECTIVITY_STATE_RECOVERING_NETWORK,
                now_ms);
            sys_cellular_request_network_recovery(
                SYS_CELLULAR_RECOVERY_PDP_DEACT);
            return;
        }
        connectivity_set_state(SYS_CONNECTIVITY_STATE_OFFLINE, now_ms);
        return;
    }
    _mqtt_recovery_requested = BOOL_FALSE;
    if ((mqtt.recovery_count >=
         NETWORK_CONNECTIVITY_NETWORK_RECOVERY_COUNT) &&
        (_network_recovery_requested != BOOL_TRUE))
    {
        _network_recovery_requested = BOOL_TRUE;
        _snapshot.network_recovery_count++;
        connectivity_set_state(
            SYS_CONNECTIVITY_STATE_RECOVERING_NETWORK,
            now_ms);
        sys_cellular_request_network_recovery(
            SYS_CELLULAR_RECOVERY_CFUN);
        return;
    }

    if ((_snapshot.last_any_puback_ms == 0U) &&
        (_snapshot.last_probe_ms == 0U))
    {
        connectivity_set_state(
            SYS_CONNECTIVITY_STATE_VALIDATING,
            now_ms);
        if (connectivity_time_due(now_ms, _next_probe_ms) == BOOL_TRUE)
        {
            (void)connectivity_send_probe(now_ms);
        }
        return;
    }

    if ((_snapshot.last_business_puback_ms == 0U) ||
        ((now_ms - _snapshot.last_business_puback_ms) >=
         NETWORK_CONNECTIVITY_PROBE_INTERVAL_MS))
    {
        if (connectivity_time_due(now_ms, _next_probe_ms) == BOOL_TRUE)
        {
            (void)connectivity_send_probe(now_ms);
        }
    }

    if (_snapshot.consecutive_failures >=
        NETWORK_CONNECTIVITY_MQTT_RECOVERY_FAILURES)
    {
        connectivity_set_state(
            SYS_CONNECTIVITY_STATE_RECOVERING_MQTT,
            now_ms);
        if (_mqtt_recovery_requested != BOOL_TRUE)
        {
            _mqtt_recovery_requested = BOOL_TRUE;
            _snapshot.mqtt_recovery_count++;
            _snapshot.consecutive_failures = 0U;
            sys_mqtt_request_recovery();
        }
    }
    else if ((_snapshot.consecutive_failures >=
              NETWORK_CONNECTIVITY_SUSPECT_FAILURES) ||
             (_snapshot.puback_lease_valid != BOOL_TRUE))
    {
        connectivity_set_state(
            SYS_CONNECTIVITY_STATE_SUSPECT,
            now_ms);
        if (connectivity_time_due(now_ms, _next_probe_ms) == BOOL_TRUE)
        {
            (void)connectivity_send_probe(now_ms);
        }
    }
    else if (combined_evidence == BOOL_TRUE)
    {
        connectivity_set_state(SYS_CONNECTIVITY_STATE_ONLINE, now_ms);
    }
    else
    {
        connectivity_set_state(
            SYS_CONNECTIVITY_STATE_VALIDATING,
            now_ms);
    }
}
