#ifndef __SYS_RESOURCE_H__
#define __SYS_RESOURCE_H__

#include "common.h"

typedef enum
{
    SYS_RESOURCE_MODEM_EXCLUSIVE = 0,
    SYS_RESOURCE_FLASH_CONFIG,
    SYS_RESOURCE_FLASH_CALIBRATION,
    SYS_RESOURCE_FLASH_OTA,
    SYS_RESOURCE_PWM_EXCLUSIVE,
    SYS_RESOURCE_PLAN_PAUSE,
    SYS_RESOURCE_REPORT_PAUSE,
    SYS_RESOURCE_COUNT
} sys_resource_id_en;

typedef enum
{
    SYS_RESOURCE_OWNER_NONE = 0,
    SYS_RESOURCE_OWNER_LEGACY_AT = 0x0201,
    SYS_RESOURCE_OWNER_LEGACY_MQTT = 0x0202,
    SYS_RESOURCE_OWNER_OTA = 0x0203
} sys_resource_owner_en;

typedef struct
{
    sys_resource_id_en resource_id;
    u16 owner_id;
    u16 generation;
} sys_resource_token_st;

typedef struct
{
    u16 owner_id;
    u16 generation;
    u16 depth;
    u32 acquired_ms;
    u32 lease_due_ms;
} sys_resource_snapshot_st;

extern void sys_resource_init(void);
extern boolean_en sys_resource_acquire(
    sys_resource_id_en resource_id,
    u16 owner_id,
    u32 lease_ms,
    sys_resource_token_st *token);
extern boolean_en sys_resource_renew(
    const sys_resource_token_st *token,
    u32 lease_ms);
extern boolean_en sys_resource_release(const sys_resource_token_st *token);
extern boolean_en sys_resource_validate(const sys_resource_token_st *token);
extern boolean_en sys_resource_get_snapshot(
    sys_resource_id_en resource_id,
    sys_resource_snapshot_st *snapshot);

#endif
