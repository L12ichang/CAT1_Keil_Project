#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 直接编入生产 sys_cellular.c，外部 AT/时间/IO/事件全部使用主机桩。 */
#define COMMON_H
#define __SYS_AT_ENGINE_H__
#define __SYS_EVENT_H__
#define __SYS_TIME_H__
#define HW_4G_IO_H

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

#define SYS_RESOURCE_OWNER_NONE       ((u16)0U)
#define SYS_RESOURCE_OWNER_CELLULAR   ((u16)0x0301U)

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
    const u8 *, u16, u16, void *);
typedef void (*sys_at_complete_handler_fn)(
    sys_at_result_en, u16, u16, void *);

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

typedef enum
{
    SYS_EVENT_NONE = 0,
    SYS_EVENT_MODEM_READY,
    SYS_EVENT_MODEM_NO_RESPONSE,
    SYS_EVENT_NETWORK_REGISTERED,
    SYS_EVENT_NETWORK_LOST
} sys_event_type_en;

typedef struct
{
    sys_event_type_en type;
    u16 source_id;
    u16 data_id;
    u32 timestamp_ms;
    u32 value;
} sys_event_st;

boolean_en sys_at_engine_submit(const sys_at_request_st *request);
boolean_en sys_at_engine_cancel_owner(u16 owner_id);
boolean_en sys_at_engine_add_urc_handler(
    sys_at_line_handler_fn handler,
    void *context);
boolean_en sys_event_post(const sys_event_st *event);
u32 sys_time_get_ms(void);
void pwr_on(void);
void pwr_off(void);
void reset_on(void);
void reset_off(void);

#include "../Core/System/sys_cellular.h"
#include "../Core/Config/network_config.h"
#include "../Core/System/sys_cellular.c"

static u32 host_now_ms;
static sys_at_request_st host_request;
static boolean_en host_request_valid;
static u8 host_reset_on_count;
static u8 host_reset_off_count;
static u8 host_pwr_on_count;
static u8 host_pwr_off_count;

boolean_en sys_at_engine_submit(const sys_at_request_st *request)
{
    assert(request != NULL);
    host_request = *request;
    host_request_valid = BOOL_TRUE;
    return BOOL_TRUE;
}

boolean_en sys_at_engine_cancel_owner(u16 owner_id)
{
    (void)owner_id;
    host_request_valid = BOOL_FALSE;
    return BOOL_TRUE;
}

boolean_en sys_at_engine_add_urc_handler(
    sys_at_line_handler_fn handler,
    void *context)
{
    (void)context;
    assert(handler == sys_cellular_on_urc);
    return BOOL_TRUE;
}

boolean_en sys_event_post(const sys_event_st *event)
{
    assert(event != NULL);
    return BOOL_TRUE;
}

u32 sys_time_get_ms(void)
{
    return host_now_ms;
}

void pwr_on(void)
{
    host_pwr_on_count++;
}

void pwr_off(void)
{
    host_pwr_off_count++;
}

void reset_on(void)
{
    host_reset_on_count++;
}

void reset_off(void)
{
    host_reset_off_count++;
}

static void host_process_due(void)
{
    sys_cellular_process();
}

static void host_complete(const char *line, sys_at_result_en result)
{
    sys_at_request_st request;

    assert(host_request_valid == BOOL_TRUE);
    request = host_request;
    host_request_valid = BOOL_FALSE;
    if ((line != NULL) && (request.line_handler != NULL))
    {
        request.line_handler(
            (const u8 *)line,
            (u16)strlen(line),
            SYS_RESOURCE_OWNER_CELLULAR,
            request.context);
    }
    request.complete_handler(
        result,
        SYS_RESOURCE_OWNER_CELLULAR,
        1U,
        request.context);
}

static void test_acquisition_and_recovery(void)
{
    sys_cellular_snapshot_st snapshot;

    host_now_ms = 1000U;
    host_request_valid = BOOL_FALSE;
    host_reset_on_count = 0U;
    host_reset_off_count = 0U;
    host_pwr_on_count = 0U;
    host_pwr_off_count = 0U;
    sys_cellular_init();

    host_process_due();
    assert(strcmp(host_request.command, "AT\r\n") == 0);
    assert(host_request.retry_max == NETWORK_CELLULAR_AT_RETRY_MAX);
    host_complete(NULL, SYS_AT_RESULT_OK);

    host_process_due();
    assert(strcmp(host_request.command, "AT+CPIN?\r\n") == 0);
    host_complete("+CPIN: READY", SYS_AT_RESULT_OK);

    host_process_due();
    host_complete("864501234567890", SYS_AT_RESULT_OK);
    host_process_due();
    host_complete("+QCCID: 89860412345678901234", SYS_AT_RESULT_OK);
    host_process_due();
    assert(strcmp(host_request.command, "AT+CEREG=2\r\n") == 0);
    host_complete(NULL, SYS_AT_RESULT_OK);
    host_process_due();
    host_complete("+CEREG: 2,1", SYS_AT_RESULT_OK);
    host_process_due();

    sys_cellular_get_snapshot(&snapshot);
    assert(snapshot.at_ready == BOOL_TRUE);
    assert(snapshot.sim_ready == BOOL_TRUE);
    assert(snapshot.imei_ready == BOOL_TRUE);
    assert(snapshot.iccid_ready == BOOL_TRUE);
    assert(snapshot.registered == BOOL_TRUE);
    assert(snapshot.pdp_active == BOOL_FALSE);
    sys_cellular_notify_transport_opened();
    sys_cellular_get_snapshot(&snapshot);
    assert(snapshot.pdp_active == BOOL_TRUE);

    sys_cellular_on_urc(
        (const u8 *)"+QIURC: \"pdpdeact\"",
        (u16)strlen("+QIURC: \"pdpdeact\""),
        0U,
        NULL);
    sys_cellular_get_snapshot(&snapshot);
    assert(snapshot.pdp_active == BOOL_FALSE);

    sys_cellular_request_network_recovery(
        SYS_CELLULAR_RECOVERY_PDP_DEACT);
    host_process_due();
    assert(strcmp(host_request.command, "AT+CFUN=0\r\n") == 0);
    host_complete(NULL, SYS_AT_RESULT_OK);
    host_now_ms += 1000U;
    host_process_due();
    assert(strcmp(host_request.command, "AT+CFUN=1\r\n") == 0);
    host_complete(NULL, SYS_AT_RESULT_OK);
}

static void test_sim_error_and_hard_reset_limits(void)
{
    sys_cellular_snapshot_st snapshot;
    u8 reset_count;

    host_now_ms = 100000U;
    sys_cellular_init();
    host_process_due();
    host_complete(NULL, SYS_AT_RESULT_OK);
    host_process_due();
    host_complete("+CME ERROR: 10", SYS_AT_RESULT_ERROR);
    sys_cellular_get_snapshot(&snapshot);
    assert(snapshot.state == SYS_CELLULAR_STATE_SIM_ERROR_WAIT);
    assert(snapshot.recovery_reason == SYS_CELLULAR_RECOVERY_SIM);

    _snapshot.state = SYS_CELLULAR_STATE_AT_PROBE;
    _snapshot.hard_resets_in_window = 0U;
    _last_hard_reset_ms =
        host_now_ms - NETWORK_CELLULAR_RESET_COOLDOWN_MS;
    reset_count = host_reset_on_count;
    sys_cellular_request_hard_reset(
        SYS_CELLULAR_RECOVERY_AT_TIMEOUT);
    assert(host_reset_on_count == (u8)(reset_count + 1U));
    sys_cellular_request_hard_reset(
        SYS_CELLULAR_RECOVERY_AT_TIMEOUT);
    assert(host_reset_on_count == (u8)(reset_count + 1U));

    host_now_ms += NETWORK_CELLULAR_RESET_TO_PWRKEY_MS;
    host_process_due();
    assert(host_pwr_on_count > 0U);
    host_now_ms += NETWORK_CELLULAR_RESET_ASSERT_MS -
        NETWORK_CELLULAR_RESET_TO_PWRKEY_MS;
    host_process_due();
    assert(host_reset_off_count > 0U);
    host_now_ms = _pwrkey_started_ms +
        NETWORK_CELLULAR_PWRKEY_PULSE_MS;
    host_process_due();
    assert(host_pwr_off_count > 0U);

    _snapshot.state = SYS_CELLULAR_STATE_AT_PROBE;
    _snapshot.hard_resets_in_window =
        NETWORK_CELLULAR_RESET_MAX_PER_WINDOW;
    _last_hard_reset_ms =
        host_now_ms - NETWORK_CELLULAR_RESET_COOLDOWN_MS;
    sys_cellular_request_hard_reset(
        SYS_CELLULAR_RECOVERY_AT_TIMEOUT);
    sys_cellular_get_snapshot(&snapshot);
    assert(snapshot.state == SYS_CELLULAR_STATE_RECOVERY_WAIT);
    assert(snapshot.recovery_reason ==
        SYS_CELLULAR_RECOVERY_HARD_RESET_LIMIT);
}

int main(void)
{
    test_acquisition_and_recovery();
    test_sim_error_and_hard_reset_limits();
    puts("phase3 cellular production harness: PASS");
    return 0;
}
