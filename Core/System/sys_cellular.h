#ifndef __SYS_CELLULAR_H__
#define __SYS_CELLULAR_H__

#include "common.h"

typedef enum
{
    SYS_CELLULAR_STATE_DISABLED = 0,
    SYS_CELLULAR_STATE_AT_PROBE,
    SYS_CELLULAR_STATE_SIM_CHECK,
    SYS_CELLULAR_STATE_READ_IMEI,
    SYS_CELLULAR_STATE_READ_ICCID,
    SYS_CELLULAR_STATE_CEREG_ENABLE,
    SYS_CELLULAR_STATE_NETWORK_REGISTERING,
    SYS_CELLULAR_STATE_NETWORK_HEALTH_CHECK,
    SYS_CELLULAR_STATE_PDP_ACTIVATING,
    SYS_CELLULAR_STATE_NETWORK_READY,
    SYS_CELLULAR_STATE_CFUN_DISABLE,
    SYS_CELLULAR_STATE_CFUN_ENABLE,
    SYS_CELLULAR_STATE_SIM_ERROR_WAIT,
    SYS_CELLULAR_STATE_HARD_RESET_ASSERT,
    SYS_CELLULAR_STATE_HARD_RESET_PWRKEY,
    SYS_CELLULAR_STATE_HARD_RESET_BOOT_WAIT,
    SYS_CELLULAR_STATE_RECOVERY_WAIT
} sys_cellular_state_en;

typedef enum
{
    SYS_CELLULAR_RECOVERY_NONE = 0,
    SYS_CELLULAR_RECOVERY_REGISTRATION,
    SYS_CELLULAR_RECOVERY_PDP_DEACT,
    SYS_CELLULAR_RECOVERY_AT_TIMEOUT,
    SYS_CELLULAR_RECOVERY_SIM,
    SYS_CELLULAR_RECOVERY_CFUN,
    SYS_CELLULAR_RECOVERY_HARD_RESET_LIMIT
} sys_cellular_recovery_reason_en;

typedef struct
{
    sys_cellular_state_en state;
    sys_cellular_recovery_reason_en recovery_reason;
    boolean_en enabled;
    boolean_en at_ready;
    boolean_en sim_ready;
    boolean_en imei_ready;
    boolean_en iccid_ready;
    boolean_en registered;
    boolean_en pdp_active;
    char imei[16];
    char iccid[21];
    s8 cereg_stat;
    s16 rsrp_dbm;
    u32 last_at_ok_ms;
    u32 last_registered_ms;
    u32 last_pdp_active_ms;
    u32 state_since_ms;
    u32 next_action_ms;
    u16 signal_query_generation;
    u16 generation;
    u8 at_probe_failures;
    u8 cfun_attempts;
    u8 hard_resets_in_window;
} sys_cellular_snapshot_st;

extern void sys_cellular_init(void);
extern void sys_cellular_process(void);
extern void sys_cellular_set_enabled(boolean_en enabled);
extern void sys_cellular_request_network_recovery(
    sys_cellular_recovery_reason_en reason);
extern void sys_cellular_request_hard_reset(
    sys_cellular_recovery_reason_en reason);
extern void sys_cellular_notify_transport_opened(void);
extern boolean_en sys_cellular_request_signal_query(void);
extern void sys_cellular_get_snapshot(sys_cellular_snapshot_st *snapshot);
extern void sys_cellular_on_urc(
    const u8 *line,
    u16 length,
    u16 owner_id,
    void *context);

#endif
