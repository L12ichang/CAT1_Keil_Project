#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * 直接编入生产 sys_at_engine.c，以主机桩替代 UART、时间和资源层。
 * 这样测试执行的就是固件中的真实分流/重试代码，而不是 Python 复刻模型。
 */
#define __SYS_AT_ENGINE_H__
#define HW_UART1_H
#define __SYS_TIME_H__

typedef uint8_t u8;
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
    SYS_RESOURCE_COUNT
} sys_resource_id_en;

typedef enum
{
    SYS_RESOURCE_OWNER_NONE = 0,
    SYS_RESOURCE_OWNER_LEGACY_AT = 0x0201
} sys_resource_owner_en;

typedef struct
{
    sys_resource_id_en resource_id;
    u16 owner_id;
    u16 generation;
} sys_resource_token_st;

typedef enum
{
    SYS_AT_STATE_IDLE = 0,
    SYS_AT_STATE_DEQUEUE,
    SYS_AT_STATE_SEND,
    SYS_AT_STATE_WAIT_RESPONSE,
    SYS_AT_STATE_RETRY_WAIT,
    SYS_AT_STATE_COMPLETE,
    SYS_AT_STATE_FAILED
} sys_at_state_en;

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

typedef enum
{
    SYS_AT_PARSE_LINE = 0,
    SYS_AT_PARSE_RAW
} sys_at_parse_mode_en;

typedef void (*sys_at_line_handler_fn)(
    const u8 *line,
    u16 length,
    u16 owner_id,
    void *context);
typedef void (*sys_at_complete_handler_fn)(
    sys_at_result_en result,
    u16 owner_id,
    u16 generation,
    void *context);
typedef void (*sys_at_raw_handler_fn)(
    u8 byte,
    u16 owner_id,
    u16 generation,
    void *context);

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

typedef struct
{
    u32 submitted_count;
    u32 completed_count;
    u32 failed_count;
    u32 timeout_count;
    u32 retry_count;
    u32 urc_count;
    u32 prompt_count;
    u32 line_overflow_count;
    u32 raw_token_error_count;
    u32 queue_full_count;
    u16 active_owner_id;
    u16 active_generation;
    u16 active_attempt;
    u8 queued_count;
    sys_at_state_en state;
    sys_at_parse_mode_en parse_mode;
} sys_at_stats_st;

static u32 host_now_ms;
static boolean_en host_resource_valid = BOOL_TRUE;
static boolean_en host_tx_accept = BOOL_TRUE;
static const u8 *host_rx_data;
static u16 host_rx_length;
static u16 host_rx_index;

u32 sys_time_get_ms(void)
{
    return host_now_ms;
}

boolean_en sys_time_is_due(u32 now_ms, u32 due_ms)
{
    return ((s32)(now_ms - due_ms) >= 0) ? BOOL_TRUE : BOOL_FALSE;
}

boolean_en sys_resource_acquire(
    sys_resource_id_en resource_id,
    u16 owner_id,
    u32 lease_ms,
    sys_resource_token_st *token)
{
    if ((host_resource_valid != BOOL_TRUE) || (lease_ms == 0U))
    {
        return BOOL_FALSE;
    }
    token->resource_id = resource_id;
    token->owner_id = owner_id;
    token->generation = 1U;
    return BOOL_TRUE;
}

boolean_en sys_resource_release(const sys_resource_token_st *token)
{
    (void)token;
    return BOOL_TRUE;
}

boolean_en sys_resource_validate(const sys_resource_token_st *token)
{
    (void)token;
    return host_resource_valid;
}

boolean_en hw_uart1_read_byte(u8 *byte)
{
    if ((byte == NULL) || (host_rx_index >= host_rx_length))
    {
        return BOOL_FALSE;
    }
    *byte = host_rx_data[host_rx_index++];
    return BOOL_TRUE;
}

u16 hw_uart1_write(const u8 *buf, u16 length)
{
    (void)buf;
    return (host_tx_accept == BOOL_TRUE) ? length : 0U;
}

#include "../Core/System/sys_at_engine.c"

static u32 host_response_count;
static u32 host_urc_count;
static u32 host_raw_count;
static u32 host_complete_count;
static sys_at_result_en host_complete_result;
static char host_last_line[64];

static void host_line_handler(
    const u8 *line,
    u16 length,
    u16 owner_id,
    void *context)
{
    u16 copy_length;

    (void)owner_id;
    (void)context;
    copy_length = (length < (u16)(sizeof(host_last_line) - 1U)) ?
        length : (u16)(sizeof(host_last_line) - 1U);
    memcpy(host_last_line, line, copy_length);
    host_last_line[copy_length] = '\0';
    host_response_count++;
}

static void host_urc_handler(
    const u8 *line,
    u16 length,
    u16 owner_id,
    void *context)
{
    (void)owner_id;
    (void)context;
    host_line_handler(line, length, owner_id, context);
    host_response_count--;
    host_urc_count++;
}

static void host_raw_handler(
    u8 byte,
    u16 owner_id,
    u16 generation,
    void *context)
{
    (void)byte;
    (void)owner_id;
    (void)generation;
    (void)context;
    host_raw_count++;
}

static void host_complete_handler(
    sys_at_result_en result,
    u16 owner_id,
    u16 generation,
    void *context)
{
    (void)owner_id;
    (void)generation;
    (void)context;
    host_complete_count++;
    host_complete_result = result;
}

static void host_set_line(const char *line)
{
    _line_length = (u16)strlen(line);
    memcpy(_line, line, _line_length + 1U);
}

static void host_prepare_active(const char *command)
{
    memset(&_active, 0, sizeof(_active));
    strcpy(_active.command, command);
    strcpy(_active.expected_token, "OK");
    strcpy(_active.error_token, "ERROR");
    _active.owner_id = (u16)SYS_RESOURCE_OWNER_LEGACY_AT;
    _active.retry_max = 0U;
    _active.line_handler = host_line_handler;
    _active.complete_handler = host_complete_handler;
    _active_valid = BOOL_TRUE;
    _active_attempt = 1U;
}

static void test_command_name_and_token_normalization(void)
{
    sys_at_request_st request;
    sys_at_request_slot_st slot;

    sys_at_engine_init();
    host_prepare_active("AT+CEREG?\r\n");
    host_set_line("+CEREG: 1");
    assert(sys_at_line_belongs_to_active() == BOOL_TRUE);
    host_set_line("+CEREGFOO: 1");
    assert(sys_at_line_belongs_to_active() == BOOL_FALSE);

    strcpy(_active.command, "AT+QENG=\"servingcell\"\r\n");
    host_set_line("+QENG: \"servingcell\"");
    assert(sys_at_line_belongs_to_active() == BOOL_TRUE);

    memset(&request, 0, sizeof(request));
    request.command = "AT\r\n";
    request.expected_token = "OK\r\n";
    request.error_token = "ERROR\r\n";
    request.timeout_ms = 100U;
    request.owner_id = (u16)SYS_RESOURCE_OWNER_LEGACY_AT;
    assert(sys_at_make_slot(&request, &slot) == BOOL_TRUE);
    assert(strcmp(slot.expected_token, "OK") == 0);
    assert(strcmp(slot.error_token, "ERROR") == 0);
}

static void test_nonplus_urc_numeric_response_and_error_isolation(void)
{
    sys_at_engine_init();
    host_response_count = 0U;
    host_urc_count = 0U;
    host_complete_count = 0U;
    host_last_line[0] = '\0';
    sys_at_engine_set_urc_handler(host_urc_handler, NULL);
    host_prepare_active("AT+CGSN\r\n");

    host_set_line("RDY");
    sys_at_handle_line();
    assert(host_urc_count == 1U);
    assert(host_response_count == 0U);
    assert(_active_valid == BOOL_TRUE);

    host_set_line("867997070000001");
    sys_at_handle_line();
    assert(host_response_count == 1U);
    assert(strcmp(host_last_line, "867997070000001") == 0);

    host_set_line("NO CARRIER");
    sys_at_handle_line();
    assert(host_urc_count == 2U);
    assert(host_response_count == 1U);

    host_set_line("ERROR");
    sys_at_handle_line();
    assert(host_response_count == 1U);
    assert(host_complete_count == 1U);
    assert(host_complete_result == SYS_AT_RESULT_ERROR);

    host_prepare_active("AT+CEREG?\r\n");
    host_set_line("+CME ERROR: 10");
    sys_at_handle_line();
    assert(host_response_count == 1U);
    assert(host_complete_count == 2U);
    assert(host_complete_result == SYS_AT_RESULT_ERROR);
}

static void test_tx_busy_has_finite_attempts(void)
{
    sys_at_request_st request;
    sys_at_stats_st stats;

    sys_at_engine_init();
    host_complete_count = 0U;
    host_complete_result = SYS_AT_RESULT_NONE;
    host_resource_valid = BOOL_TRUE;
    host_tx_accept = BOOL_FALSE;
    host_now_ms = 0U;
    host_rx_data = NULL;
    host_rx_length = 0U;
    host_rx_index = 0U;

    memset(&request, 0, sizeof(request));
    request.command = "AT\r\n";
    request.expected_token = "OK\r\n";
    request.error_token = "ERROR\r\n";
    request.timeout_ms = 200U;
    request.retry_max = 1U;
    request.owner_id = (u16)SYS_RESOURCE_OWNER_LEGACY_AT;
    request.complete_handler = host_complete_handler;
    assert(sys_at_engine_submit(&request) == BOOL_TRUE);

    sys_at_engine_process();
    sys_at_engine_process();
    assert(_stats.state == SYS_AT_STATE_SEND);
    assert(_stats.active_attempt == 1U);

    host_now_ms = 1000U;
    sys_at_engine_process();
    assert(_stats.state == SYS_AT_STATE_RETRY_WAIT);
    assert(_stats.retry_count == 1U);

    host_now_ms = 1100U;
    sys_at_engine_process();
    assert(_stats.state == SYS_AT_STATE_SEND);
    assert(_stats.active_attempt == 2U);

    host_now_ms = 2100U;
    sys_at_engine_process();
    assert(host_complete_count == 1U);
    assert(host_complete_result == SYS_AT_RESULT_TX_ERROR);
    sys_at_engine_get_stats(&stats);
    assert(stats.failed_count == 1U);
    assert(stats.retry_count == 1U);
}

static void test_invalid_raw_token_preserves_current_byte(void)
{
    static const u8 line[] = "RDY\n";

    sys_at_engine_init();
    host_response_count = 0U;
    host_urc_count = 0U;
    host_raw_count = 0U;
    host_resource_valid = BOOL_FALSE;
    host_rx_data = line;
    host_rx_length = (u16)(sizeof(line) - 1U);
    host_rx_index = 0U;
    sys_at_engine_set_urc_handler(host_urc_handler, NULL);
    _parse_mode = SYS_AT_PARSE_RAW;
    _stats.parse_mode = SYS_AT_PARSE_RAW;
    _raw_handler = host_raw_handler;
    _raw_token.resource_id = SYS_RESOURCE_MODEM_EXCLUSIVE;
    _raw_token.owner_id = (u16)SYS_RESOURCE_OWNER_LEGACY_AT;
    _raw_token.generation = 1U;

    sys_at_drain_uart();
    assert(host_raw_count == 0U);
    assert(host_urc_count == 1U);
    assert(strcmp(host_last_line, "RDY") == 0);
    assert(_stats.raw_token_error_count == 1U);
    assert(_parse_mode == SYS_AT_PARSE_LINE);
}

int main(void)
{
    test_command_name_and_token_normalization();
    test_nonplus_urc_numeric_response_and_error_isolation();
    test_tx_busy_has_finite_attempts();
    test_invalid_raw_token_preserves_current_byte();
    puts("phase2 AT engine production-C harness: PASS");
    return 0;
}
