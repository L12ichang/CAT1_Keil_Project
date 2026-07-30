/*************************************************************
程序功能：静态资源所有者、代次和超时租约管理
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.7.31
*************************************************************/
#include "sys_resource.h"
#include "sys_time.h"

#define SYS_RESOURCE_U16_MAX    ((u16)0xFFFFU)

typedef struct
{
    u16 owner_id;
    u16 generation;
    u16 depth;
    u32 acquired_ms;
    u32 lease_due_ms;
} sys_resource_slot_st;

static sys_resource_slot_st _slots[SYS_RESOURCE_COUNT];

static u32 sys_resource_enter_critical(void)
{
    u32 primask;

    primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void sys_resource_exit_critical(u32 primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}

static boolean_en sys_resource_slot_expired(
    const sys_resource_slot_st *slot,
    u32 now_ms)
{
    if ((slot->owner_id != (u16)SYS_RESOURCE_OWNER_NONE) &&
        (sys_time_is_due(now_ms, slot->lease_due_ms) == BOOL_TRUE))
    {
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static void sys_resource_clear_slot(sys_resource_slot_st *slot)
{
    slot->owner_id = (u16)SYS_RESOURCE_OWNER_NONE;
    slot->depth = 0U;
    slot->acquired_ms = 0U;
    slot->lease_due_ms = 0U;
}

void sys_resource_init(void)
{
    u32 primask;

    primask = sys_resource_enter_critical();
    memset(_slots, 0, sizeof(_slots));
    sys_resource_exit_critical(primask);
}

boolean_en sys_resource_acquire(
    sys_resource_id_en resource_id,
    u16 owner_id,
    u32 lease_ms,
    sys_resource_token_st *token)
{
    sys_resource_slot_st *slot;
    u32 now_ms;
    u32 requested_due_ms;
    u32 primask;

    if (((u32)resource_id >= (u32)SYS_RESOURCE_COUNT) ||
        (owner_id == (u16)SYS_RESOURCE_OWNER_NONE) ||
        (lease_ms == 0U) ||
        (token == NULL))
    {
        return BOOL_FALSE;
    }

    now_ms = sys_time_get_ms();
    primask = sys_resource_enter_critical();
    slot = &_slots[resource_id];
    if (sys_resource_slot_expired(slot, now_ms) == BOOL_TRUE)
    {
        sys_resource_clear_slot(slot);
    }

    if ((slot->owner_id != (u16)SYS_RESOURCE_OWNER_NONE) &&
        (slot->owner_id != owner_id))
    {
        sys_resource_exit_critical(primask);
        return BOOL_FALSE;
    }

    requested_due_ms = now_ms + lease_ms;
    if (slot->owner_id == (u16)SYS_RESOURCE_OWNER_NONE)
    {
        slot->generation++;
        if (slot->generation == 0U)
        {
            slot->generation = 1U;
        }
        slot->owner_id = owner_id;
        slot->depth = 1U;
        slot->acquired_ms = now_ms;
        slot->lease_due_ms = requested_due_ms;
    }
    else if (slot->depth < SYS_RESOURCE_U16_MAX)
    {
        slot->depth++;
        if (sys_time_is_due(requested_due_ms,
                            slot->lease_due_ms) == BOOL_TRUE)
        {
            slot->lease_due_ms = requested_due_ms;
        }
    }
    else
    {
        sys_resource_exit_critical(primask);
        return BOOL_FALSE;
    }

    token->resource_id = resource_id;
    token->owner_id = owner_id;
    token->generation = slot->generation;
    sys_resource_exit_critical(primask);
    return BOOL_TRUE;
}

boolean_en sys_resource_release(const sys_resource_token_st *token)
{
    sys_resource_slot_st *slot;
    u32 primask;

    if ((token == NULL) ||
        ((u32)token->resource_id >= (u32)SYS_RESOURCE_COUNT))
    {
        return BOOL_FALSE;
    }

    primask = sys_resource_enter_critical();
    slot = &_slots[token->resource_id];
    if ((slot->owner_id != token->owner_id) ||
        (slot->generation != token->generation) ||
        (slot->depth == 0U))
    {
        sys_resource_exit_critical(primask);
        return BOOL_FALSE;
    }

    slot->depth--;
    if (slot->depth == 0U)
    {
        sys_resource_clear_slot(slot);
    }
    sys_resource_exit_critical(primask);
    return BOOL_TRUE;
}

boolean_en sys_resource_renew(
    const sys_resource_token_st *token,
    u32 lease_ms)
{
    sys_resource_slot_st *slot;
    u32 now_ms;
    u32 requested_due_ms;
    u32 primask;

    if ((token == NULL) || (lease_ms == 0U) ||
        ((u32)token->resource_id >= (u32)SYS_RESOURCE_COUNT))
    {
        return BOOL_FALSE;
    }

    now_ms = sys_time_get_ms();
    primask = sys_resource_enter_critical();
    slot = &_slots[token->resource_id];
    if (sys_resource_slot_expired(slot, now_ms) == BOOL_TRUE)
    {
        sys_resource_clear_slot(slot);
    }
    if ((slot->owner_id != token->owner_id) ||
        (slot->generation != token->generation) ||
        (slot->depth == 0U))
    {
        sys_resource_exit_critical(primask);
        return BOOL_FALSE;
    }

    requested_due_ms = now_ms + lease_ms;
    if (sys_time_is_due(requested_due_ms, slot->lease_due_ms) == BOOL_TRUE)
    {
        slot->lease_due_ms = requested_due_ms;
    }
    sys_resource_exit_critical(primask);
    return BOOL_TRUE;
}

boolean_en sys_resource_validate(const sys_resource_token_st *token)
{
    sys_resource_slot_st *slot;
    u32 now_ms;
    u32 primask;
    boolean_en valid;

    if ((token == NULL) ||
        ((u32)token->resource_id >= (u32)SYS_RESOURCE_COUNT))
    {
        return BOOL_FALSE;
    }

    now_ms = sys_time_get_ms();
    primask = sys_resource_enter_critical();
    slot = &_slots[token->resource_id];
    if (sys_resource_slot_expired(slot, now_ms) == BOOL_TRUE)
    {
        sys_resource_clear_slot(slot);
    }
    valid = ((slot->owner_id == token->owner_id) &&
             (slot->generation == token->generation) &&
             (slot->depth > 0U)) ? BOOL_TRUE : BOOL_FALSE;
    sys_resource_exit_critical(primask);
    return valid;
}

boolean_en sys_resource_get_snapshot(
    sys_resource_id_en resource_id,
    sys_resource_snapshot_st *snapshot)
{
    sys_resource_slot_st *slot;
    u32 now_ms;
    u32 primask;

    if (((u32)resource_id >= (u32)SYS_RESOURCE_COUNT) ||
        (snapshot == NULL))
    {
        return BOOL_FALSE;
    }

    now_ms = sys_time_get_ms();
    primask = sys_resource_enter_critical();
    slot = &_slots[resource_id];
    if (sys_resource_slot_expired(slot, now_ms) == BOOL_TRUE)
    {
        sys_resource_clear_slot(slot);
    }
    snapshot->owner_id = slot->owner_id;
    snapshot->generation = slot->generation;
    snapshot->depth = slot->depth;
    snapshot->acquired_ms = slot->acquired_ms;
    snapshot->lease_due_ms = slot->lease_due_ms;
    sys_resource_exit_critical(primask);
    return BOOL_TRUE;
}
