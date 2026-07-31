#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 直接编入生产 sys_connectivity.c，蜂窝/MQTT/时间均由主机桩替代。 */
#define COMMON_H

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

#include "../Core/Config/network_config.h"
#include "../Core/System/sys_cellular.h"
#include "../Core/System/sys_mqtt.h"
#include "../Core/System/sys_connectivity.h"

static u32 host_now_ms;
static sys_cellular_snapshot_st host_cellular;
static sys_mqtt_snapshot_st host_mqtt;
static sys_mqtt_publish_result_st host_results[8];
static u8 host_result_count;
static u8 host_probe_count;
static u8 host_mqtt_recovery_count;
static u8 host_network_recovery_count;
static u8 host_hard_reset_count;

u32 sys_time_get_ms(void);
void sys_cellular_get_snapshot(sys_cellular_snapshot_st *snapshot);
void sys_mqtt_get_snapshot(sys_mqtt_snapshot_st *snapshot);
boolean_en sys_mqtt_get_publish_result(
    sys_mqtt_publish_result_st *result);
boolean_en sys_mqtt_publish(
    const char *topic,
    const u8 *payload,
    u16 payload_length,
    u8 priority,
    u16 source_id,
    u32 request_id);
void sys_mqtt_request_recovery(void);
void sys_cellular_request_network_recovery(
    sys_cellular_recovery_reason_en reason);
void sys_cellular_request_hard_reset(
    sys_cellular_recovery_reason_en reason);

#include "../Core/System/sys_connectivity.c"

u32 sys_time_get_ms(void)
{
    return host_now_ms;
}

void sys_cellular_get_snapshot(sys_cellular_snapshot_st *snapshot)
{
    *snapshot = host_cellular;
}

void sys_mqtt_get_snapshot(sys_mqtt_snapshot_st *snapshot)
{
    *snapshot = host_mqtt;
}

boolean_en sys_mqtt_get_publish_result(
    sys_mqtt_publish_result_st *result)
{
    if (host_result_count == 0U)
    {
        return BOOL_FALSE;
    }
    *result = host_results[0];
    memmove(
        &host_results[0],
        &host_results[1],
        sizeof(host_results[0]) * (host_result_count - 1U));
    host_result_count--;
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
    (void)priority;
    assert(strcmp(topic, "MS/test/dev2plt") == 0);
    assert(payload_length == 1U);
    assert(payload[0] == (u8)'0');
    assert(source_id == NETWORK_SOURCE_CONNECTIVITY_PROBE);
    assert(request_id > 0U);
    host_probe_count++;
    return BOOL_TRUE;
}

void sys_mqtt_request_recovery(void)
{
    host_mqtt_recovery_count++;
}

void sys_cellular_request_network_recovery(
    sys_cellular_recovery_reason_en reason)
{
    assert((reason == SYS_CELLULAR_RECOVERY_REGISTRATION) ||
           (reason == SYS_CELLULAR_RECOVERY_PDP_DEACT));
    host_network_recovery_count++;
}

void sys_cellular_request_hard_reset(
    sys_cellular_recovery_reason_en reason)
{
    (void)reason;
    host_hard_reset_count++;
}

static void host_push_probe_result(sys_mqtt_result_en result)
{
    sys_mqtt_publish_result_st item;

    memset(&item, 0, sizeof(item));
    item.result = result;
    item.source_id = NETWORK_SOURCE_CONNECTIVITY_PROBE;
    item.request_id = 1U;
    item.packet_id = 1U;
    item.session_generation = host_mqtt.session_generation;
    item.timestamp_ms = host_now_ms;
    host_results[host_result_count++] = item;
}

static void host_reset(void)
{
    memset(&host_cellular, 0, sizeof(host_cellular));
    memset(&host_mqtt, 0, sizeof(host_mqtt));
    memset(host_results, 0, sizeof(host_results));
    host_now_ms = 1000U;
    host_result_count = 0U;
    host_probe_count = 0U;
    host_mqtt_recovery_count = 0U;
    host_network_recovery_count = 0U;
    host_hard_reset_count = 0U;

    host_cellular.enabled = BOOL_TRUE;
    host_cellular.at_ready = BOOL_TRUE;
    host_cellular.registered = BOOL_TRUE;
    host_cellular.pdp_active = BOOL_TRUE;
    host_cellular.last_at_ok_ms = host_now_ms;
    host_cellular.last_registered_ms = host_now_ms;
    host_cellular.generation = 1U;
    host_mqtt.enabled = BOOL_TRUE;
    host_mqtt.opened = BOOL_TRUE;
    host_mqtt.connected = BOOL_TRUE;
    host_mqtt.downlink_subscribed = BOOL_TRUE;
    host_mqtt.upgrade_subscribed = BOOL_TRUE;
    host_mqtt.ready = BOOL_TRUE;
    host_mqtt.session_generation = 1U;

    sys_connectivity_init();
    assert(sys_connectivity_set_probe_topic(
        "MS/test/dev2plt") == BOOL_TRUE);
}

static void test_online_lease_and_failure_thresholds(void)
{
    sys_connectivity_snapshot_st snapshot;

    host_reset();
    sys_connectivity_process();
    assert(host_probe_count == 1U);
    sys_connectivity_get_snapshot(&snapshot);
    assert(snapshot.online == BOOL_FALSE);

    host_now_ms += 1U;
    host_push_probe_result(SYS_MQTT_RESULT_SUCCESS);
    sys_connectivity_process();
    sys_connectivity_get_snapshot(&snapshot);
    assert(snapshot.online == BOOL_TRUE);
    assert(snapshot.state == SYS_CONNECTIVITY_STATE_ONLINE);
    assert(snapshot.at_lease_valid == BOOL_TRUE);
    assert(snapshot.registration_lease_valid == BOOL_TRUE);
    assert(snapshot.puback_lease_valid == BOOL_TRUE);

    host_now_ms += NETWORK_CONNECTIVITY_PROBE_INTERVAL_MS;
    sys_connectivity_process();
    assert(host_probe_count == 2U);

    host_push_probe_result(SYS_MQTT_RESULT_TIMEOUT);
    host_push_probe_result(SYS_MQTT_RESULT_TIMEOUT);
    sys_connectivity_process();
    sys_connectivity_get_snapshot(&snapshot);
    assert(snapshot.state == SYS_CONNECTIVITY_STATE_SUSPECT);
    assert(host_mqtt_recovery_count == 0U);

    host_push_probe_result(SYS_MQTT_RESULT_TIMEOUT);
    sys_connectivity_process();
    assert(host_mqtt_recovery_count == 1U);
    sys_connectivity_get_snapshot(&snapshot);
    assert(snapshot.mqtt_recovery_count == 1U);
}

static void test_recovery_edges_do_not_storm(void)
{
    host_reset();
    host_cellular.at_ready = BOOL_FALSE;
    sys_connectivity_process();
    sys_connectivity_process();
    assert(host_hard_reset_count == 0U);

    host_cellular.at_ready = BOOL_TRUE;
    host_cellular.registered = BOOL_FALSE;
    host_cellular.recovery_reason =
        SYS_CELLULAR_RECOVERY_REGISTRATION;
    sys_connectivity_process();
    sys_connectivity_process();
    assert(host_network_recovery_count == 1U);

    host_reset();
    host_cellular.pdp_active = BOOL_FALSE;
    host_cellular.recovery_reason = SYS_CELLULAR_RECOVERY_NONE;
    sys_connectivity_process();
    sys_connectivity_process();
    assert(host_network_recovery_count == 0U);

    host_cellular.recovery_reason =
        SYS_CELLULAR_RECOVERY_PDP_DEACT;
    sys_connectivity_process();
    sys_connectivity_process();
    assert(host_network_recovery_count == 1U);
}

int main(void)
{
    test_online_lease_and_failure_thresholds();
    test_recovery_edges_do_not_storm();
    puts("phase3 connectivity production harness: PASS");
    return 0;
}
