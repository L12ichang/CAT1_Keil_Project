#ifndef __SYS_CONNECTIVITY_H__
#define __SYS_CONNECTIVITY_H__

#include "common.h"
#include "network_config.h"
#include "sys_mqtt.h"

typedef enum
{
    SYS_CONNECTIVITY_STATE_DISABLED = 0,
    SYS_CONNECTIVITY_STATE_OFFLINE,
    SYS_CONNECTIVITY_STATE_VALIDATING,
    SYS_CONNECTIVITY_STATE_ONLINE,
    SYS_CONNECTIVITY_STATE_SUSPECT,
    SYS_CONNECTIVITY_STATE_RECOVERING_MQTT,
    SYS_CONNECTIVITY_STATE_RECOVERING_NETWORK,
    SYS_CONNECTIVITY_STATE_RECOVERING_MODEM
} sys_connectivity_state_en;

typedef struct
{
    sys_connectivity_state_en state;
    boolean_en enabled;
    boolean_en transport_ready;
    boolean_en online;
    boolean_en at_lease_valid;
    boolean_en registration_lease_valid;
    boolean_en puback_lease_valid;
    u16 session_generation;
    u32 state_since_ms;
    u32 last_probe_ms;
    u32 last_probe_success_ms;
    u32 last_business_puback_ms;
    u32 last_any_puback_ms;
    u32 next_probe_request_id;
    u8 consecutive_failures;
    u8 mqtt_recovery_count;
    u8 network_recovery_count;
    u8 modem_recovery_count;
} sys_connectivity_snapshot_st;

extern void sys_connectivity_init(void);
extern void sys_connectivity_process(void);
extern void sys_connectivity_set_enabled(boolean_en enabled);
extern boolean_en sys_connectivity_set_probe_topic(const char *topic);
extern boolean_en sys_connectivity_get_publish_result(
    sys_mqtt_publish_result_st *result);
extern void sys_connectivity_get_snapshot(
    sys_connectivity_snapshot_st *snapshot);

#endif
