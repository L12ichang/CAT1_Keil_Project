/*************************************************************
程序功能：UART1 单事务 AT 引擎、请求队列及响应/URC/Raw 分流
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.7.31
*************************************************************/
#include "sys_at_engine.h"
#include "hw_uart1.h"
#include "sys_time.h"

#define SYS_AT_QUEUE_CAPACITY          ((u8)6U)
#define SYS_AT_COMMAND_CAPACITY        ((u16)384U)
#define SYS_AT_EXPECTED_CAPACITY       ((u16)64U)
#define SYS_AT_ERROR_CAPACITY          ((u16)32U)
#define SYS_AT_LINE_CAPACITY           ((u16)640U)
#define SYS_AT_RAW_TRIGGER_CAPACITY    ((u16)32U)
#define SYS_AT_RX_BUDGET               ((u16)256U)
#define SYS_AT_RETRY_DELAY_MS          ((u32)100U)
#define SYS_AT_SEND_WAIT_MS            ((u32)1000U)
#define SYS_AT_U32_MAX                 ((u32)0xFFFFFFFFUL)

typedef struct
{
    char command[SYS_AT_COMMAND_CAPACITY];
    u16 command_length;
    char expected_token[SYS_AT_EXPECTED_CAPACITY];
    char error_token[SYS_AT_ERROR_CAPACITY];
    u32 timeout_ms;
    u8 retry_max;
    u8 priority;
    u16 owner_id;
    boolean_en expect_prompt;
    sys_at_line_handler_fn line_handler;
    sys_at_complete_handler_fn complete_handler;
    void *context;
} sys_at_request_slot_st;

static sys_at_request_slot_st _queue[SYS_AT_QUEUE_CAPACITY];
static u8 _queue_count;
static sys_at_request_slot_st _active;
static boolean_en _active_valid;
static u16 _active_attempt;
static u32 _state_since_ms;
static u32 _deadline_ms;
static sys_resource_token_st _active_resource_token;
static boolean_en _active_resource_valid;
static char _line[SYS_AT_LINE_CAPACITY];
static u16 _line_length;
static sys_at_line_handler_fn _urc_handler;
static void *_urc_context;
static sys_at_parse_mode_en _parse_mode;
static sys_resource_token_st _raw_token;
static sys_at_raw_handler_fn _raw_handler;
static void *_raw_context;
static char _raw_trigger[SYS_AT_RAW_TRIGGER_CAPACITY];
static boolean_en _raw_armed;
static sys_at_stats_st _stats;

static void sys_at_increment_saturated(u32 *value)
{
    if (*value < SYS_AT_U32_MAX)
    {
        (*value)++;
    }
}

static boolean_en sys_at_copy_text(
    char *dst,
    u16 capacity,
    const char *src,
    u16 source_length,
    u16 *copied_length)
{
    u16 length;

    if ((dst == NULL) || (capacity == 0U))
    {
        return BOOL_FALSE;
    }
    dst[0] = '\0';
    if (src == NULL)
    {
        if (copied_length != NULL)
        {
            *copied_length = 0U;
        }
        return BOOL_TRUE;
    }

    length = source_length;
    if (length == 0U)
    {
        length = (u16)strlen(src);
    }
    if (length >= capacity)
    {
        return BOOL_FALSE;
    }
    memcpy(dst, src, length);
    dst[length] = '\0';
    if (copied_length != NULL)
    {
        *copied_length = length;
    }
    return BOOL_TRUE;
}

static void sys_at_trim_line_end(char *text)
{
    u16 length;

    if (text == NULL)
    {
        return;
    }
    length = (u16)strlen(text);
    while ((length > 0U) &&
           ((text[length - 1U] == '\r') ||
            (text[length - 1U] == '\n')))
    {
        text[--length] = '\0';
    }
}

static boolean_en sys_at_make_slot(
    const sys_at_request_st *request,
    sys_at_request_slot_st *slot)
{
    if ((request == NULL) || (slot == NULL) ||
        (request->command == NULL) ||
        (request->owner_id == (u16)SYS_RESOURCE_OWNER_NONE) ||
        (request->timeout_ms == 0U))
    {
        return BOOL_FALSE;
    }

    memset(slot, 0, sizeof(*slot));
    if (sys_at_copy_text(slot->command,
                         SYS_AT_COMMAND_CAPACITY,
                         request->command,
                         request->command_length,
                         &slot->command_length) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (sys_at_copy_text(slot->expected_token,
                         SYS_AT_EXPECTED_CAPACITY,
                         request->expected_token,
                         0U,
                         NULL) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    sys_at_trim_line_end(slot->expected_token);
    if (sys_at_copy_text(slot->error_token,
                         SYS_AT_ERROR_CAPACITY,
                         request->error_token,
                         0U,
                         NULL) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    sys_at_trim_line_end(slot->error_token);
    if (slot->expected_token[0] == '\0')
    {
        memcpy(slot->expected_token, "OK", 3U);
    }
    if (slot->error_token[0] == '\0')
    {
        memcpy(slot->error_token, "ERROR", 6U);
    }
    slot->timeout_ms = request->timeout_ms;
    slot->retry_max = request->retry_max;
    slot->priority = request->priority;
    slot->owner_id = request->owner_id;
    slot->expect_prompt = request->expect_prompt;
    slot->line_handler = request->line_handler;
    slot->complete_handler = request->complete_handler;
    slot->context = request->context;
    return BOOL_TRUE;
}

static void sys_at_remove_queue_at(u8 index)
{
    u8 move_index;

    for (move_index = index; (move_index + 1U) < _queue_count; move_index++)
    {
        _queue[move_index] = _queue[move_index + 1U];
    }
    if (_queue_count > 0U)
    {
        _queue_count--;
    }
}

static boolean_en sys_at_dequeue_highest(sys_at_request_slot_st *slot)
{
    u8 index;
    u8 selected;

    if ((_queue_count == 0U) || (slot == NULL))
    {
        return BOOL_FALSE;
    }

    selected = 0U;
    for (index = 1U; index < _queue_count; index++)
    {
        if (_queue[index].priority > _queue[selected].priority)
        {
            selected = index;
        }
    }
    *slot = _queue[selected];
    sys_at_remove_queue_at(selected);
    return BOOL_TRUE;
}

static boolean_en sys_at_line_contains(const char *token)
{
    if ((token == NULL) || (token[0] == '\0'))
    {
        return BOOL_FALSE;
    }
    return (strstr(_line, token) != NULL) ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en sys_at_line_belongs_to_active(void)
{
    const char *command_name;
    const char *line_name;
    u16 name_length;

    if (_active_valid != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (sys_at_line_contains(_active.expected_token) == BOOL_TRUE)
    {
        return BOOL_TRUE;
    }

    command_name = strstr(_active.command, "AT+");
    line_name = (_line[0] == '+') ? _line : NULL;
    if ((command_name == NULL) || (line_name == NULL))
    {
        return BOOL_FALSE;
    }
    /* "AT+CEREG" 要与 "+CEREG" 对齐，跳过 A/T 而保留加号。 */
    command_name += 2;
    name_length = 0U;
    while ((command_name[name_length] != '\0') &&
           (command_name[name_length] != '=') &&
           (command_name[name_length] != '?') &&
           (command_name[name_length] != '\r') &&
           (command_name[name_length] != '\n'))
    {
        name_length++;
    }
    if (name_length == 0U)
    {
        return BOOL_FALSE;
    }
    if (strncmp(line_name, command_name, name_length) != 0)
    {
        return BOOL_FALSE;
    }
    return ((line_name[name_length] == '\0') ||
            (line_name[name_length] == ':') ||
            (line_name[name_length] == ' ')) ?
        BOOL_TRUE : BOOL_FALSE;
}

static boolean_en sys_at_line_is_known_nonplus_urc(void)
{
    static const char * const urc_lines[] =
    {
        "RDY",
        "POWERED DOWN",
        "NORMAL POWER DOWN",
        "PB DONE",
        "SMS DONE",
        "APP RDY",
        "Call Ready",
        "SMS Ready",
        "NO CARRIER",
        "NO ANSWER",
        "NO DIALTONE",
        "BUSY",
        "RING"
    };
    u16 index;

    if (_line[0] == '^')
    {
        return BOOL_TRUE;
    }
    for (index = 0U;
         index < (u16)(sizeof(urc_lines) / sizeof(urc_lines[0]));
         index++)
    {
        if (strcmp(_line, urc_lines[index]) == 0)
        {
            return BOOL_TRUE;
        }
    }
    return BOOL_FALSE;
}

static void sys_at_route_urc(const u8 *line, u16 length)
{
    if ((_urc_handler != NULL) && (length > 0U))
    {
        _urc_handler(line,
                     length,
                     (u16)SYS_RESOURCE_OWNER_NONE,
                     _urc_context);
    }
    sys_at_increment_saturated(&_stats.urc_count);
}

static void sys_at_release_active_resource(void)
{
    if (_active_resource_valid == BOOL_TRUE)
    {
        (void)sys_resource_release(&_active_resource_token);
        _active_resource_valid = BOOL_FALSE;
    }
}

static void sys_at_finish(sys_at_result_en result)
{
    sys_at_complete_handler_fn complete_handler;
    void *context;
    u16 owner_id;
    u16 generation;

    complete_handler = _active.complete_handler;
    context = _active.context;
    owner_id = _active.owner_id;
    generation = _active_resource_valid ?
        _active_resource_token.generation : 0U;

    if (result == SYS_AT_RESULT_OK)
    {
        sys_at_increment_saturated(&_stats.completed_count);
        _stats.state = SYS_AT_STATE_COMPLETE;
    }
    else
    {
        sys_at_increment_saturated(&_stats.failed_count);
        _stats.state = SYS_AT_STATE_FAILED;
    }
    sys_at_release_active_resource();
    _active_valid = BOOL_FALSE;
    _stats.active_owner_id = 0U;
    _stats.active_generation = 0U;
    _stats.active_attempt = 0U;
    _line_length = 0U;
    _line[0] = '\0';

    if (complete_handler != NULL)
    {
        complete_handler(result, owner_id, generation, context);
    }
}

static void sys_at_retry_or_finish(sys_at_result_en result)
{
    if (_active_attempt <= _active.retry_max)
    {
        sys_at_increment_saturated(&_stats.retry_count);
        _state_since_ms = sys_time_get_ms();
        _deadline_ms = _state_since_ms + SYS_AT_RETRY_DELAY_MS;
        _stats.state = SYS_AT_STATE_RETRY_WAIT;
    }
    else
    {
        if (result == SYS_AT_RESULT_TIMEOUT)
        {
            sys_at_increment_saturated(&_stats.timeout_count);
        }
        sys_at_finish(result);
    }
}

static void sys_at_handle_line(void)
{
    boolean_en belongs_to_active;
    boolean_en error_line;
    boolean_en known_nonplus_urc;
    boolean_en raw_triggered;

    if (_line_length == 0U)
    {
        return;
    }

    belongs_to_active = sys_at_line_belongs_to_active();
    known_nonplus_urc = sys_at_line_is_known_nonplus_urc();
    raw_triggered = ((_raw_armed == BOOL_TRUE) &&
                     (strstr(_line, _raw_trigger) != NULL)) ?
        BOOL_TRUE : BOOL_FALSE;

    /*
     * 不带 '+' 的异步通知必须先于活动事务分流；纯数字 IMEI 不在
     * 此白名单中，仍会作为当前 AT 命令的普通响应行交付。
     */
    if (known_nonplus_urc == BOOL_TRUE)
    {
        sys_at_route_urc((const u8 *)_line, _line_length);
        return;
    }

    error_line = ((_active_valid == BOOL_TRUE) &&
        (((sys_at_line_contains(_active.error_token) == BOOL_TRUE) &&
          ((_line[0] != '+') || (belongs_to_active == BOOL_TRUE))) ||
         (sys_at_line_contains("+CME ERROR:") == BOOL_TRUE))) ?
        BOOL_TRUE : BOOL_FALSE;
    if ((_active_valid == BOOL_TRUE) &&
        (_active.line_handler != NULL) &&
        (error_line != BOOL_TRUE) &&
        ((_line[0] != '+') || (belongs_to_active == BOOL_TRUE)))
    {
        _active.line_handler((const u8 *)_line,
                             _line_length,
                             _active.owner_id,
                             _active.context);
    }

    if ((_active_valid == BOOL_TRUE) &&
        (sys_at_line_contains(_active.expected_token) == BOOL_TRUE))
    {
        sys_at_finish(SYS_AT_RESULT_OK);
        if (raw_triggered != BOOL_TRUE)
        {
            return;
        }
    }
    if (error_line == BOOL_TRUE)
    {
        sys_at_retry_or_finish(SYS_AT_RESULT_ERROR);
        return;
    }

    if ((_active_valid != BOOL_TRUE) ||
        ((_line[0] == '+') && (belongs_to_active != BOOL_TRUE)))
    {
        sys_at_route_urc((const u8 *)_line, _line_length);
    }
    if (raw_triggered == BOOL_TRUE)
    {
        _raw_armed = BOOL_FALSE;
        _raw_trigger[0] = '\0';
        _parse_mode = SYS_AT_PARSE_RAW;
        _stats.parse_mode = SYS_AT_PARSE_RAW;
    }
}

static void sys_at_handle_prompt(void)
{
    static const u8 prompt[] = ">";

    sys_at_increment_saturated(&_stats.prompt_count);
    if ((_active_valid == BOOL_TRUE) && (_active.line_handler != NULL))
    {
        _active.line_handler(prompt, 1U, _active.owner_id, _active.context);
    }
    if ((_active_valid == BOOL_TRUE) &&
        ((_active.expect_prompt == BOOL_TRUE) ||
         (strcmp(_active.expected_token, ">") == 0)))
    {
        sys_at_finish(SYS_AT_RESULT_OK);
    }
    else
    {
        sys_at_route_urc(prompt, 1U);
    }
}

static void sys_at_parse_byte(u8 byte)
{
    if ((byte == (u8)'>') && (_line_length == 0U))
    {
        sys_at_handle_prompt();
        return;
    }
    if (byte == (u8)'\r')
    {
        return;
    }
    if (byte == (u8)'\n')
    {
        if (_line_length > 0U)
        {
            _line[_line_length] = '\0';
            sys_at_handle_line();
        }
        _line_length = 0U;
        _line[0] = '\0';
        return;
    }
    if (_line_length < (SYS_AT_LINE_CAPACITY - 1U))
    {
        _line[_line_length++] = (char)byte;
    }
    else
    {
        _line_length = 0U;
        _line[0] = '\0';
        sys_at_increment_saturated(&_stats.line_overflow_count);
    }
}

static void sys_at_recover_from_invalid_raw_token(void)
{
    _parse_mode = SYS_AT_PARSE_LINE;
    _stats.parse_mode = SYS_AT_PARSE_LINE;
    _raw_handler = NULL;
    _raw_context = NULL;
    _raw_trigger[0] = '\0';
    _raw_armed = BOOL_FALSE;
    memset(&_raw_token, 0, sizeof(_raw_token));
    sys_at_increment_saturated(&_stats.raw_token_error_count);
}

static void sys_at_drain_uart(void)
{
    u8 byte;
    u16 budget;

    budget = 0U;
    while ((budget < SYS_AT_RX_BUDGET) &&
           (hw_uart1_read_byte(&byte) == BOOL_TRUE))
    {
        if (_parse_mode == SYS_AT_PARSE_RAW)
        {
            if ((_raw_handler != NULL) &&
                (sys_resource_validate(&_raw_token) == BOOL_TRUE))
            {
                _raw_handler(byte,
                             _raw_token.owner_id,
                             _raw_token.generation,
                             _raw_context);
            }
            else
            {
                /*
                 * Raw token 失效时切回行模式，并把已取出的当前字节继续
                 * 交给行解析器，不能静默吞掉下一条响应/URC 的首字节。
                 */
                sys_at_recover_from_invalid_raw_token();
                sys_at_parse_byte(byte);
            }
        }
        else
        {
            sys_at_parse_byte(byte);
        }
        budget++;
    }
}

void sys_at_engine_init(void)
{
    memset(_queue, 0, sizeof(_queue));
    memset(&_active, 0, sizeof(_active));
    memset(&_stats, 0, sizeof(_stats));
    memset(&_active_resource_token, 0, sizeof(_active_resource_token));
    memset(&_raw_token, 0, sizeof(_raw_token));
    _queue_count = 0U;
    _active_valid = BOOL_FALSE;
    _active_attempt = 0U;
    _active_resource_valid = BOOL_FALSE;
    _line_length = 0U;
    _line[0] = '\0';
    _urc_handler = NULL;
    _urc_context = NULL;
    _parse_mode = SYS_AT_PARSE_LINE;
    _raw_handler = NULL;
    _raw_context = NULL;
    _raw_trigger[0] = '\0';
    _raw_armed = BOOL_FALSE;
    _stats.state = SYS_AT_STATE_IDLE;
    _stats.parse_mode = SYS_AT_PARSE_LINE;
}

boolean_en sys_at_engine_submit(const sys_at_request_st *request)
{
    sys_at_request_slot_st slot;

    if ((_parse_mode != SYS_AT_PARSE_LINE) ||
        (_queue_count >= SYS_AT_QUEUE_CAPACITY) ||
        (sys_at_make_slot(request, &slot) != BOOL_TRUE))
    {
        if (_queue_count >= SYS_AT_QUEUE_CAPACITY)
        {
            sys_at_increment_saturated(&_stats.queue_full_count);
        }
        return BOOL_FALSE;
    }

    _queue[_queue_count++] = slot;
    _stats.queued_count = _queue_count;
    sys_at_increment_saturated(&_stats.submitted_count);
    return BOOL_TRUE;
}

void sys_at_engine_process(void)
{
    u32 now_ms;
    u32 lease_ms;

    sys_at_drain_uart();
    now_ms = sys_time_get_ms();

    switch (_stats.state)
    {
        case SYS_AT_STATE_IDLE:
        case SYS_AT_STATE_COMPLETE:
        case SYS_AT_STATE_FAILED:
            _stats.state = SYS_AT_STATE_DEQUEUE;
            break;

        case SYS_AT_STATE_DEQUEUE:
            if ((_parse_mode == SYS_AT_PARSE_LINE) &&
                (sys_at_dequeue_highest(&_active) == BOOL_TRUE))
            {
                _active_valid = BOOL_TRUE;
                _active_attempt = 0U;
                _stats.queued_count = _queue_count;
                lease_ms = ((_active.timeout_ms +
                    SYS_AT_SEND_WAIT_MS) *
                    ((u32)_active.retry_max + 1U)) +
                    ((u32)_active.retry_max * SYS_AT_RETRY_DELAY_MS);
                if (sys_resource_acquire(
                        SYS_RESOURCE_MODEM_EXCLUSIVE,
                        _active.owner_id,
                        lease_ms,
                        &_active_resource_token) != BOOL_TRUE)
                {
                    sys_at_finish(SYS_AT_RESULT_RESOURCE_BUSY);
                    break;
                }
                _active_resource_valid = BOOL_TRUE;
                _stats.active_owner_id = _active.owner_id;
                _stats.active_generation = _active_resource_token.generation;
                _active_attempt = 1U;
                _stats.active_attempt = _active_attempt;
                _state_since_ms = now_ms;
                _deadline_ms = now_ms + SYS_AT_SEND_WAIT_MS;
                _stats.state = SYS_AT_STATE_SEND;
            }
            else
            {
                _stats.state = SYS_AT_STATE_IDLE;
            }
            break;

        case SYS_AT_STATE_SEND:
            if ((_active.command_length == 0U) ||
                (hw_uart1_write((const u8 *)_active.command,
                                _active.command_length) ==
                 _active.command_length))
            {
                _state_since_ms = now_ms;
                _deadline_ms = now_ms + _active.timeout_ms;
                _stats.state = SYS_AT_STATE_WAIT_RESPONSE;
            }
            else if (sys_time_is_due(now_ms, _deadline_ms) == BOOL_TRUE)
            {
                sys_at_retry_or_finish(SYS_AT_RESULT_TX_ERROR);
            }
            break;

        case SYS_AT_STATE_WAIT_RESPONSE:
            if (sys_time_is_due(now_ms, _deadline_ms) == BOOL_TRUE)
            {
                sys_at_retry_or_finish(SYS_AT_RESULT_TIMEOUT);
            }
            break;

        case SYS_AT_STATE_RETRY_WAIT:
            if (sys_time_is_due(now_ms, _deadline_ms) == BOOL_TRUE)
            {
                _active_attempt++;
                _stats.active_attempt = _active_attempt;
                _state_since_ms = now_ms;
                _deadline_ms = now_ms + SYS_AT_SEND_WAIT_MS;
                _stats.state = SYS_AT_STATE_SEND;
            }
            break;

        default:
            _stats.state = SYS_AT_STATE_IDLE;
            break;
    }
}

boolean_en sys_at_engine_busy(void)
{
    return ((_active_valid == BOOL_TRUE) || (_queue_count > 0U)) ?
        BOOL_TRUE : BOOL_FALSE;
}

boolean_en sys_at_engine_cancel_owner(u16 owner_id)
{
    u8 index;
    boolean_en cancelled;

    cancelled = BOOL_FALSE;
    index = 0U;
    while (index < _queue_count)
    {
        if (_queue[index].owner_id == owner_id)
        {
            sys_at_remove_queue_at(index);
            cancelled = BOOL_TRUE;
        }
        else
        {
            index++;
        }
    }
    _stats.queued_count = _queue_count;
    if ((_active_valid == BOOL_TRUE) && (_active.owner_id == owner_id))
    {
        sys_at_finish(SYS_AT_RESULT_CANCELLED);
        cancelled = BOOL_TRUE;
    }
    return cancelled;
}

void sys_at_engine_set_urc_handler(
    sys_at_line_handler_fn handler,
    void *context)
{
    _urc_handler = handler;
    _urc_context = context;
}

boolean_en sys_at_engine_enter_raw_mode(
    const sys_resource_token_st *token,
    sys_at_raw_handler_fn handler,
    void *context)
{
    if ((token == NULL) || (handler == NULL) ||
        (token->resource_id != SYS_RESOURCE_MODEM_EXCLUSIVE) ||
        (sys_resource_validate(token) != BOOL_TRUE) ||
        (sys_at_engine_busy() == BOOL_TRUE))
    {
        return BOOL_FALSE;
    }

    _raw_token = *token;
    _raw_handler = handler;
    _raw_context = context;
    _raw_trigger[0] = '\0';
    _raw_armed = BOOL_FALSE;
    _line_length = 0U;
    _line[0] = '\0';
    _parse_mode = SYS_AT_PARSE_RAW;
    _stats.parse_mode = SYS_AT_PARSE_RAW;
    return BOOL_TRUE;
}

boolean_en sys_at_engine_arm_raw_mode(
    const sys_resource_token_st *token,
    const char *trigger_token,
    sys_at_raw_handler_fn handler,
    void *context)
{
    u16 trigger_length;

    if ((token == NULL) || (trigger_token == NULL) ||
        (handler == NULL) ||
        (token->resource_id != SYS_RESOURCE_MODEM_EXCLUSIVE) ||
        (sys_resource_validate(token) != BOOL_TRUE) ||
        (_parse_mode != SYS_AT_PARSE_LINE) ||
        (_raw_armed == BOOL_TRUE))
    {
        return BOOL_FALSE;
    }
    trigger_length = (u16)strlen(trigger_token);
    if ((trigger_length == 0U) ||
        (trigger_length >= SYS_AT_RAW_TRIGGER_CAPACITY))
    {
        return BOOL_FALSE;
    }

    _raw_token = *token;
    _raw_handler = handler;
    _raw_context = context;
    memcpy(_raw_trigger, trigger_token, trigger_length + 1U);
    _raw_armed = BOOL_TRUE;
    return BOOL_TRUE;
}

boolean_en sys_at_engine_leave_raw_mode(
    const sys_resource_token_st *token)
{
    if ((token == NULL) ||
        (token->owner_id != _raw_token.owner_id) ||
        (token->generation != _raw_token.generation) ||
        (token->resource_id != _raw_token.resource_id))
    {
        return BOOL_FALSE;
    }

    _parse_mode = SYS_AT_PARSE_LINE;
    _stats.parse_mode = SYS_AT_PARSE_LINE;
    _raw_handler = NULL;
    _raw_context = NULL;
    _raw_trigger[0] = '\0';
    _raw_armed = BOOL_FALSE;
    memset(&_raw_token, 0, sizeof(_raw_token));
    return BOOL_TRUE;
}

void sys_at_engine_get_stats(sys_at_stats_st *stats)
{
    if (stats != NULL)
    {
        *stats = _stats;
    }
}
