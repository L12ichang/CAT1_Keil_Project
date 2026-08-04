/*************************************************************
程序功能：量产校准会话、租约、结果缓存与安全门禁
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "sys_calibration_service.h"
#include "sys_calibration_curve.h"
#include "sys_calibration_driver_protocol.h"
#include "sys_calibration_storage.h"
#include <string.h>

#define SYS_CALIBRATION_STAGE_LENGTH \
    SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH

typedef enum
{
    SYS_CALIBRATION_OP_BEGIN = 1,
    SYS_CALIBRATION_OP_HEARTBEAT,
    SYS_CALIBRATION_OP_SET_POINT,
    SYS_CALIBRATION_OP_RAW,
    SYS_CALIBRATION_OP_STAGE,
    SYS_CALIBRATION_OP_APPLY,
    SYS_CALIBRATION_OP_READBACK,
    SYS_CALIBRATION_OP_COMMIT,
    SYS_CALIBRATION_OP_ABORT,
    SYS_CALIBRATION_OP_RELEASE
} sys_calibration_operation_en;

typedef struct
{
    sys_calibration_service_status_st status;
    u8 staged_payload[SYS_CALIBRATION_STAGE_LENGTH];
    u32 cache_session_id;
    u32 cache_seq;
    u8 cache_operation;
    sys_calibration_result_en cache_result;
    boolean_en cache_valid;
    sys_calibration_safe_off_fn safe_off;
    boolean_en safety_ready;
    boolean_en boot_inhibit_active;
} sys_calibration_service_context_st;

static sys_calibration_service_context_st _service;

static boolean_en sys_calibration_service_copy_status(
    sys_calibration_service_status_st *status)
{
    if (status == NULL)
    {
        return BOOL_FALSE;
    }
    _service.status.safety_ready = _service.safety_ready;
    _service.status.boot_inhibit_active = _service.boot_inhibit_active;
    memcpy(status, &_service.status, sizeof(*status));
    return BOOL_TRUE;
}

static void sys_calibration_service_safe_off(void)
{
    if (_service.safe_off != NULL)
    {
        _service.safe_off();
    }
}

static void sys_calibration_service_set_result(
    sys_calibration_result_en result)
{
    _service.status.last_result = result;
    ++_service.status.result_seq;
}

static void sys_calibration_service_cache_result(
    u32 session_id,
    u32 seq,
    sys_calibration_operation_en operation,
    sys_calibration_result_en result)
{
    _service.cache_session_id = session_id;
    _service.cache_seq = seq;
    _service.cache_operation = (u8)operation;
    _service.cache_result = result;
    _service.cache_valid = BOOL_TRUE;
    _service.status.last_request_seq = seq;
}

static boolean_en sys_calibration_service_get_cached_result(
    u32 session_id,
    u32 seq,
    sys_calibration_operation_en operation,
    sys_calibration_result_en *result,
    sys_calibration_service_status_st *status)
{
    if (_service.cache_valid != BOOL_TRUE ||
        _service.cache_session_id != session_id ||
        _service.cache_seq != seq ||
        _service.cache_operation != (u8)operation)
    {
        return BOOL_FALSE;
    }
    if (result != NULL)
    {
        *result = _service.cache_result;
    }
    (void)sys_calibration_service_copy_status(status);
    return BOOL_TRUE;
}

static void sys_calibration_service_finish(
    u32 session_id,
    u32 seq,
    sys_calibration_operation_en operation,
    sys_calibration_result_en result,
    sys_calibration_service_status_st *status)
{
    sys_calibration_service_set_result(result);
    sys_calibration_service_cache_result(session_id, seq, operation, result);
    (void)sys_calibration_service_copy_status(status);
}

static boolean_en sys_calibration_service_lease_expired(u32 now_ms)
{
    return ((s32)(now_ms - _service.status.lease_deadline_ms) >= 0) ?
           BOOL_TRUE : BOOL_FALSE;
}

static sys_calibration_result_en sys_calibration_service_require_session(
    u32 session_id,
    u32 now_ms,
    sys_calibration_operation_en operation,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en cached_result;

    if (sys_calibration_service_get_cached_result(session_id, seq, operation,
                                                   &cached_result, status) ==
        BOOL_TRUE)
    {
        return cached_result;
    }
    if (_service.status.state != SYS_CALIBRATION_STATE_ACTIVE &&
        _service.status.state != SYS_CALIBRATION_STATE_STAGED &&
        _service.status.state != SYS_CALIBRATION_STATE_APPLIED)
    {
        sys_calibration_service_finish(session_id, seq, operation,
                                       SYS_CALIBRATION_RESULT_INVALID_STATE,
                                       status);
        return SYS_CALIBRATION_RESULT_INVALID_STATE;
    }
    if (_service.status.session_id != session_id || session_id == 0U)
    {
        sys_calibration_service_finish(session_id, seq, operation,
                                       SYS_CALIBRATION_RESULT_INVALID_ARGUMENT,
                                       status);
        return SYS_CALIBRATION_RESULT_INVALID_ARGUMENT;
    }
    if (sys_calibration_service_lease_expired(now_ms) == BOOL_TRUE)
    {
        sys_calibration_service_safe_off();
        _service.status.state = SYS_CALIBRATION_STATE_FAULT;
        _service.boot_inhibit_active = BOOL_TRUE;
        sys_calibration_service_finish(session_id, seq, operation,
                                       SYS_CALIBRATION_RESULT_LEASE_EXPIRED,
                                       status);
        return SYS_CALIBRATION_RESULT_LEASE_EXPIRED;
    }
    return SYS_CALIBRATION_RESULT_OK;
}

static boolean_en sys_calibration_service_validate_lease(u32 lease_ms)
{
    return (lease_ms >= SYS_CALIBRATION_LEASE_MIN_MS &&
            lease_ms <= SYS_CALIBRATION_LEASE_MAX_MS) ? BOOL_TRUE : BOOL_FALSE;
}

void sys_calibration_service_init(void)
{
    memset(&_service, 0, sizeof(_service));
    _service.status.state = SYS_CALIBRATION_STATE_DISABLED;
    _service.status.last_result = SYS_CALIBRATION_RESULT_SAFETY_NOT_READY;
    _service.status.codec_available = BOOL_TRUE;
    _service.status.commit_available = BOOL_FALSE;
    _service.status.nonzero_output_allowed = BOOL_FALSE;
    _service.safety_ready = BOOL_FALSE;
    _service.boot_inhibit_active = BOOL_TRUE;
    _service.status.boot_inhibit_active = BOOL_TRUE;
}

void sys_calibration_service_bind_safe_off(sys_calibration_safe_off_fn safe_off)
{
    _service.safe_off = safe_off;
}

void sys_calibration_service_set_safety_ready(boolean_en ready)
{
    /* 仅供Host状态机测试注入；生产启动路径不调用此函数。 */
    _service.safety_ready = ready;
    if (ready == BOOL_TRUE)
    {
        _service.boot_inhibit_active = BOOL_FALSE;
        _service.status.state = SYS_CALIBRATION_STATE_IDLE;
    }
    else
    {
        _service.boot_inhibit_active = BOOL_TRUE;
        _service.status.state = SYS_CALIBRATION_STATE_DISABLED;
        sys_calibration_service_safe_off();
    }
}

boolean_en sys_calibration_service_get_status(
    sys_calibration_service_status_st *status)
{
    return sys_calibration_service_copy_status(status);
}

boolean_en sys_calibration_service_get_staged_payload(
    u8 *payload,
    u16 capacity,
    u16 *length)
{
    if (payload == NULL || length == NULL ||
        _service.status.staged_valid != BOOL_TRUE ||
        capacity < _service.status.staged_length)
    {
        return BOOL_FALSE;
    }
    memcpy(payload, _service.staged_payload, _service.status.staged_length);
    *length = _service.status.staged_length;
    return BOOL_TRUE;
}

boolean_en sys_calibration_service_is_boot_inhibited(void)
{
    return _service.boot_inhibit_active;
}

sys_calibration_result_en sys_calibration_service_begin_seq(
    u32 session_id,
    u32 now_ms,
    u32 lease_ms,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en cached_result;

    if (sys_calibration_service_get_cached_result(session_id, seq,
                                                   SYS_CALIBRATION_OP_BEGIN,
                                                   &cached_result, status) ==
        BOOL_TRUE)
    {
        return cached_result;
    }
    if (session_id == 0U ||
        sys_calibration_service_validate_lease(lease_ms) != BOOL_TRUE)
    {
        sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_BEGIN,
                                       SYS_CALIBRATION_RESULT_INVALID_ARGUMENT,
                                       status);
        return SYS_CALIBRATION_RESULT_INVALID_ARGUMENT;
    }
    if (_service.status.state == SYS_CALIBRATION_STATE_ACTIVE &&
        _service.status.session_id != session_id)
    {
        sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_BEGIN,
                                       SYS_CALIBRATION_RESULT_BUSY, status);
        return SYS_CALIBRATION_RESULT_BUSY;
    }
    if (_service.safety_ready != BOOL_TRUE)
    {
        sys_calibration_service_safe_off();
        sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_BEGIN,
                                       SYS_CALIBRATION_RESULT_SAFETY_NOT_READY,
                                       status);
        return SYS_CALIBRATION_RESULT_SAFETY_NOT_READY;
    }
    sys_calibration_service_safe_off();
    _service.status.session_id = session_id;
    _service.status.lease_deadline_ms = now_ms + lease_ms;
    _service.status.state = SYS_CALIBRATION_STATE_ACTIVE;
    _service.status.current_level = 0U;
    _service.status.staged_valid = BOOL_FALSE;
    _service.status.staged_length = 0U;
    _service.boot_inhibit_active = BOOL_TRUE;
    sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_BEGIN,
                                   SYS_CALIBRATION_RESULT_OK, status);
    return SYS_CALIBRATION_RESULT_OK;
}

sys_calibration_result_en sys_calibration_service_heartbeat_seq(
    u32 session_id,
    u32 now_ms,
    u32 lease_ms,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;

    if (sys_calibration_service_validate_lease(lease_ms) != BOOL_TRUE)
    {
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_HEARTBEAT,
                                       SYS_CALIBRATION_RESULT_INVALID_ARGUMENT,
                                       status);
        return SYS_CALIBRATION_RESULT_INVALID_ARGUMENT;
    }
    result = sys_calibration_service_require_session(
        session_id, now_ms, SYS_CALIBRATION_OP_HEARTBEAT, seq, status);
    if (result != SYS_CALIBRATION_RESULT_OK)
    {
        return result;
    }
    _service.status.lease_deadline_ms = now_ms + lease_ms;
    sys_calibration_service_finish(session_id, seq,
                                   SYS_CALIBRATION_OP_HEARTBEAT,
                                   SYS_CALIBRATION_RESULT_OK, status);
    return SYS_CALIBRATION_RESULT_OK;
}

sys_calibration_result_en sys_calibration_service_set_point_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    u16 level,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;

    if (sys_calibration_curve_validate_level(level) != BOOL_TRUE)
    {
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_SET_POINT,
                                       SYS_CALIBRATION_RESULT_PROTOCOL_ERROR,
                                       status);
        return SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
    }
    result = sys_calibration_service_require_session(
        session_id, now_ms, SYS_CALIBRATION_OP_SET_POINT, seq, status);
    if (result != SYS_CALIBRATION_RESULT_OK)
    {
        return result;
    }
    if (level != 0U &&
        (_service.safety_ready != BOOL_TRUE ||
         SYS_CALIBRATION_NONZERO_OUTPUT_ENABLED == 0U))
    {
        sys_calibration_service_safe_off();
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_SET_POINT,
                                       SYS_CALIBRATION_RESULT_SAFETY_NOT_READY,
                                       status);
        return SYS_CALIBRATION_RESULT_SAFETY_NOT_READY;
    }
    _service.status.current_level = level;
    if (level == 0U)
    {
        sys_calibration_service_safe_off();
    }
    sys_calibration_service_finish(session_id, seq,
                                   SYS_CALIBRATION_OP_SET_POINT,
                                   SYS_CALIBRATION_RESULT_OK, status);
    return SYS_CALIBRATION_RESULT_OK;
}

sys_calibration_result_en sys_calibration_service_raw_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    const u8 *frame,
    u16 frame_length,
    sys_calibration_raw_direction_en direction,
    sys_calibration_service_status_st *status)
{
    sys_calibration_driver_message_st message;
    sys_calibration_result_en result;

    if ((direction != SYS_CALIBRATION_RAW_QUERY &&
         direction != SYS_CALIBRATION_RAW_SET) || frame == NULL ||
        sys_calibration_driver_decode(frame, frame_length, &message) != BOOL_TRUE ||
        (direction == SYS_CALIBRATION_RAW_QUERY &&
         message.command != SYS_CALIBRATION_DRIVER_CMD_QUERY) ||
        (direction == SYS_CALIBRATION_RAW_SET &&
         message.command != SYS_CALIBRATION_DRIVER_CMD_SET))
    {
        sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_RAW,
                                       SYS_CALIBRATION_RESULT_PROTOCOL_ERROR,
                                       status);
        return SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
    }
    result = sys_calibration_service_require_session(
        session_id, now_ms, SYS_CALIBRATION_OP_RAW, seq, status);
    if (result != SYS_CALIBRATION_RESULT_OK)
    {
        return result;
    }
    /* 只完成校验和幂等缓存；UART/驱动器事务必须由后续隔离transport实现。 */
    (void)message;
    sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_RAW,
                                   SYS_CALIBRATION_RESULT_NOT_AVAILABLE, status);
    return SYS_CALIBRATION_RESULT_NOT_AVAILABLE;
}

sys_calibration_result_en sys_calibration_service_stage_config_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    const u8 *payload,
    u16 length,
    sys_calibration_service_status_st *status)
{
    sys_calibration_driver_table_st table;
    sys_calibration_result_en result;

    if (payload == NULL || length != SYS_CALIBRATION_STAGE_LENGTH ||
        sys_calibration_driver_table_decode(payload, length, &table) != BOOL_TRUE)
    {
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_STAGE,
                                       SYS_CALIBRATION_RESULT_PROTOCOL_ERROR,
                                       status);
        return SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
    }
    if (sys_calibration_curve_validate_context(
            SYS_CALIBRATION_50W_MID,
            SYS_CALIBRATION_50W_RS3_MOHM,
            SYS_CALIBRATION_50W_RATED_CURRENT_MA) != BOOL_TRUE)
    {
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_STAGE,
                                       SYS_CALIBRATION_RESULT_INVALID_ARGUMENT,
                                       status);
        return SYS_CALIBRATION_RESULT_INVALID_ARGUMENT;
    }
    result = sys_calibration_service_require_session(
        session_id, now_ms, SYS_CALIBRATION_OP_STAGE, seq, status);
    if (result != SYS_CALIBRATION_RESULT_OK)
    {
        return result;
    }
    memcpy(_service.staged_payload, payload, length);
    _service.status.staged_length = length;
    _service.status.staged_crc32 = sys_calibration_storage_crc32(payload, length);
    _service.status.staged_valid = BOOL_TRUE;
    _service.status.state = SYS_CALIBRATION_STATE_STAGED;
    sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_STAGE,
                                   SYS_CALIBRATION_RESULT_OK, status);
    return SYS_CALIBRATION_RESULT_OK;
}

sys_calibration_result_en sys_calibration_service_apply_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;

    result = sys_calibration_service_require_session(
        session_id, now_ms, SYS_CALIBRATION_OP_APPLY, seq, status);
    if (result != SYS_CALIBRATION_RESULT_OK)
    {
        return result;
    }
    if (_service.status.staged_valid != BOOL_TRUE)
    {
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_APPLY,
                                       SYS_CALIBRATION_RESULT_INVALID_STATE,
                                       status);
        return SYS_CALIBRATION_RESULT_INVALID_STATE;
    }
    sys_calibration_service_safe_off();
    sys_calibration_service_finish(session_id, seq,
                                   SYS_CALIBRATION_OP_APPLY,
                                   SYS_CALIBRATION_RESULT_SAFETY_NOT_READY,
                                   status);
    return SYS_CALIBRATION_RESULT_SAFETY_NOT_READY;
}

sys_calibration_result_en sys_calibration_service_commit_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;

    result = sys_calibration_service_require_session(
        session_id, now_ms, SYS_CALIBRATION_OP_COMMIT, seq, status);
    if (result != SYS_CALIBRATION_RESULT_OK)
    {
        return result;
    }
    if (_service.status.staged_valid != BOOL_TRUE)
    {
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_COMMIT,
                                       SYS_CALIBRATION_RESULT_INVALID_STATE,
                                       status);
        return SYS_CALIBRATION_RESULT_INVALID_STATE;
    }
    /* 共享2KiB页所有权、新鲜Keil MAP及真实掉电证据未齐，保持失败关闭。 */
    sys_calibration_service_finish(session_id, seq,
                                   SYS_CALIBRATION_OP_COMMIT,
                                   SYS_CALIBRATION_RESULT_FLASH_GATED,
                                   status);
    return SYS_CALIBRATION_RESULT_FLASH_GATED;
}

sys_calibration_result_en sys_calibration_service_readback_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;

    result = sys_calibration_service_require_session(
        session_id, now_ms, SYS_CALIBRATION_OP_READBACK, seq, status);
    if (result != SYS_CALIBRATION_RESULT_OK)
    {
        return result;
    }
    if (_service.status.staged_valid != BOOL_TRUE)
    {
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_READBACK,
                                       SYS_CALIBRATION_RESULT_INVALID_STATE,
                                       status);
        return SYS_CALIBRATION_RESULT_INVALID_STATE;
    }
    sys_calibration_service_finish(session_id, seq,
                                   SYS_CALIBRATION_OP_READBACK,
                                   SYS_CALIBRATION_RESULT_OK, status);
    return SYS_CALIBRATION_RESULT_OK;
}

sys_calibration_result_en sys_calibration_service_abort_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en cached_result;

    if (sys_calibration_service_get_cached_result(session_id, seq,
                                                   SYS_CALIBRATION_OP_ABORT,
                                                   &cached_result, status) ==
        BOOL_TRUE)
    {
        return cached_result;
    }
    (void)now_ms;
    if (session_id == 0U && _service.status.state != SYS_CALIBRATION_STATE_DISABLED)
    {
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_ABORT,
                                       SYS_CALIBRATION_RESULT_INVALID_ARGUMENT,
                                       status);
        return SYS_CALIBRATION_RESULT_INVALID_ARGUMENT;
    }
    if (_service.status.session_id != 0U && session_id != 0U &&
        _service.status.session_id != session_id)
    {
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_ABORT,
                                       SYS_CALIBRATION_RESULT_INVALID_ARGUMENT,
                                       status);
        return SYS_CALIBRATION_RESULT_INVALID_ARGUMENT;
    }
    sys_calibration_service_safe_off();
    _service.status.session_id = session_id;
    _service.status.state = SYS_CALIBRATION_STATE_ABORTED;
    _service.status.staged_valid = BOOL_FALSE;
    _service.status.staged_length = 0U;
    _service.boot_inhibit_active = BOOL_TRUE;
    sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_ABORT,
                                   SYS_CALIBRATION_RESULT_OK, status);
    return SYS_CALIBRATION_RESULT_OK;
}

sys_calibration_result_en sys_calibration_service_release_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en cached_result;

    if (sys_calibration_service_get_cached_result(session_id, seq,
                                                   SYS_CALIBRATION_OP_RELEASE,
                                                   &cached_result, status) ==
        BOOL_TRUE)
    {
        return cached_result;
    }
    (void)now_ms;
    if (session_id == 0U || _service.status.session_id != session_id)
    {
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_RELEASE,
                                       SYS_CALIBRATION_RESULT_INVALID_ARGUMENT,
                                       status);
        return SYS_CALIBRATION_RESULT_INVALID_ARGUMENT;
    }
    if (_service.status.state != SYS_CALIBRATION_STATE_ABORTED &&
        _service.status.state != SYS_CALIBRATION_STATE_FAULT &&
        _service.status.state != SYS_CALIBRATION_STATE_ACTIVE &&
        _service.status.state != SYS_CALIBRATION_STATE_STAGED &&
        _service.status.state != SYS_CALIBRATION_STATE_APPLIED)
    {
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_RELEASE,
                                       SYS_CALIBRATION_RESULT_INVALID_STATE,
                                       status);
        return SYS_CALIBRATION_RESULT_INVALID_STATE;
    }
    sys_calibration_service_safe_off();
    /* 清除持久化active记录仍依赖共享页审计和新鲜MAP，不能在此伪造成功。 */
    sys_calibration_service_finish(session_id, seq,
                                   SYS_CALIBRATION_OP_RELEASE,
                                   SYS_CALIBRATION_RESULT_FLASH_GATED,
                                   status);
    return SYS_CALIBRATION_RESULT_FLASH_GATED;
}

void sys_calibration_service_force_fault(void)
{
    sys_calibration_service_safe_off();
    _service.status.state = SYS_CALIBRATION_STATE_FAULT;
    _service.status.last_result = SYS_CALIBRATION_RESULT_HARDWARE_FAULT;
    ++_service.status.result_seq;
    _service.boot_inhibit_active = BOOL_TRUE;
    _service.cache_valid = BOOL_FALSE;
}

sys_calibration_result_en sys_calibration_service_begin(
    u32 session_id,
    u32 now_ms,
    u32 lease_ms,
    sys_calibration_service_status_st *status)
{
    return sys_calibration_service_begin_seq(session_id, now_ms, lease_ms, 0U,
                                              status);
}

sys_calibration_result_en sys_calibration_service_set_point(
    u32 session_id,
    u32 now_ms,
    u16 level,
    sys_calibration_service_status_st *status)
{
    return sys_calibration_service_set_point_seq(session_id, now_ms, 0U, level,
                                                  status);
}

sys_calibration_result_en sys_calibration_service_stage_config(
    u32 session_id,
    const u8 *payload,
    u32 length,
    sys_calibration_service_status_st *status)
{
    if (length > 65535UL)
    {
        sys_calibration_service_set_result(SYS_CALIBRATION_RESULT_PROTOCOL_ERROR);
        (void)sys_calibration_service_copy_status(status);
        return SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
    }
    return sys_calibration_service_stage_config_seq(
        session_id, 0U, 0U, payload, (u16)length, status);
}

sys_calibration_result_en sys_calibration_service_commit(
    u32 session_id,
    sys_calibration_service_status_st *status)
{
    return sys_calibration_service_commit_seq(session_id, 0U, 0U, status);
}

boolean_en sys_calibration_service_abort(
    u32 session_id,
    sys_calibration_service_status_st *status)
{
    return sys_calibration_service_abort_seq(session_id, 0U, 0U, status) ==
           SYS_CALIBRATION_RESULT_OK ? BOOL_TRUE : BOOL_FALSE;
}

boolean_en sys_calibration_service_timer(
    u32 now_ms,
    sys_calibration_service_status_st *status)
{
    if ((_service.status.state == SYS_CALIBRATION_STATE_ACTIVE ||
         _service.status.state == SYS_CALIBRATION_STATE_STAGED ||
         _service.status.state == SYS_CALIBRATION_STATE_APPLIED) &&
        sys_calibration_service_lease_expired(now_ms) == BOOL_TRUE)
    {
        sys_calibration_service_safe_off();
        _service.status.state = SYS_CALIBRATION_STATE_FAULT;
        _service.status.last_result = SYS_CALIBRATION_RESULT_LEASE_EXPIRED;
        ++_service.status.result_seq;
        _service.boot_inhibit_active = BOOL_TRUE;
        _service.cache_valid = BOOL_FALSE;
    }
    return sys_calibration_service_copy_status(status);
}
