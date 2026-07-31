#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 直接执行生产 sys_resource.c，验证续租不会增加递归深度或复活旧代次。 */
#define __SYS_RESOURCE_H__
#define __SYS_TIME_H__

typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;

typedef enum
{
    BOOL_FALSE = 0,
    BOOL_TRUE = 1
} boolean_en;

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

static u32 host_now_ms;

static u32 __get_PRIMASK(void)
{
    return 0U;
}

static void __disable_irq(void)
{
}

static void __enable_irq(void)
{
}

u32 sys_time_get_ms(void)
{
    return host_now_ms;
}

boolean_en sys_time_is_due(u32 now_ms, u32 due_ms)
{
    return ((s32)(now_ms - due_ms) >= 0) ? BOOL_TRUE : BOOL_FALSE;
}

#include "../Core/System/sys_resource.c"

int main(void)
{
    sys_resource_token_st token;
    sys_resource_snapshot_st before;
    sys_resource_snapshot_st after;

    host_now_ms = 0U;
    sys_resource_init();
    assert(sys_resource_acquire(
        SYS_RESOURCE_MODEM_EXCLUSIVE,
        (u16)SYS_RESOURCE_OWNER_OTA,
        100U,
        &token) == BOOL_TRUE);
    assert(sys_resource_get_snapshot(
        SYS_RESOURCE_MODEM_EXCLUSIVE,
        &before) == BOOL_TRUE);
    assert(before.depth == 1U);
    assert(before.lease_due_ms == 100U);

    host_now_ms = 50U;
    assert(sys_resource_validate(&token) == BOOL_TRUE);
    assert(sys_resource_renew(&token, 100U) == BOOL_TRUE);
    assert(sys_resource_get_snapshot(
        SYS_RESOURCE_MODEM_EXCLUSIVE,
        &after) == BOOL_TRUE);
    assert(after.owner_id == before.owner_id);
    assert(after.generation == before.generation);
    assert(after.depth == before.depth);
    assert(after.lease_due_ms == 150U);

    host_now_ms = 151U;
    assert(sys_resource_validate(&token) == BOOL_FALSE);
    assert(sys_resource_renew(&token, 100U) == BOOL_FALSE);
    puts("phase2 resource production-C harness: PASS");
    return 0;
}
