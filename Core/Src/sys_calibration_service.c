/*************************************************************
程序功能：量产校准会话、租约、结果缓存与安全门禁
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：黎长彩
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "sys_calibration_service.h"
#include "sys_calibration_curve.h"
#include "sys_calibration_driver_protocol.h"
#include "sys_calibration_storage.h"
#include "sys_calibration_safety.h"
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
    SYS_CALIBRATION_OP_RELEASE,
    SYS_CALIBRATION_OP_SET_VALIDATION_PERCENT
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
    sys_calibration_set_level_fn set_level;
    sys_calibration_set_inhibit_fn set_inhibit;
    sys_calibration_commit_fn commit;
    sys_calibration_bound_voltage_fn get_bound_voltage;
    boolean_en safety_ready;
    boolean_en boot_inhibit_active;
    boolean_en persistence_ready;
    u8 committed_payload[SYS_CALIBRATION_STAGE_LENGTH];
    sys_calibration_context_st committed_context;
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
    _service.status.persistence_ready = _service.persistence_ready;
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
    /* 兼容旧调用点的seq=0没有可证明的请求身份，不参与重试缓存。 */
    if (seq == 0U)
    {
        return;
    }
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
    sys_calibration_service_status_st *status);

static boolean_en sys_calibration_service_check_replay(
    u32 session_id,
    u32 seq,
    sys_calibration_operation_en operation,
    sys_calibration_result_en *result,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en cached_result;

    if (seq == 0U)
    {
        return BOOL_FALSE;
    }
    if (sys_calibration_service_get_cached_result(
            session_id, seq, operation, &cached_result, status) == BOOL_TRUE)
    {
        if (result != NULL)
        {
            *result = cached_result;
        }
        return BOOL_TRUE;
    }

    /* 单槽缓存被新请求覆盖后，旧seq只能拒绝，不能再次执行副作用。 */
    if (_service.status.session_id == session_id && session_id != 0U &&
        _service.status.last_request_seq != 0U &&
        seq <= _service.status.last_request_seq)
    {
        sys_calibration_service_set_result(SYS_CALIBRATION_RESULT_DUPLICATE);
        (void)sys_calibration_service_copy_status(status);
        if (result != NULL)
        {
            *result = SYS_CALIBRATION_RESULT_DUPLICATE;
        }
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
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
    sys_calibration_result_en replay_result;

    if (sys_calibration_service_check_replay(
            session_id, seq, operation, &replay_result, status) == BOOL_TRUE)
    {
        return replay_result;
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
    if (_service.status.context_valid != BOOL_TRUE ||
        sys_product_profile_context_validate(
            &_service.status.context,
            (_service.status.state == SYS_CALIBRATION_STATE_STAGED ||
             _service.status.state == SYS_CALIBRATION_STATE_APPLIED) ?
                BOOL_TRUE : BOOL_FALSE) != BOOL_TRUE ||
        _service.get_bound_voltage == NULL ||
        _service.status.context.calibration_voltage_01v !=
            _service.get_bound_voltage())
    {
        sys_calibration_service_safe_off();
        _service.status.state = SYS_CALIBRATION_STATE_FAULT;
        _service.boot_inhibit_active = BOOL_TRUE;
        sys_calibration_service_finish(session_id, seq, operation,
                                       SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH,
                                       status);
        return SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH;
    }
    if (sys_calibration_service_lease_expired(now_ms) == BOOL_TRUE)
    {
        sys_calibration_service_safe_off();
        _service.status.state = SYS_CALIBRATION_STATE_FAULT;
        _service.safety_ready = BOOL_FALSE;
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

static boolean_en sys_calibration_service_validate_table(
    const sys_calibration_driver_table_st *table,
    u16 *calibrated_max_current_ma)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    u16 characterized_i_max_ma;
    u16 calibrated_target_ma;
    u32 minimum_span_current_ma;
    u32 maximum_span_current_ma;
    u8 index;

    if (table == NULL || calibrated_max_current_ma == NULL ||
        sys_product_profile_is_complete(profile) != BOOL_TRUE ||
        sys_product_profile_context_validate(&_service.status.context,
            (_service.status.context.table_crc32 != 0U) ?
                BOOL_TRUE : BOOL_FALSE) != BOOL_TRUE ||
        _service.status.context.calibrated_max_current_ma == 0U)
    {
        return BOOL_FALSE;
    }
    characterized_i_max_ma =
        _service.status.context.calibrated_max_current_ma;
    calibrated_target_ma =
        _service.status.context.configured_rated_current_ma;
    for (index = 1U; index < SYS_CALIBRATION_DRIVER_POINT_COUNT; ++index)
    {
        if (table->point[index].instrument_output_current_ma <
                table->point[index - 1U].instrument_output_current_ma ||
            table->point[index].instrument_output_power_01w <
                table->point[index - 1U].instrument_output_power_01w ||
            table->point[index].device_output_current_ma <
                table->point[index - 1U].device_output_current_ma ||
            table->point[index].device_output_power_01w <
                table->point[index - 1U].device_output_power_01w ||
            table->point[index].input_current_ma <
                table->point[index - 1U].input_current_ma ||
            table->point[index].input_power_01w <
                table->point[index - 1U].input_power_01w ||
            table->point[index].input_current_ad <
                table->point[index - 1U].input_current_ad)
        {
            return BOOL_FALSE;
        }
    }
    minimum_span_current_ma =
        ((u32)calibrated_target_ma *
         (1000U - profile->calibration_span_tolerance_permille)) / 1000U;
    maximum_span_current_ma =
        ((u32)calibrated_target_ma *
         (1000U + profile->calibration_span_tolerance_permille) + 999U) /
        1000U;

    /* 0x07 Imax is the pre-gain Level200 measurement. After it is written,
       firmware applies the per-device full-scale gain before the formal
       11-point sweep, so Level200 must land near configured SET_OUTCUR. */
    if (table->point[SYS_CALIBRATION_DRIVER_POINT_COUNT - 1U]
            .instrument_output_current_ma >= profile->absolute_fail_current_ma ||
        table->point[SYS_CALIBRATION_DRIVER_POINT_COUNT - 1U]
            .device_output_current_ma >= profile->absolute_fail_current_ma ||
        table->point[SYS_CALIBRATION_DRIVER_POINT_COUNT - 1U]
            .instrument_output_current_ma < minimum_span_current_ma ||
        table->point[SYS_CALIBRATION_DRIVER_POINT_COUNT - 1U]
            .instrument_output_current_ma > maximum_span_current_ma)
    {
        return BOOL_FALSE;
    }
    *calibrated_max_current_ma = characterized_i_max_ma;
    return BOOL_TRUE;
}

static boolean_en sys_calibration_service_get_runtime_table(
    sys_calibration_driver_table_st *table)
{
    const u8 *payload;

    if (table == NULL || _service.status.context_valid != BOOL_TRUE ||
        sys_product_profile_context_validate(&_service.status.context,
                                             BOOL_TRUE) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (_service.status.state == SYS_CALIBRATION_STATE_APPLIED &&
        _service.status.staged_valid == BOOL_TRUE)
    {
        payload = _service.staged_payload;
    }
    else if (_service.status.committed_valid == BOOL_TRUE)
    {
        payload = _service.committed_payload;
    }
    else
    {
        return BOOL_FALSE;
    }
    return sys_calibration_driver_table_decode(
        payload, SYS_CALIBRATION_STAGE_LENGTH, table);
}

static u16 sys_calibration_service_interpolate_u16(u16 x,
                                                   u16 x0,
                                                   u16 x1,
                                                   u16 y0,
                                                   u16 y1)
{
    u32 numerator;
    if (x1 <= x0 || y1 <= y0)
    {
        return y0;
    }
    numerator = (u32)(x - x0) * (u32)(y1 - y0);
    return (u16)(y0 + (u16)((numerator + (u32)(x1 - x0) / 2U) /
                            (u32)(x1 - x0)));
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

void sys_calibration_service_bind_platform(
    sys_calibration_set_level_fn set_level,
    sys_calibration_set_inhibit_fn set_inhibit,
    sys_calibration_commit_fn commit)
{
    _service.set_level = set_level;
    _service.set_inhibit = set_inhibit;
    _service.commit = commit;
    _service.status.nonzero_output_allowed = (set_level != NULL) ? BOOL_TRUE : BOOL_FALSE;
    _service.status.commit_available = (commit != NULL && set_inhibit != NULL) ?
                                       BOOL_TRUE : BOOL_FALSE;
}

void sys_calibration_service_bind_bound_voltage(
    sys_calibration_bound_voltage_fn get_bound_voltage)
{
    _service.get_bound_voltage = get_bound_voltage;
}

void sys_calibration_service_restore_boot(boolean_en inhibited,
                                          boolean_en persistence_ready)
{
    _service.persistence_ready = persistence_ready;
    _service.boot_inhibit_active = inhibited;
    if (inhibited == BOOL_TRUE)
    {
        _service.status.state = SYS_CALIBRATION_STATE_FAULT;
        _service.status.last_result = SYS_CALIBRATION_RESULT_HARDWARE_FAULT;
        _service.safety_ready = BOOL_FALSE;
        sys_calibration_service_safe_off();
    }
}

boolean_en sys_calibration_service_load_committed(
    const sys_calibration_context_st *context,
    const u8 *payload,
    u16 length,
    u32 generation)
{
    sys_calibration_driver_table_st table;
    u16 derived_max_current_ma;

    if (context == NULL || payload == NULL ||
        length != SYS_CALIBRATION_STAGE_LENGTH ||
        sys_product_profile_context_validate(context, BOOL_TRUE) != BOOL_TRUE ||
        context->table_crc32 != sys_calibration_storage_crc32(payload, length))
    {
        return BOOL_FALSE;
    }
    _service.status.context = *context;
    _service.status.context_valid = BOOL_TRUE;
    _service.committed_context = *context;
    if (
        sys_calibration_driver_table_decode(payload, length, &table) != BOOL_TRUE ||
        sys_calibration_service_validate_table(
            &table, &derived_max_current_ma) != BOOL_TRUE ||
        derived_max_current_ma != context->calibrated_max_current_ma)
    {
        memset(&_service.status.context, 0, sizeof(_service.status.context));
        _service.status.context_valid = BOOL_FALSE;
        return BOOL_FALSE;
    }
    memcpy(_service.committed_payload, payload, length);
    _service.status.committed_crc32 = sys_calibration_storage_crc32(payload, length);
    _service.status.committed_generation = generation;
    _service.status.committed_valid = BOOL_TRUE;
    return BOOL_TRUE;
}

static void sys_calibration_service_restore_committed_context(void)
{
    if (_service.status.committed_valid == BOOL_TRUE &&
        sys_product_profile_context_validate(&_service.committed_context,
                                             BOOL_TRUE) == BOOL_TRUE)
    {
        _service.status.context = _service.committed_context;
        _service.status.context_valid = BOOL_TRUE;
    }
    else
    {
        memset(&_service.status.context, 0, sizeof(_service.status.context));
        _service.status.context_valid = BOOL_FALSE;
    }
}

void sys_calibration_service_set_safety_ready(boolean_en ready)
{
    /* 仅在启动持久化检查通过或Host状态机测试时设置。 */
    _service.safety_ready = ready;
    if (ready == BOOL_TRUE)
    {
        if (_service.status.state == SYS_CALIBRATION_STATE_DISABLED)
        {
            _service.safety_ready = BOOL_TRUE;
            /* 只有初始化后的明确安全前置检查才能解锁；故障/ABORT不可自解锁。 */
            _service.boot_inhibit_active = BOOL_FALSE;
            _service.status.state = SYS_CALIBRATION_STATE_IDLE;
        }
        else
        {
            _service.safety_ready = BOOL_FALSE;
            /* 运行中、ABORT和FAULT均保持锁存，不因重复ready调用而点亮。 */
            _service.boot_inhibit_active = BOOL_TRUE;
        }
    }
    else
    {
        _service.boot_inhibit_active = BOOL_TRUE;
        if (_service.status.state != SYS_CALIBRATION_STATE_FAULT &&
            _service.status.state != SYS_CALIBRATION_STATE_ABORTED)
        {
            _service.status.state = SYS_CALIBRATION_STATE_DISABLED;
        }
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
        capacity < SYS_CALIBRATION_STAGE_LENGTH)
    {
        return BOOL_FALSE;
    }
    if (_service.status.staged_valid == BOOL_TRUE)
    {
        memcpy(payload, _service.staged_payload, _service.status.staged_length);
        *length = _service.status.staged_length;
    }
    else if (_service.status.committed_valid == BOOL_TRUE)
    {
        memcpy(payload, _service.committed_payload, SYS_CALIBRATION_STAGE_LENGTH);
        *length = SYS_CALIBRATION_STAGE_LENGTH;
    }
    else
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

boolean_en sys_calibration_service_get_context(
    sys_calibration_context_st *context)
{
    if (context == NULL || _service.status.context_valid != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    *context = _service.status.context;
    return BOOL_TRUE;
}

boolean_en sys_calibration_service_get_committed_context(
    sys_calibration_context_st *context)
{
    if (context == NULL || _service.status.committed_valid != BOOL_TRUE ||
        sys_product_profile_context_validate(&_service.committed_context,
                                             BOOL_TRUE) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    *context = _service.committed_context;
    return BOOL_TRUE;
}

boolean_en sys_calibration_service_is_boot_inhibited(void)
{
    return _service.boot_inhibit_active;
}

boolean_en sys_calibration_service_is_output_authorized(void)
{
    return ((_service.status.state == SYS_CALIBRATION_STATE_ACTIVE ||
             _service.status.state == SYS_CALIBRATION_STATE_STAGED ||
             _service.status.state == SYS_CALIBRATION_STATE_APPLIED) &&
            _service.status.session_id != 0U &&
            _service.safety_ready == BOOL_TRUE &&
            _service.boot_inhibit_active == BOOL_TRUE) ? BOOL_TRUE : BOOL_FALSE;
}

boolean_en sys_calibration_service_runtime_context_matches_voltage(
    u16 bound_voltage_01v)
{
    boolean_en require_table_crc;
    if (_service.status.context_valid != BOOL_TRUE)
    {
        return BOOL_TRUE;
    }
    require_table_crc =
        (_service.status.state == SYS_CALIBRATION_STATE_STAGED ||
         _service.status.state == SYS_CALIBRATION_STATE_APPLIED ||
         (_service.status.state == SYS_CALIBRATION_STATE_IDLE &&
          _service.status.committed_valid == BOOL_TRUE)) ? BOOL_TRUE : BOOL_FALSE;
    return (sys_product_profile_context_validate(
                &_service.status.context, require_table_crc) == BOOL_TRUE &&
            _service.status.context.calibration_voltage_01v ==
                bound_voltage_01v) ? BOOL_TRUE : BOOL_FALSE;
}

boolean_en sys_calibration_service_get_calibrated_max_current_ma(
    u16 bound_voltage_01v,
    u16 *calibrated_max_current_ma)
{
    if (calibrated_max_current_ma == NULL ||
        _service.status.context_valid != BOOL_TRUE ||
        _service.status.context.calibrated_max_current_ma == 0U ||
        sys_calibration_service_runtime_context_matches_voltage(
            bound_voltage_01v) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    *calibrated_max_current_ma =
        _service.status.context.calibrated_max_current_ma;
    return BOOL_TRUE;
}

boolean_en sys_calibration_service_get_calibrated_target_current_ma(
    u16 bound_voltage_01v,
    u16 *calibrated_target_current_ma)
{
    if (calibrated_target_current_ma == NULL ||
        _service.status.context_valid != BOOL_TRUE ||
        _service.status.context.configured_rated_current_ma == 0U ||
        _service.status.context.calibrated_max_current_ma == 0U ||
        sys_calibration_service_runtime_context_matches_voltage(
            bound_voltage_01v) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    *calibrated_target_current_ma =
        _service.status.context.configured_rated_current_ma;
    return BOOL_TRUE;
}

boolean_en sys_calibration_service_apply_fullscale_gain_pwm(
    u16 nominal_pwm,
    u16 pwm_limit,
    u16 *corrected_pwm)
{
    u32 scaled_pwm;
    u16 characterized_i_max_ma;
    u16 target_current_ma;

    if (corrected_pwm == NULL || pwm_limit == 0U || nominal_pwm > pwm_limit)
    {
        return BOOL_FALSE;
    }
    *corrected_pwm = nominal_pwm;
    if (_service.status.context_valid != BOOL_TRUE ||
        _service.status.context.calibrated_max_current_ma == 0U ||
        _service.status.context.configured_rated_current_ma == 0U)
    {
        return BOOL_TRUE;
    }
    characterized_i_max_ma =
        _service.status.context.calibrated_max_current_ma;
    target_current_ma =
        _service.status.context.configured_rated_current_ma;
    scaled_pwm = ((u32)nominal_pwm * (u32)target_current_ma +
                  (u32)characterized_i_max_ma / 2U) /
                 (u32)characterized_i_max_ma;
    if (scaled_pwm > pwm_limit)
    {
        /* The characterized device does not have enough PWM headroom to
           reach SET_OUTCUR. Do not drive at the hardware ceiling and hope
           the later 11-point table rejects it; fail off immediately. */
        return BOOL_FALSE;
    }
    *corrected_pwm = (u16)scaled_pwm;
    return BOOL_TRUE;
}

boolean_en sys_calibration_service_correct_output_percent(
    u8 requested_percent,
    u16 rated_current_ma,
    u8 *corrected_percent)
{
    sys_calibration_driver_table_st table;
    u16 calibrated_max_current_ma;
    u16 target_ma;
    u16 level;
    u8 index;

    if (corrected_percent == NULL || requested_percent > 100U ||
        rated_current_ma == 0U)
    {
        return BOOL_FALSE;
    }
    *corrected_percent = requested_percent;
    if (requested_percent == 0U ||
        sys_calibration_service_get_runtime_table(&table) != BOOL_TRUE ||
        sys_calibration_service_validate_table(
            &table, &calibrated_max_current_ma) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    target_ma = (u16)(((u32)requested_percent * rated_current_ma) / 100U);
    level = 200U;
    for (index = 1U; index < SYS_CALIBRATION_DRIVER_POINT_COUNT; ++index)
    {
        if (target_ma <= table.point[index].instrument_output_current_ma)
        {
            level = sys_calibration_service_interpolate_u16(
                target_ma,
                table.point[index - 1U].instrument_output_current_ma,
                table.point[index].instrument_output_current_ma,
                table.point[index - 1U].level,
                table.point[index].level);
            break;
        }
    }
    if (level > 200U)
    {
        level = 200U;
    }
    *corrected_percent = (u8)((level + 1U) / 2U);
    return BOOL_TRUE;
}

boolean_en sys_calibration_service_correct_output_current(
    u16 device_current_ma,
    u16 *corrected_current_ma)
{
    sys_calibration_driver_table_st table;
    u16 calibrated_max_current_ma;
    u8 index;

    if (corrected_current_ma == NULL)
    {
        return BOOL_FALSE;
    }
    *corrected_current_ma = device_current_ma;
    if (sys_calibration_service_get_runtime_table(&table) != BOOL_TRUE ||
        sys_calibration_service_validate_table(
            &table, &calibrated_max_current_ma) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (device_current_ma <= table.point[0].device_output_current_ma)
    {
        *corrected_current_ma = table.point[0].instrument_output_current_ma;
        return BOOL_TRUE;
    }
    for (index = 1U; index < SYS_CALIBRATION_DRIVER_POINT_COUNT; ++index)
    {
        if (device_current_ma <= table.point[index].device_output_current_ma)
        {
            *corrected_current_ma = sys_calibration_service_interpolate_u16(
                device_current_ma,
                table.point[index - 1U].device_output_current_ma,
                table.point[index].device_output_current_ma,
                table.point[index - 1U].instrument_output_current_ma,
                table.point[index].instrument_output_current_ma);
            return BOOL_TRUE;
        }
    }
    *corrected_current_ma =
        table.point[SYS_CALIBRATION_DRIVER_POINT_COUNT - 1U]
            .instrument_output_current_ma;
    return BOOL_TRUE;
}

sys_calibration_result_en sys_calibration_service_begin_context_seq(
    u32 session_id,
    u32 now_ms,
    u32 lease_ms,
    u32 seq,
    const sys_calibration_context_st *context,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en replay_result;

    if (sys_calibration_service_check_replay(
            session_id, seq, SYS_CALIBRATION_OP_BEGIN, &replay_result,
            status) == BOOL_TRUE)
    {
        return replay_result;
    }
    if (session_id == 0U || context == NULL || context->table_crc32 != 0U ||
        context->calibrated_max_current_ma != 0U ||
        sys_product_profile_context_validate(context, BOOL_FALSE) != BOOL_TRUE ||
        _service.get_bound_voltage == NULL ||
        context->calibration_voltage_01v != _service.get_bound_voltage() ||
        sys_calibration_service_validate_lease(lease_ms) != BOOL_TRUE)
    {
        sys_calibration_service_safe_off();
        sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_BEGIN,
                                       SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH,
                                       status);
        return SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH;
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
    if (_service.persistence_ready != BOOL_TRUE || _service.set_inhibit == NULL ||
        _service.set_inhibit(BOOL_TRUE) != BOOL_TRUE)
    {
        sys_calibration_service_safe_off();
        _service.boot_inhibit_active = BOOL_TRUE;
        sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_BEGIN,
                                       SYS_CALIBRATION_RESULT_FLASH_GATED, status);
        return SYS_CALIBRATION_RESULT_FLASH_GATED;
    }
    if (_service.status.session_id != session_id ||
        _service.status.state != SYS_CALIBRATION_STATE_ACTIVE)
    {
        _service.cache_valid = BOOL_FALSE;
        _service.status.last_request_seq = 0U;
    }
    sys_calibration_service_safe_off();
    _service.status.session_id = session_id;
    _service.status.lease_deadline_ms = now_ms + lease_ms;
    _service.status.state = SYS_CALIBRATION_STATE_ACTIVE;
    _service.status.current_level = 0U;
    _service.status.staged_valid = BOOL_FALSE;
    _service.status.staged_length = 0U;
    _service.status.context = *context;
    _service.status.context_valid = BOOL_TRUE;
    _service.boot_inhibit_active = BOOL_TRUE;
    sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_BEGIN,
                                   SYS_CALIBRATION_RESULT_OK, status);
    return SYS_CALIBRATION_RESULT_OK;
}

sys_calibration_result_en sys_calibration_service_begin_seq(
    u32 session_id,
    u32 now_ms,
    u32 lease_ms,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    return sys_calibration_service_begin_context_seq(
        session_id, now_ms, lease_ms, seq, NULL, status);
}

sys_calibration_result_en sys_calibration_service_heartbeat_seq(
    u32 session_id,
    u32 now_ms,
    u32 lease_ms,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;

    if (sys_calibration_service_check_replay(
            session_id, seq, SYS_CALIBRATION_OP_HEARTBEAT, &result,
            status) == BOOL_TRUE)
    {
        return result;
    }

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

    if (sys_calibration_service_check_replay(
            session_id, seq, SYS_CALIBRATION_OP_SET_POINT, &result,
            status) == BOOL_TRUE)
    {
        return result;
    }

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
        (_service.safety_ready != BOOL_TRUE || _service.set_level == NULL))
    {
        sys_calibration_service_safe_off();
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_SET_POINT,
                                       SYS_CALIBRATION_RESULT_SAFETY_NOT_READY,
                                       status);
        return SYS_CALIBRATION_RESULT_SAFETY_NOT_READY;
    }
    _service.status.current_level = level;
    if (_service.set_level != NULL && _service.set_level(level) != BOOL_TRUE)
    {
        sys_calibration_service_safe_off();
        _service.status.state = SYS_CALIBRATION_STATE_FAULT;
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_SET_POINT,
                                       SYS_CALIBRATION_RESULT_HARDWARE_FAULT,
                                       status);
        return SYS_CALIBRATION_RESULT_HARDWARE_FAULT;
    }
    sys_calibration_service_finish(session_id, seq,
                                   SYS_CALIBRATION_OP_SET_POINT,
                                   SYS_CALIBRATION_RESULT_OK, status);
    return SYS_CALIBRATION_RESULT_OK;
}

sys_calibration_result_en sys_calibration_service_set_validation_percent_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    u8 target_percent,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;
    u8 calibrated_percent;
    u16 output_level;

    if (sys_calibration_service_check_replay(
            session_id, seq, SYS_CALIBRATION_OP_SET_VALIDATION_PERCENT,
            &result, status) == BOOL_TRUE)
    {
        return result;
    }
    if (target_percent == 0U || target_percent >= 100U)
    {
        sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_SET_VALIDATION_PERCENT,
            SYS_CALIBRATION_RESULT_PROTOCOL_ERROR, status);
        return SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
    }
    result = sys_calibration_service_require_session(
        session_id, now_ms, SYS_CALIBRATION_OP_SET_VALIDATION_PERCENT,
        seq, status);
    if (result != SYS_CALIBRATION_RESULT_OK)
    {
        return result;
    }
    if (_service.status.state != SYS_CALIBRATION_STATE_APPLIED ||
        _service.status.context_valid != BOOL_TRUE ||
        _service.status.staged_valid != BOOL_TRUE ||
        _service.safety_ready != BOOL_TRUE || _service.set_level == NULL ||
        sys_calibration_service_correct_output_percent(
            target_percent,
            _service.status.context.configured_rated_current_ma,
            &calibrated_percent) != BOOL_TRUE)
    {
        sys_calibration_service_safe_off();
        sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_SET_VALIDATION_PERCENT,
            SYS_CALIBRATION_RESULT_INVALID_STATE, status);
        return SYS_CALIBRATION_RESULT_INVALID_STATE;
    }
    output_level = (u16)calibrated_percent * 2U;
    if (_service.set_level(output_level) != BOOL_TRUE)
    {
        sys_calibration_service_safe_off();
        _service.status.state = SYS_CALIBRATION_STATE_FAULT;
        _service.boot_inhibit_active = BOOL_TRUE;
        sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_SET_VALIDATION_PERCENT,
            SYS_CALIBRATION_RESULT_HARDWARE_FAULT, status);
        return SYS_CALIBRATION_RESULT_HARDWARE_FAULT;
    }
    _service.status.current_level = output_level;
    sys_calibration_service_finish(
        session_id, seq, SYS_CALIBRATION_OP_SET_VALIDATION_PERCENT,
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
    const sys_product_profile_st *profile = sys_product_profile_current();
    sys_calibration_driver_message_st message;
    sys_calibration_driver_max_context_st max_context;
    sys_calibration_result_en result;
    u16 iv_limit_ma = 0U;
    u32 index;

    if (sys_calibration_service_check_replay(
            session_id, seq, SYS_CALIBRATION_OP_RAW, &result, status) ==
        BOOL_TRUE)
    {
        return result;
    }
    if (direction != SYS_CALIBRATION_RAW_SET || frame == NULL ||
        sys_calibration_driver_decode(frame, frame_length, &message) != BOOL_TRUE ||
        message.command != SYS_CALIBRATION_DRIVER_CMD_SET ||
        message.offset != SYS_CALIBRATION_DRIVER_OFFSET_MAX_CONTEXT ||
        sys_calibration_driver_max_context_decode(
            message.data, message.length, &max_context) != BOOL_TRUE)
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
    if (_service.status.state != SYS_CALIBRATION_STATE_ACTIVE ||
        _service.status.context.calibrated_max_current_ma != 0U ||
        sys_product_profile_is_complete(profile) != BOOL_TRUE)
    {
        sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_RAW,
                                       SYS_CALIBRATION_RESULT_INVALID_STATE,
                                       status);
        return SYS_CALIBRATION_RESULT_INVALID_STATE;
    }
    for (index = 0U; index < profile->iv_limit_count; ++index)
    {
        if (_service.status.context.calibration_voltage_01v >=
                profile->iv_limits[index].voltage_01v)
        {
            iv_limit_ma = profile->iv_limits[index].current_ma;
        }
        else
        {
            break;
        }
    }
    if (max_context.input_ac_voltage_float_bits == 0U ||
        max_context.maximum_output_voltage_01v == 0U ||
        max_context.maximum_output_current_ma == 0U ||
        iv_limit_ma == 0U ||
        max_context.maximum_output_current_ma > iv_limit_ma ||
        max_context.maximum_output_current_ma > profile->hw_max_current_ma ||
        max_context.maximum_output_current_ma >= profile->absolute_fail_current_ma)
    {
        sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_RAW,
                                       SYS_CALIBRATION_RESULT_SAFETY_NOT_READY,
                                       status);
        return SYS_CALIBRATION_RESULT_SAFETY_NOT_READY;
    }

    /* Persist the raw pre-gain Level200 Imax. The gain is derived from
       SET_OUTCUR/Imax at runtime, so no new wire field or Flash coefficient
       is required and the old 0x07 protocol remains unchanged. */
    _service.status.context.calibrated_max_current_ma =
        max_context.maximum_output_current_ma;
    sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_RAW,
                                   SYS_CALIBRATION_RESULT_OK, status);
    return SYS_CALIBRATION_RESULT_OK;
}

sys_calibration_result_en sys_calibration_service_snapshot_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;

    if (sys_calibration_service_check_replay(
            session_id, seq, SYS_CALIBRATION_OP_RAW, &result, status) == BOOL_TRUE)
    {
        return result;
    }
    result = sys_calibration_service_require_session(
        session_id, now_ms, SYS_CALIBRATION_OP_RAW, seq, status);
    if (result != SYS_CALIBRATION_RESULT_OK)
    {
        return result;
    }
    sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_RAW,
                                   SYS_CALIBRATION_RESULT_OK, status);
    return SYS_CALIBRATION_RESULT_OK;
}

sys_calibration_result_en sys_calibration_service_stage_config_context_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    const sys_calibration_context_st *context,
    const u8 *payload,
    u16 length,
    sys_calibration_service_status_st *status)
{
    sys_calibration_driver_table_st table;
    sys_calibration_result_en result;
    u16 derived_max_current_ma;

    if (sys_calibration_service_check_replay(
            session_id, seq, SYS_CALIBRATION_OP_STAGE, &result, status) ==
        BOOL_TRUE)
    {
        return result;
    }

    if (context == NULL || payload == NULL ||
        length != SYS_CALIBRATION_STAGE_LENGTH ||
        sys_product_profile_context_validate(context, BOOL_TRUE) != BOOL_TRUE ||
        sys_product_profile_context_equal(
            context, &_service.status.context, BOOL_FALSE) != BOOL_TRUE ||
        context->table_crc32 != sys_calibration_storage_crc32(payload, length) ||
        sys_calibration_driver_table_decode(payload, length, &table) != BOOL_TRUE ||
        sys_calibration_service_validate_table(
            &table, &derived_max_current_ma) != BOOL_TRUE ||
        context->calibrated_max_current_ma != derived_max_current_ma)
    {
        sys_calibration_service_safe_off();
        _service.status.state = SYS_CALIBRATION_STATE_FAULT;
        _service.boot_inhibit_active = BOOL_TRUE;
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_STAGE,
                                       SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH,
                                       status);
        return SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH;
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
    _service.status.context = *context;
    _service.status.context_valid = BOOL_TRUE;
    _service.status.staged_valid = BOOL_TRUE;
    _service.status.state = SYS_CALIBRATION_STATE_STAGED;
    sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_STAGE,
                                   SYS_CALIBRATION_RESULT_OK, status);
    return SYS_CALIBRATION_RESULT_OK;
}

sys_calibration_result_en sys_calibration_service_stage_config_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    const u8 *payload,
    u16 length,
    sys_calibration_service_status_st *status)
{
    return sys_calibration_service_stage_config_context_seq(
        session_id, now_ms, seq, NULL, payload, length, status);
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
    if (sys_product_profile_context_validate(&_service.status.context,
                                             BOOL_TRUE) != BOOL_TRUE ||
        _service.status.context.table_crc32 != _service.status.staged_crc32)
    {
        sys_calibration_service_safe_off();
        _service.status.state = SYS_CALIBRATION_STATE_FAULT;
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_APPLY,
                                       SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH,
                                       status);
        return SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH;
    }
    /* 198字节为正式校准测量表；APPLY只切换候选状态，不伪造额外系数。 */
    _service.status.state = SYS_CALIBRATION_STATE_APPLIED;
    sys_calibration_service_finish(session_id, seq,
                                   SYS_CALIBRATION_OP_APPLY,
                                   SYS_CALIBRATION_RESULT_OK,
                                   status);
    return SYS_CALIBRATION_RESULT_OK;
}

sys_calibration_result_en sys_calibration_service_commit_context_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    const sys_calibration_context_st *context,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;

    if (context == NULL ||
        sys_product_profile_context_validate(context, BOOL_TRUE) != BOOL_TRUE ||
        sys_product_profile_context_equal(context, &_service.status.context,
                                          BOOL_TRUE) != BOOL_TRUE)
    {
        sys_calibration_service_safe_off();
        _service.status.state = SYS_CALIBRATION_STATE_FAULT;
        _service.boot_inhibit_active = BOOL_TRUE;
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_COMMIT,
                                       SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH,
                                       status);
        return SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH;
    }
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
    if (_service.commit == NULL ||
        _service.commit(&_service.status.context, _service.staged_payload,
                        _service.status.staged_length,
                        &_service.status.committed_generation) != BOOL_TRUE)
    {
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_COMMIT,
                                       SYS_CALIBRATION_RESULT_FLASH_GATED,
                                       status);
        return SYS_CALIBRATION_RESULT_FLASH_GATED;
    }
    memcpy(_service.committed_payload, _service.staged_payload,
           _service.status.staged_length);
    _service.committed_context = _service.status.context;
    _service.status.committed_crc32 = _service.status.staged_crc32;
    _service.status.committed_valid = BOOL_TRUE;
    sys_calibration_service_finish(session_id, seq,
                                   SYS_CALIBRATION_OP_COMMIT,
                                   SYS_CALIBRATION_RESULT_OK,
                                   status);
    return SYS_CALIBRATION_RESULT_OK;
}

sys_calibration_result_en sys_calibration_service_commit_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    (void)now_ms;
    sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_COMMIT,
                                   SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH,
                                   status);
    return SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH;
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
    if (_service.status.staged_valid != BOOL_TRUE &&
        _service.status.committed_valid != BOOL_TRUE)
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
    sys_calibration_result_en replay_result;

    if (sys_calibration_service_check_replay(
            session_id, seq, SYS_CALIBRATION_OP_ABORT, &replay_result,
            status) == BOOL_TRUE)
    {
        return replay_result;
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
    if (_service.set_inhibit == NULL ||
        _service.set_inhibit(BOOL_FALSE) != BOOL_TRUE)
    {
        _service.status.state = SYS_CALIBRATION_STATE_FAULT;
        _service.boot_inhibit_active = BOOL_TRUE;
        sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_ABORT,
                                       SYS_CALIBRATION_RESULT_FLASH_GATED, status);
        return SYS_CALIBRATION_RESULT_FLASH_GATED;
    }
    _service.status.state = SYS_CALIBRATION_STATE_IDLE;
    _service.status.staged_valid = BOOL_FALSE;
    _service.status.staged_length = 0U;
    sys_calibration_service_restore_committed_context();
    _service.boot_inhibit_active = BOOL_FALSE;
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
    sys_calibration_result_en replay_result;

    if (sys_calibration_service_check_replay(
            session_id, seq, SYS_CALIBRATION_OP_RELEASE, &replay_result,
            status) == BOOL_TRUE)
    {
        return replay_result;
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
    if (_service.set_inhibit == NULL ||
        _service.set_inhibit(BOOL_FALSE) != BOOL_TRUE)
    {
        _service.boot_inhibit_active = BOOL_TRUE;
        _service.status.state = SYS_CALIBRATION_STATE_FAULT;
        sys_calibration_service_finish(session_id, seq,
                                       SYS_CALIBRATION_OP_RELEASE,
                                       SYS_CALIBRATION_RESULT_FLASH_GATED,
                                       status);
        return SYS_CALIBRATION_RESULT_FLASH_GATED;
    }
    _service.boot_inhibit_active = BOOL_FALSE;
    _service.status.state = SYS_CALIBRATION_STATE_IDLE;
    _service.status.session_id = 0U;
    _service.status.current_level = 0U;
    _service.status.staged_valid = BOOL_FALSE;
    _service.status.staged_length = 0U;
    sys_calibration_service_restore_committed_context();
    sys_calibration_service_finish(session_id, seq,
                                   SYS_CALIBRATION_OP_RELEASE,
                                   SYS_CALIBRATION_RESULT_OK,
                                   status);
    return SYS_CALIBRATION_RESULT_OK;
}

void sys_calibration_service_force_fault(void)
{
    sys_calibration_service_safe_off();
    _service.status.state = SYS_CALIBRATION_STATE_FAULT;
    _service.status.last_result = SYS_CALIBRATION_RESULT_HARDWARE_FAULT;
    ++_service.status.result_seq;
    _service.safety_ready = BOOL_FALSE;
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
        _service.safety_ready = BOOL_FALSE;
        _service.boot_inhibit_active = BOOL_TRUE;
        _service.cache_valid = BOOL_FALSE;
    }
    return sys_calibration_service_copy_status(status);
}
