#ifndef __SYS_AT_ENGINE_H__
#define __SYS_AT_ENGINE_H__

#include "common.h"
#include "sys_resource.h"

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

extern void sys_at_engine_init(void);
extern void sys_at_engine_process(void);
extern boolean_en sys_at_engine_submit(const sys_at_request_st *request);
extern boolean_en sys_at_engine_busy(void);
extern boolean_en sys_at_engine_cancel_owner(u16 owner_id);
extern void sys_at_engine_set_urc_handler(
    sys_at_line_handler_fn handler,
    void *context);
extern boolean_en sys_at_engine_enter_raw_mode(
    const sys_resource_token_st *token,
    sys_at_raw_handler_fn handler,
    void *context);
extern boolean_en sys_at_engine_arm_raw_mode(
    const sys_resource_token_st *token,
    const char *trigger_token,
    sys_at_raw_handler_fn handler,
    void *context);
extern boolean_en sys_at_engine_leave_raw_mode(
    const sys_resource_token_st *token);
extern void sys_at_engine_get_stats(sys_at_stats_st *stats);

#endif
