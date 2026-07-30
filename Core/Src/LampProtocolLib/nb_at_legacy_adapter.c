/*************************************************************
程序功能：阶段2旧 NbDriver/OTA 到统一 AT 引擎的临时适配
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.7.31
注意：阶段3/8在业务状态机迁移后删除本适配层。
*************************************************************/
#include "nb_at_legacy_adapter.h"
#include "hw_uart1.h"
#include "Portable.h"

#define NB_AT_LEGACY_QUEUE_CAPACITY          ((u16)2048U)
#define NB_AT_LEGACY_QUEUE_MASK              ((u16)2047U)
#define NB_AT_LEGACY_RESPONSE_COUNT          ((u8)8U)
#define NB_AT_LEGACY_RESPONSE_CAPACITY       ((u16)160U)
#define NB_AT_LEGACY_U32_MAX                 ((u32)0xFFFFFFFFUL)

typedef struct
{
    u8 data[NB_AT_LEGACY_RESPONSE_CAPACITY];
    u16 length;
} nb_at_legacy_response_st;

static u8 _legacy_queue[NB_AT_LEGACY_QUEUE_CAPACITY];
static u16 _legacy_head;
static u16 _legacy_tail;
static nb_at_legacy_response_st _response_queue[NB_AT_LEGACY_RESPONSE_COUNT];
static u8 _response_head;
static u8 _response_tail;
static boolean_en _transaction_active;
static boolean_en _transaction_done;
static sys_at_result_en _transaction_result;
static u16 _transaction_owner;
static sys_resource_token_st _exclusive_token;
static boolean_en _exclusive_valid;
static boolean_en _raw_mode;
static nb_at_legacy_adapter_stats_st _stats;

volatile uint32 usart_queue_drop_count;

static void nb_at_legacy_increment_saturated(u32 *value)
{
    if (*value < NB_AT_LEGACY_U32_MAX)
    {
        (*value)++;
    }
}

static u16 nb_at_legacy_queue_count(void)
{
    return (u16)((_legacy_head - _legacy_tail) &
                 NB_AT_LEGACY_QUEUE_MASK);
}

static void nb_at_legacy_enqueue_byte(u8 byte)
{
    u16 next_head;
    u16 count;

    next_head = (u16)((_legacy_head + 1U) & NB_AT_LEGACY_QUEUE_MASK);
    if (next_head == _legacy_tail)
    {
        nb_at_legacy_increment_saturated(&_stats.dropped_byte_count);
        if (usart_queue_drop_count < NB_AT_LEGACY_U32_MAX)
        {
            usart_queue_drop_count++;
        }
        return;
    }

    _legacy_queue[_legacy_head] = byte;
    _legacy_head = next_head;
    nb_at_legacy_increment_saturated(&_stats.routed_byte_count);
    count = nb_at_legacy_queue_count();
    if (count > _stats.queue_high_watermark)
    {
        _stats.queue_high_watermark = count;
    }
}

static void nb_at_legacy_urc_handler(
    const u8 *line,
    u16 length,
    u16 owner_id,
    void *context)
{
    u16 index;

    (void)owner_id;
    (void)context;
    for (index = 0U; index < length; index++)
    {
        nb_at_legacy_enqueue_byte(line[index]);
    }
    if (!((length == 1U) && (line[0] == (u8)'>')))
    {
        nb_at_legacy_enqueue_byte((u8)'\r');
        nb_at_legacy_enqueue_byte((u8)'\n');
    }
}

static void nb_at_legacy_response_handler(
    const u8 *line,
    u16 length,
    u16 owner_id,
    void *context)
{
    nb_at_legacy_response_st *response;
    u8 next_head;

    (void)owner_id;
    (void)context;
    next_head = (u8)((_response_head + 1U) %
                     NB_AT_LEGACY_RESPONSE_COUNT);
    if (next_head == _response_tail)
    {
        nb_at_legacy_increment_saturated(&_stats.response_line_drop_count);
        return;
    }

    response = &_response_queue[_response_head];
    if (length >= NB_AT_LEGACY_RESPONSE_CAPACITY)
    {
        length = NB_AT_LEGACY_RESPONSE_CAPACITY - 1U;
    }
    memcpy(response->data, line, length);
    response->data[length] = 0U;
    response->length = length;
    _response_head = next_head;
}

static void nb_at_legacy_complete_handler(
    sys_at_result_en result,
    u16 owner_id,
    u16 generation,
    void *context)
{
    (void)owner_id;
    (void)generation;
    (void)context;
    _transaction_result = result;
    _transaction_done = BOOL_TRUE;
    _transaction_active = BOOL_FALSE;
}

static void nb_at_legacy_raw_handler(
    u8 byte,
    u16 owner_id,
    u16 generation,
    void *context)
{
    (void)owner_id;
    (void)generation;
    (void)context;
    nb_at_legacy_enqueue_byte(byte);
}

void nb_at_legacy_adapter_init(void)
{
    memset(_legacy_queue, 0, sizeof(_legacy_queue));
    memset(_response_queue, 0, sizeof(_response_queue));
    memset(&_stats, 0, sizeof(_stats));
    memset(&_exclusive_token, 0, sizeof(_exclusive_token));
    _legacy_head = 0U;
    _legacy_tail = 0U;
    _response_head = 0U;
    _response_tail = 0U;
    _transaction_active = BOOL_FALSE;
    _transaction_done = BOOL_FALSE;
    _transaction_result = SYS_AT_RESULT_NONE;
    _transaction_owner = (u16)SYS_RESOURCE_OWNER_NONE;
    _exclusive_valid = BOOL_FALSE;
    _raw_mode = BOOL_FALSE;
    usart_queue_drop_count = 0U;
    sys_at_engine_set_urc_handler(nb_at_legacy_urc_handler, NULL);
}

boolean_en nb_at_legacy_adapter_start(
    const char *command,
    u16 command_length,
    const char *expected_token,
    u32 timeout_ms,
    u8 retry_max,
    u16 owner_id,
    boolean_en expect_prompt)
{
    sys_at_request_st request;

    if ((_transaction_active == BOOL_TRUE) ||
        (_transaction_done == BOOL_TRUE))
    {
        return BOOL_FALSE;
    }

    memset(&request, 0, sizeof(request));
    request.command = command;
    request.command_length = command_length;
    request.expected_token = expected_token;
    request.error_token = "ERROR";
    request.timeout_ms = timeout_ms;
    request.retry_max = retry_max;
    request.priority = 1U;
    request.owner_id = owner_id;
    request.expect_prompt = expect_prompt;
    request.line_handler = nb_at_legacy_response_handler;
    request.complete_handler = nb_at_legacy_complete_handler;
    request.context = NULL;
    if (sys_at_engine_submit(&request) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }

    _response_head = 0U;
    _response_tail = 0U;
    _transaction_result = SYS_AT_RESULT_NONE;
    _transaction_owner = owner_id;
    _transaction_active = BOOL_TRUE;
    return BOOL_TRUE;
}

boolean_en nb_at_legacy_adapter_read_response_line(
    u8 *buf,
    u16 capacity,
    u16 *length)
{
    nb_at_legacy_response_st *response;
    u16 copy_length;

    if ((buf == NULL) || (capacity == 0U) || (length == NULL) ||
        (_response_tail == _response_head))
    {
        return BOOL_FALSE;
    }

    response = &_response_queue[_response_tail];
    copy_length = response->length;
    if (copy_length >= capacity)
    {
        copy_length = capacity - 1U;
    }
    memcpy(buf, response->data, copy_length);
    buf[copy_length] = 0U;
    *length = copy_length;
    _response_tail = (u8)((_response_tail + 1U) %
                          NB_AT_LEGACY_RESPONSE_COUNT);
    return BOOL_TRUE;
}

boolean_en nb_at_legacy_adapter_take_result(sys_at_result_en *result)
{
    if ((result == NULL) || (_transaction_done != BOOL_TRUE))
    {
        return BOOL_FALSE;
    }

    *result = _transaction_result;
    _transaction_done = BOOL_FALSE;
    _transaction_result = SYS_AT_RESULT_NONE;
    _transaction_owner = (u16)SYS_RESOURCE_OWNER_NONE;
    return BOOL_TRUE;
}

boolean_en nb_at_legacy_adapter_busy(void)
{
    return ((_transaction_active == BOOL_TRUE) ||
            (_transaction_done == BOOL_TRUE)) ?
        BOOL_TRUE : BOOL_FALSE;
}

void nb_at_legacy_adapter_cancel(u16 owner_id)
{
    if (_transaction_owner != owner_id)
    {
        return;
    }
    (void)sys_at_engine_cancel_owner(owner_id);
    _transaction_active = BOOL_FALSE;
    _transaction_done = BOOL_FALSE;
    _transaction_result = SYS_AT_RESULT_CANCELLED;
    _transaction_owner = (u16)SYS_RESOURCE_OWNER_NONE;
    _response_head = 0U;
    _response_tail = 0U;
}

boolean_en nb_at_legacy_adapter_read_byte(u8 *byte)
{
    if ((byte == NULL) || (_legacy_tail == _legacy_head))
    {
        return BOOL_FALSE;
    }
    *byte = _legacy_queue[_legacy_tail];
    _legacy_tail = (u16)((_legacy_tail + 1U) & NB_AT_LEGACY_QUEUE_MASK);
    return BOOL_TRUE;
}

u16 nb_at_legacy_adapter_read_line(
    u8 *buf,
    u16 *length,
    u16 capacity)
{
    u8 byte;

    if ((buf == NULL) || (length == NULL) || (capacity < 2U))
    {
        return 0U;
    }
    while (nb_at_legacy_adapter_read_byte(&byte) == BOOL_TRUE)
    {
        if (*length >= (capacity - 1U))
        {
            *length = 0U;
        }
        buf[(*length)++] = byte;
        if ((*length >= 2U) &&
            (buf[*length - 2U] == (u8)'\r') &&
            (buf[*length - 1U] == (u8)'\n'))
        {
            if (*length == 2U)
            {
                *length = 0U;
                continue;
            }
            buf[*length] = 0U;
            return *length;
        }
    }
    return 0U;
}

void nb_at_legacy_adapter_route_raw_byte(u8 byte)
{
    nb_at_legacy_enqueue_byte(byte);
}

u8 nb_at_legacy_adapter_send_raw(const u8 *buf, u16 length)
{
    if ((buf == NULL) || (length == 0U))
    {
        return (u8)HAL_ERROR;
    }
    return (hw_uart1_write(buf, length) == length) ?
        (u8)HAL_OK : (u8)HAL_BUSY;
}

boolean_en nb_at_legacy_adapter_begin_exclusive(
    u16 owner_id,
    u32 lease_ms)
{
    if (_exclusive_valid == BOOL_TRUE)
    {
        if (_exclusive_token.owner_id != owner_id)
        {
            return BOOL_FALSE;
        }
        if ((sys_resource_validate(&_exclusive_token) == BOOL_TRUE) &&
            (sys_resource_renew(&_exclusive_token, lease_ms) == BOOL_TRUE))
        {
            return BOOL_TRUE;
        }

        /*
         * 同所有者的旧 token 也可能已经超时失效；撤销引擎中的
         * Raw/armed 状态后重新申请，不能把失效 token 当作成功。
         */
        (void)nb_at_legacy_adapter_leave_raw_mode();
        memset(&_exclusive_token, 0, sizeof(_exclusive_token));
        _exclusive_valid = BOOL_FALSE;
        _raw_mode = BOOL_FALSE;
    }
    if (sys_resource_acquire(SYS_RESOURCE_MODEM_EXCLUSIVE,
                             owner_id,
                             lease_ms,
                             &_exclusive_token) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    _exclusive_valid = BOOL_TRUE;
    return BOOL_TRUE;
}

boolean_en nb_at_legacy_adapter_enter_raw_mode(void)
{
    if (_raw_mode == BOOL_TRUE)
    {
        return BOOL_TRUE;
    }
    if ((_exclusive_valid != BOOL_TRUE) ||
        (sys_at_engine_enter_raw_mode(&_exclusive_token,
                                      nb_at_legacy_raw_handler,
                                      NULL) != BOOL_TRUE))
    {
        return BOOL_FALSE;
    }
    _raw_mode = BOOL_TRUE;
    return BOOL_TRUE;
}

boolean_en nb_at_legacy_adapter_arm_raw_mode(
    const char *trigger_token)
{
    if ((_exclusive_valid != BOOL_TRUE) ||
        (_raw_mode == BOOL_TRUE) ||
        (sys_at_engine_arm_raw_mode(&_exclusive_token,
                                    trigger_token,
                                    nb_at_legacy_raw_handler,
                                    NULL) != BOOL_TRUE))
    {
        return BOOL_FALSE;
    }
    _raw_mode = BOOL_TRUE;
    return BOOL_TRUE;
}

boolean_en nb_at_legacy_adapter_leave_raw_mode(void)
{
    if (_raw_mode != BOOL_TRUE)
    {
        return BOOL_TRUE;
    }
    if (sys_at_engine_leave_raw_mode(&_exclusive_token) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    _raw_mode = BOOL_FALSE;
    return BOOL_TRUE;
}

void nb_at_legacy_adapter_end_exclusive(void)
{
    if (_exclusive_valid == BOOL_TRUE)
    {
        (void)nb_at_legacy_adapter_leave_raw_mode();
        (void)sys_resource_release(&_exclusive_token);
        memset(&_exclusive_token, 0, sizeof(_exclusive_token));
        _exclusive_valid = BOOL_FALSE;
    }
}

boolean_en nb_at_legacy_adapter_has_exclusive(void)
{
    return (_exclusive_valid == BOOL_TRUE) ?
        sys_resource_validate(&_exclusive_token) : BOOL_FALSE;
}

u16 nb_at_legacy_adapter_exclusive_generation(void)
{
    return (_exclusive_valid == BOOL_TRUE) ?
        _exclusive_token.generation : 0U;
}

void nb_at_legacy_adapter_get_stats(
    nb_at_legacy_adapter_stats_st *stats)
{
    if (stats != NULL)
    {
        *stats = _stats;
    }
}
