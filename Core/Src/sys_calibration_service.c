/*************************************************************
程序功能：Calibration MQTT V3会话、完整244B Correction与运行时应用
开发环境：Keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
*************************************************************/
#include "sys_calibration_service.h"

#include "sys_bl0942.h"
#include "sys_calibration_curve.h"
#include "sys_calibration_snapshot.h"
#include "sys_product_profile.h"
#include "sys_temp_over_protect.h"
#include "sys_Vo_Io.h"
#include <string.h>

extern boolean_en sys_calibration_flash_set_inhibit(boolean_en active);
extern boolean_en sys_calibration_flash_commit_v3(
    const u8 payload[SYS_CALIBRATION_PAYLOAD_LENGTH],
    u16 length,
    u32 payload_crc32,
    u32 *generation);

#define CALP_PROFILE_ID_OFFSET             0x08U
#define CALP_PROFILE_VERSION_OFFSET        0x0AU
#define CALP_FINGERPRINT_OFFSET            0x0CU
#define CALP_OUTPUT_OFFSET \
    SYS_CALIBRATION_PAYLOAD_OUTPUT_OFFSET
#define CALP_OCO_OFFSET \
    SYS_CALIBRATION_PAYLOAD_OCO_OFFSET
#define CALP_BL_CURRENT_OFFSET \
    SYS_CALIBRATION_PAYLOAD_BL_CURRENT_OFFSET
#define CALP_BL_POWER_OFFSET \
    SYS_CALIBRATION_PAYLOAD_BL_POWER_OFFSET
#define CALP_VOLTAGE_GAIN_OFFSET \
    SYS_CALIBRATION_PAYLOAD_BL_VOLTAGE_OFFSET

#define CALP_OUTPUT_POINT_SIZE                4U
#define CALP_OCO_POINT_SIZE                   4U
#define CALP_BL_POINT_SIZE                    6U

typedef enum
{
    SYS_CALIBRATION_OP_NONE = 0,
    SYS_CALIBRATION_OP_BEGIN,
    SYS_CALIBRATION_OP_HEARTBEAT,
    SYS_CALIBRATION_OP_SET_POINT,
    SYS_CALIBRATION_OP_RAW,
    SYS_CALIBRATION_OP_STAGE,
    SYS_CALIBRATION_OP_APPLY,
    SYS_CALIBRATION_OP_SET_OUTPUT,
    SYS_CALIBRATION_OP_COMMIT,
    SYS_CALIBRATION_OP_READ,
    SYS_CALIBRATION_OP_ABORT,
    SYS_CALIBRATION_OP_RELEASE
} sys_calibration_operation_en;

static sys_calibration_service_status_st _status;
static u8 _staged_payload[SYS_CALIBRATION_PAYLOAD_LENGTH];
static u8 _committed_payload[SYS_CALIBRATION_PAYLOAD_LENGTH];

static sys_calibration_safe_off_fn _safe_off;
static sys_calibration_set_level_fn _set_level;
static sys_calibration_set_output_fn _set_output;
static sys_calibration_set_inhibit_fn _set_inhibit;
static sys_calibration_commit_v3_fn _commit_v3;

static boolean_en _replay_valid;
static u32 _replay_session_id;
static u32 _replay_seq;
static sys_calibration_operation_en _replay_operation;
static sys_calibration_result_en _replay_result;
static u32 _replay_signature;
static u32 _request_signature;
static u32 _expired_session_id;

static u16 sys_calibration_get_u16_le(const u8 *data)
{
    return (u16)data[0] | ((u16)data[1] << 8U);
}

static u32 sys_calibration_get_u32_le(const u8 *data)
{
    return (u32)data[0] | ((u32)data[1] << 8U) |
           ((u32)data[2] << 16U) | ((u32)data[3] << 24U);
}

static s32 sys_calibration_get_s32_le(const u8 *data)
{
    return (s32)sys_calibration_get_u32_le(data);
}

static void sys_calibration_counter_increment(u32 *counter)
{
    if (counter != NULL && *counter < 0xFFFFFFFFUL)
    {
        ++(*counter);
    }
}

static u32 sys_calibration_service_signature_mix(u32 signature, u32 value)
{
    u8 index;

    for (index = 0U; index < 4U; ++index)
    {
        signature ^= (u8)(value >> ((u32)index * 8U));
        signature *= 16777619UL;
    }
    return signature;
}

static u16 sys_calibration_service_fault_flags(void)
{
    u16 flags = 0U;

    if (Error_1_OL != 0U)
    {
        flags |= SYS_CALIBRATION_FAULT_OUTPUT_OVERLOAD;
    }
    if (Error_Out_LV != 0U)
    {
        flags |= SYS_CALIBRATION_FAULT_OUTPUT_LOW_VOLTAGE;
    }
    if (Error_3_OV != 0U)
    {
        flags |= SYS_CALIBRATION_FAULT_INPUT_OVERVOLTAGE;
    }
    if (Error_4_LV != 0U)
    {
        flags |= SYS_CALIBRATION_FAULT_INPUT_UNDERVOLTAGE;
    }
    if (sys_temp_over_protect_state != SYS_TEMP_OVER_PROTECT_STATE_IDLE)
    {
        flags |= SYS_CALIBRATION_FAULT_OVER_TEMPERATURE;
    }
    return flags;
}

static void sys_calibration_service_safe_off(void)
{
    if (_safe_off != NULL)
    {
        _safe_off();
    }
    _status.current_level = 0U;
    _status.current_percent = 0U;
    _status.actual_pwm = 0U;
}

static boolean_en sys_calibration_service_copy_status(
    sys_calibration_service_status_st *status)
{
    _status.fault_flags = sys_calibration_service_fault_flags();
    if (status == NULL)
    {
        return BOOL_FALSE;
    }
    *status = _status;
    return BOOL_TRUE;
}

static void sys_calibration_service_cache_result(
    u32 session_id,
    u32 seq,
    sys_calibration_operation_en operation,
    sys_calibration_result_en result)
{
    _replay_valid = BOOL_TRUE;
    _replay_session_id = session_id;
    _replay_seq = seq;
    _replay_operation = operation;
    _replay_result = result;
    _replay_signature = _request_signature;
}

static boolean_en sys_calibration_service_get_replay(
    u32 session_id,
    u32 seq,
    sys_calibration_operation_en operation,
    sys_calibration_result_en *result,
    sys_calibration_service_status_st *status)
{
    if (_replay_valid != BOOL_TRUE || result == NULL ||
        _replay_session_id != session_id || _replay_seq != seq ||
        _replay_operation != operation)
    {
        return BOOL_FALSE;
    }
    *result = (_replay_signature == _request_signature) ?
              _replay_result : SYS_CALIBRATION_RESULT_BAD_REQUEST;
    (void)sys_calibration_service_copy_status(status);
    if (status != NULL && _replay_signature != _request_signature)
    {
        status->last_result = SYS_CALIBRATION_RESULT_BAD_REQUEST;
        status->result_seq = seq;
    }
    return BOOL_TRUE;
}

static sys_calibration_result_en sys_calibration_service_finish(
    u32 session_id,
    u32 seq,
    sys_calibration_operation_en operation,
    sys_calibration_result_en result,
    sys_calibration_service_status_st *status)
{
    _status.last_result = result;
    _status.result_seq = seq;
    if (_status.session_id == session_id && seq > _status.last_request_seq)
    {
        _status.last_request_seq = seq;
    }
    sys_calibration_service_cache_result(session_id, seq, operation, result);
    (void)sys_calibration_service_copy_status(status);
    return result;
}

static boolean_en sys_calibration_service_lease_expired(u32 now_ms)
{
    return (_status.state != SYS_CALIBRATION_STATE_IDLE &&
            (s32)(now_ms - _status.lease_deadline_ms) >= 0) ?
           BOOL_TRUE : BOOL_FALSE;
}

static void sys_calibration_service_clear_session(void)
{
    _status.state = SYS_CALIBRATION_STATE_IDLE;
    _status.session_id = 0U;
    _status.lease_deadline_ms = 0U;
    _status.lease_ms = 0U;
    _status.last_request_seq = 0U;
    _status.current_level = 0U;
    _status.current_percent = 0U;
    _status.actual_pwm = 0U;
    _status.staged_valid = BOOL_FALSE;
    _status.staged_length = 0U;
    _status.staged_crc32 = 0U;
}

static void sys_calibration_service_expire(u32 session_id)
{
    boolean_en inhibit_cleared = BOOL_FALSE;

    sys_calibration_service_safe_off();
    _status.staged_valid = BOOL_FALSE;
    if (_set_inhibit != NULL)
    {
        inhibit_cleared = _set_inhibit(BOOL_FALSE);
    }
    _status.boot_inhibit_active = (inhibit_cleared == BOOL_TRUE) ?
                                  BOOL_FALSE : BOOL_TRUE;
    if (inhibit_cleared != BOOL_TRUE)
    {
        _status.persistence_ready = BOOL_FALSE;
    }
    _expired_session_id = session_id;
    _replay_valid = BOOL_FALSE;
    sys_calibration_service_clear_session();
    _status.last_result = (inhibit_cleared == BOOL_TRUE) ?
                          SYS_CALIBRATION_RESULT_SESSION_EXPIRED :
                          SYS_CALIBRATION_RESULT_FLASH_ERROR;
}

static boolean_en sys_calibration_service_state_allowed(u16 state_mask)
{
    return ((state_mask & (u16)(1U << (u16)_status.state)) != 0U) ?
           BOOL_TRUE : BOOL_FALSE;
}

static sys_calibration_result_en sys_calibration_service_require_session(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    u16 allowed_states)
{
    if (session_id == 0U || seq == 0U)
    {
        return SYS_CALIBRATION_RESULT_BAD_REQUEST;
    }
    if (_status.state == SYS_CALIBRATION_STATE_IDLE)
    {
        return (session_id == _expired_session_id) ?
               SYS_CALIBRATION_RESULT_SESSION_EXPIRED :
               SYS_CALIBRATION_RESULT_BAD_STATE;
    }
    if (session_id != _status.session_id)
    {
        return SYS_CALIBRATION_RESULT_BAD_STATE;
    }
    if (sys_calibration_service_lease_expired(now_ms) == BOOL_TRUE)
    {
        sys_calibration_service_expire(session_id);
        return SYS_CALIBRATION_RESULT_SESSION_EXPIRED;
    }
    if (sys_calibration_service_state_allowed(allowed_states) != BOOL_TRUE)
    {
        return SYS_CALIBRATION_RESULT_BAD_STATE;
    }
    if (seq <= _status.last_request_seq)
    {
        return SYS_CALIBRATION_RESULT_BAD_REQUEST;
    }
    return SYS_CALIBRATION_RESULT_OK;
}

static const u8 *sys_calibration_service_runtime_payload(void)
{
    if (_status.state == SYS_CALIBRATION_STATE_APPLIED &&
        _status.staged_valid == BOOL_TRUE)
    {
        return _staged_payload;
    }
    return (_status.committed_valid == BOOL_TRUE) ? _committed_payload : NULL;
}

static sys_calibration_result_en sys_calibration_service_validate_payload(
    const u8 *payload,
    u16 length,
    u32 payload_crc32)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    u16 output_ref[SYS_CALIBRATION_POINT_COUNT];
    u16 previous_oco_raw = 0U;
    u16 previous_oco_ref = 0U;
    u32 previous_bl_current_raw = 0U;
    s32 previous_bl_power_raw = 0;
    u16 previous_bl_current_ref = 0U;
    u16 previous_bl_power_ref = 0U;
    u32 index;

    if (payload == NULL || length != SYS_CALIBRATION_PAYLOAD_LENGTH)
    {
        return SYS_CALIBRATION_RESULT_BAD_REQUEST;
    }
    if (sys_calibration_payload_crc32_iso_hdlc(payload, length) !=
        payload_crc32)
    {
        return SYS_CALIBRATION_RESULT_CRC_ERROR;
    }
    if (memcmp(payload, "CALP", 4U) != 0 ||
        sys_calibration_get_u16_le(payload + 0x04U) !=
            SYS_CALIBRATION_PAYLOAD_VERSION ||
        sys_calibration_get_u16_le(payload + 0x06U) !=
            SYS_CALIBRATION_PAYLOAD_LENGTH ||
        payload[0x10U] != SYS_CALIBRATION_POINT_COUNT ||
        payload[0x11U] != SYS_CALIBRATION_LEVEL_STEP ||
        sys_calibration_get_u16_le(payload + 0x12U) !=
            SYS_CALIBRATION_REQUIRED_SECTIONS)
    {
        return SYS_CALIBRATION_RESULT_BAD_REQUEST;
    }
    if (sys_product_profile_is_complete(profile) != BOOL_TRUE ||
        sys_calibration_get_u16_le(payload + CALP_PROFILE_ID_OFFSET) !=
            profile->profile_id ||
        sys_calibration_get_u16_le(payload + CALP_PROFILE_VERSION_OFFSET) !=
            profile->profile_version ||
        sys_calibration_get_u32_le(payload + CALP_FINGERPRINT_OFFSET) !=
            profile->fingerprint_crc32)
    {
        return SYS_CALIBRATION_RESULT_PROFILE_MISMATCH;
    }

    for (index = 0U; index < SYS_CALIBRATION_POINT_COUNT; ++index)
    {
        u32 output_offset = CALP_OUTPUT_OFFSET +
                            index * CALP_OUTPUT_POINT_SIZE;
        u32 oco_offset = CALP_OCO_OFFSET + index * CALP_OCO_POINT_SIZE;
        u32 bl_i_offset = CALP_BL_CURRENT_OFFSET +
                          index * CALP_BL_POINT_SIZE;
        u32 bl_p_offset = CALP_BL_POWER_OFFSET +
                          index * CALP_BL_POINT_SIZE;
        u16 logical_pwm = sys_calibration_get_u16_le(payload + output_offset);
        u16 oco_raw = sys_calibration_get_u16_le(payload + oco_offset);
        u16 oco_ref = sys_calibration_get_u16_le(payload + oco_offset + 2U);
        u32 bl_current_raw = sys_calibration_get_u32_le(payload + bl_i_offset);
        s32 bl_power_raw = sys_calibration_get_s32_le(payload + bl_p_offset);
        u16 bl_current_ref = sys_calibration_get_u16_le(
            payload + bl_i_offset + 4U);
        u16 bl_power_ref = sys_calibration_get_u16_le(
            payload + bl_p_offset + 4U);

        output_ref[index] = sys_calibration_get_u16_le(
            payload + output_offset + 2U);
        if (logical_pwm != (u16)(index * 100U) ||
            output_ref[index] > profile->hw_max_current_ma ||
            oco_ref > profile->hw_max_current_ma ||
            bl_current_raw > 0x00FFFFFFUL ||
            bl_power_raw < -8388608L || bl_power_raw > 8388607L)
        {
            return SYS_CALIBRATION_RESULT_RANGE_ERROR;
        }
        if (index > 0U &&
            (output_ref[index] <= output_ref[index - 1U] ||
             oco_raw <= previous_oco_raw ||
             oco_ref < previous_oco_ref ||
             bl_current_raw <= previous_bl_current_raw ||
             bl_power_raw <= previous_bl_power_raw ||
             bl_current_ref < previous_bl_current_ref ||
             bl_power_ref < previous_bl_power_ref))
        {
            return SYS_CALIBRATION_RESULT_RANGE_ERROR;
        }
        previous_oco_raw = oco_raw;
        previous_oco_ref = oco_ref;
        previous_bl_current_raw = bl_current_raw;
        previous_bl_power_raw = bl_power_raw;
        previous_bl_current_ref = bl_current_ref;
        previous_bl_power_ref = bl_power_ref;
    }
    if (sys_calibration_get_u32_le(payload + CALP_VOLTAGE_GAIN_OFFSET) == 0U)
    {
        return SYS_CALIBRATION_RESULT_RANGE_ERROR;
    }
    return SYS_CALIBRATION_RESULT_OK;
}

static void sys_calibration_service_load_output_curve(
    const u8 *payload,
    u16 *reference_current,
    u16 *logical_pwm)
{
    u32 index;
    for (index = 0U; index < SYS_CALIBRATION_POINT_COUNT; ++index)
    {
        u32 offset = CALP_OUTPUT_OFFSET + index * CALP_OUTPUT_POINT_SIZE;
        logical_pwm[index] = sys_calibration_get_u16_le(payload + offset);
        reference_current[index] = sys_calibration_get_u16_le(
            payload + offset + 2U);
    }
}

static void sys_calibration_service_load_oco_curve(
    const u8 *payload,
    u16 *raw,
    u16 *reference)
{
    u32 index;
    for (index = 0U; index < SYS_CALIBRATION_POINT_COUNT; ++index)
    {
        u32 offset = CALP_OCO_OFFSET + index * CALP_OCO_POINT_SIZE;
        raw[index] = sys_calibration_get_u16_le(payload + offset);
        reference[index] = sys_calibration_get_u16_le(payload + offset + 2U);
    }
}

static void sys_calibration_service_load_bl_current_curve(
    const u8 *payload,
    u32 *raw,
    u16 *reference)
{
    u32 index;
    for (index = 0U; index < SYS_CALIBRATION_POINT_COUNT; ++index)
    {
        u32 offset = CALP_BL_CURRENT_OFFSET + index * CALP_BL_POINT_SIZE;
        raw[index] = sys_calibration_get_u32_le(payload + offset);
        reference[index] = sys_calibration_get_u16_le(payload + offset + 4U);
    }
}

static void sys_calibration_service_load_bl_power_curve(
    const u8 *payload,
    s32 *raw,
    u16 *reference)
{
    u32 index;
    for (index = 0U; index < SYS_CALIBRATION_POINT_COUNT; ++index)
    {
        u32 offset = CALP_BL_POWER_OFFSET + index * CALP_BL_POINT_SIZE;
        raw[index] = sys_calibration_get_s32_le(payload + offset);
        reference[index] = sys_calibration_get_u16_le(payload + offset + 4U);
    }
}

static sys_calibration_result_en sys_calibration_service_build_raw(
    u32 now_ms,
    sys_calibration_raw_st *raw)
{
    sys_calibration_snapshot_aggregate_st snapshot;
    u16 required_raw_flags = SYS_CALIBRATION_RAW_PWM_VALID |
                             SYS_CALIBRATION_RAW_OCO_VALID |
                             SYS_CALIBRATION_RAW_BL_V_VALID |
                             SYS_CALIBRATION_RAW_BL_I_VALID |
                             SYS_CALIBRATION_RAW_BL_P_VALID |
                             SYS_CALIBRATION_RAW_BL_FRESH;
    u16 blocking_faults;

    if (raw == NULL)
    {
        return SYS_CALIBRATION_RESULT_BAD_REQUEST;
    }
    memset(raw, 0, sizeof(*raw));
    (void)sys_calibration_snapshot_read_aggregate(now_ms, &snapshot);
    raw->level = _status.current_level;
    raw->bl_age_ms = snapshot.meter_age_ms;
    raw->fault_flags = sys_calibration_service_fault_flags();

    if (snapshot.pwm.valid_flags != 0U)
    {
        raw->actual_pwm = snapshot.pwm.logical_pwm;
        raw->valid_flags |= SYS_CALIBRATION_RAW_PWM_VALID;
    }
    if (snapshot.adc.valid_flags != 0U)
    {
        raw->oco_raw = snapshot.adc.iout_raw;
        raw->output_voltage_01v = (u16)(((u32)snapshot.adc.vout_raw * 53U) /
                                        100U);
        raw->valid_flags |= SYS_CALIBRATION_RAW_OCO_VALID |
                            SYS_CALIBRATION_RAW_VO_VALID;
        if (sys_calibration_service_correct_output_current_raw(
                raw->oco_raw, &raw->corrected_output_current_ma) == BOOL_TRUE)
        {
            raw->valid_flags |= SYS_CALIBRATION_RAW_OUTPUT_CORRECTED;
        }
    }
    if (snapshot.meter.valid_flags != 0U)
    {
        raw->bl_voltage_raw = snapshot.meter.v_rms_raw;
        raw->bl_current_raw = snapshot.meter.i_rms_raw;
        raw->bl_power_raw = snapshot.meter.watt_raw;
        raw->valid_flags |= SYS_CALIBRATION_RAW_BL_V_VALID |
                            SYS_CALIBRATION_RAW_BL_I_VALID |
                            SYS_CALIBRATION_RAW_BL_P_VALID;
        if (sys_calibration_service_correct_bl_voltage(
                raw->bl_voltage_raw,
                &raw->corrected_input_voltage_01v) == BOOL_TRUE)
        {
            raw->valid_flags |= SYS_CALIBRATION_RAW_BL_V_CORRECTED;
        }
        if (sys_calibration_service_correct_bl_current(
                raw->bl_current_raw,
                &raw->corrected_input_current_ma) == BOOL_TRUE)
        {
            raw->valid_flags |= SYS_CALIBRATION_RAW_BL_I_CORRECTED;
        }
        if (sys_calibration_service_correct_bl_power(
                raw->bl_power_raw,
                &raw->corrected_input_power_01w) == BOOL_TRUE)
        {
            raw->valid_flags |= SYS_CALIBRATION_RAW_BL_P_CORRECTED;
        }
    }
    if (snapshot.bl_fresh != 0U)
    {
        raw->valid_flags |= SYS_CALIBRATION_RAW_BL_FRESH;
    }
    _status.actual_pwm = raw->actual_pwm;
    _status.fault_flags = raw->fault_flags;

    if ((raw->valid_flags & required_raw_flags) != required_raw_flags)
    {
        return SYS_CALIBRATION_RESULT_DATA_STALE;
    }
    blocking_faults = raw->fault_flags;
    if (raw->actual_pwm == 0U)
    {
        blocking_faults &= (u16)~SYS_CALIBRATION_FAULT_OUTPUT_LOW_VOLTAGE;
    }
    return (blocking_faults != 0U) ?
           SYS_CALIBRATION_RESULT_HARDWARE_FAULT :
           SYS_CALIBRATION_RESULT_OK;
}

void sys_calibration_service_init(void)
{
    memset(&_status, 0, sizeof(_status));
    _status.state = SYS_CALIBRATION_STATE_IDLE;
    _status.last_result = SYS_CALIBRATION_RESULT_OK;
    _set_inhibit = sys_calibration_flash_set_inhibit;
    _commit_v3 = sys_calibration_flash_commit_v3;
    _safe_off = NULL;
    _set_level = NULL;
    _set_output = NULL;
    _replay_valid = BOOL_FALSE;
    _expired_session_id = 0U;
}

void sys_calibration_service_bind_output(
    sys_calibration_safe_off_fn safe_off,
    sys_calibration_set_level_fn set_level,
    sys_calibration_set_output_fn set_output)
{
    _safe_off = safe_off;
    _set_level = set_level;
    _set_output = set_output;
}

void sys_calibration_service_bind_storage(
    sys_calibration_set_inhibit_fn set_inhibit,
    sys_calibration_commit_v3_fn commit_v3)
{
    _set_inhibit = (set_inhibit != NULL) ? set_inhibit :
                   sys_calibration_flash_set_inhibit;
    _commit_v3 = (commit_v3 != NULL) ? commit_v3 :
                 sys_calibration_flash_commit_v3;
}

void sys_calibration_service_restore_boot(boolean_en inhibited,
                                          boolean_en persistence_ready)
{
    _status.persistence_ready = persistence_ready;
    _status.boot_inhibit_active = inhibited;
    if (inhibited == BOOL_TRUE || persistence_ready != BOOL_TRUE)
    {
        sys_calibration_service_safe_off();
    }
}

boolean_en sys_calibration_service_load_committed_v3(
    const u8 payload[SYS_CALIBRATION_PAYLOAD_LENGTH],
    u16 length,
    u32 payload_crc32,
    u32 generation)
{
    if (sys_calibration_service_validate_payload(payload, length,
                                                  payload_crc32) !=
            SYS_CALIBRATION_RESULT_OK ||
        generation == 0U)
    {
        _status.committed_valid = BOOL_FALSE;
        _status.committed_length = 0U;
        _status.committed_crc32 = 0U;
        _status.committed_generation = 0U;
        return BOOL_FALSE;
    }
    memcpy(_committed_payload, payload, SYS_CALIBRATION_PAYLOAD_LENGTH);
    _status.committed_valid = BOOL_TRUE;
    _status.committed_length = SYS_CALIBRATION_PAYLOAD_LENGTH;
    _status.committed_crc32 = payload_crc32;
    _status.committed_generation = generation;
    return BOOL_TRUE;
}

void sys_calibration_service_set_safety_ready(boolean_en ready)
{
    _status.safety_ready = ready;
    if (ready != BOOL_TRUE)
    {
        sys_calibration_service_safe_off();
    }
}

boolean_en sys_calibration_service_get_status(
    sys_calibration_service_status_st *status)
{
    return sys_calibration_service_copy_status(status);
}

boolean_en sys_calibration_service_is_boot_inhibited(void)
{
    return _status.boot_inhibit_active;
}

boolean_en sys_calibration_service_is_output_authorized(void)
{
    return (_status.boot_inhibit_active == BOOL_TRUE &&
            _status.session_id != 0U &&
            (_status.state == SYS_CALIBRATION_STATE_ACTIVE ||
             _status.state == SYS_CALIBRATION_STATE_APPLIED)) ?
           BOOL_TRUE : BOOL_FALSE;
}

sys_calibration_result_en sys_calibration_service_begin_seq(
    u32 session_id,
    u32 now_ms,
    u32 lease_ms,
    u32 seq,
    u16 profile_id,
    u32 profile_fingerprint,
    sys_calibration_service_status_st *status)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    sys_calibration_result_en replay;

    _request_signature = 2166136261UL;
    _request_signature = sys_calibration_service_signature_mix(
        _request_signature, lease_ms);
    _request_signature = sys_calibration_service_signature_mix(
        _request_signature, profile_id);
    _request_signature = sys_calibration_service_signature_mix(
        _request_signature, profile_fingerprint);
    if (sys_calibration_service_get_replay(session_id, seq,
                                            SYS_CALIBRATION_OP_BEGIN,
                                            &replay, status) == BOOL_TRUE)
    {
        return replay;
    }
    if (session_id == 0U || seq != 1U)
    {
        return sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_BEGIN,
            SYS_CALIBRATION_RESULT_BAD_REQUEST, status);
    }
    if (session_id == _expired_session_id)
    {
        return sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_BEGIN,
            SYS_CALIBRATION_RESULT_SESSION_EXPIRED, status);
    }
    if (lease_ms < SYS_CALIBRATION_LEASE_MIN_MS ||
        lease_ms > SYS_CALIBRATION_LEASE_MAX_MS)
    {
        return sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_BEGIN,
            SYS_CALIBRATION_RESULT_RANGE_ERROR, status);
    }
    if (_status.state != SYS_CALIBRATION_STATE_IDLE)
    {
        return sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_BEGIN,
            SYS_CALIBRATION_RESULT_BUSY, status);
    }
    if (sys_product_profile_is_complete(profile) != BOOL_TRUE ||
        profile_id != profile->profile_id ||
        profile_fingerprint != profile->fingerprint_crc32)
    {
        return sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_BEGIN,
            SYS_CALIBRATION_RESULT_PROFILE_MISMATCH, status);
    }
    if (_status.persistence_ready != BOOL_TRUE || _set_inhibit == NULL)
    {
        return sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_BEGIN,
            SYS_CALIBRATION_RESULT_FLASH_ERROR, status);
    }
    if (_status.safety_ready != BOOL_TRUE || _safe_off == NULL ||
        _set_level == NULL || _set_output == NULL ||
        (sys_calibration_service_fault_flags() &
         (u16)~SYS_CALIBRATION_FAULT_OUTPUT_LOW_VOLTAGE) != 0U)
    {
        return sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_BEGIN,
            SYS_CALIBRATION_RESULT_HARDWARE_FAULT, status);
    }

    sys_calibration_service_safe_off();
    if (_set_inhibit(BOOL_TRUE) != BOOL_TRUE)
    {
        _status.persistence_ready = BOOL_FALSE;
        _status.boot_inhibit_active = BOOL_TRUE;
        return sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_BEGIN,
            SYS_CALIBRATION_RESULT_FLASH_ERROR, status);
    }
    _status.state = SYS_CALIBRATION_STATE_ACTIVE;
    _status.session_id = session_id;
    _status.lease_ms = lease_ms;
    _status.lease_deadline_ms = now_ms + lease_ms;
    _status.last_request_seq = seq;
    _status.staged_valid = BOOL_FALSE;
    _status.staged_length = 0U;
    _status.staged_crc32 = 0U;
    _status.boot_inhibit_active = BOOL_TRUE;
    _expired_session_id = 0U;
    return sys_calibration_service_finish(
        session_id, seq, SYS_CALIBRATION_OP_BEGIN,
        SYS_CALIBRATION_RESULT_OK, status);
}

sys_calibration_result_en sys_calibration_service_heartbeat_seq(
    u32 session_id,
    u32 now_ms,
    u32 lease_ms,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;
    sys_calibration_result_en replay;
    u16 allowed = (u16)((1U << SYS_CALIBRATION_STATE_ACTIVE) |
                        (1U << SYS_CALIBRATION_STATE_STAGED) |
                        (1U << SYS_CALIBRATION_STATE_APPLIED) |
                        (1U << SYS_CALIBRATION_STATE_COMMITTED));

    _request_signature = sys_calibration_service_signature_mix(
        2166136261UL, lease_ms);
    if (sys_calibration_service_get_replay(session_id, seq,
                                            SYS_CALIBRATION_OP_HEARTBEAT,
                                            &replay, status) == BOOL_TRUE)
    {
        return replay;
    }
    if (lease_ms < SYS_CALIBRATION_LEASE_MIN_MS ||
        lease_ms > SYS_CALIBRATION_LEASE_MAX_MS)
    {
        return sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_HEARTBEAT,
            SYS_CALIBRATION_RESULT_RANGE_ERROR, status);
    }
    result = sys_calibration_service_require_session(session_id, now_ms, seq,
                                                      allowed);
    if (result == SYS_CALIBRATION_RESULT_OK)
    {
        _status.lease_ms = lease_ms;
        _status.lease_deadline_ms = now_ms + lease_ms;
    }
    return sys_calibration_service_finish(
        session_id, seq, SYS_CALIBRATION_OP_HEARTBEAT, result, status);
}

sys_calibration_result_en sys_calibration_service_set_point_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    u16 level,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;
    sys_calibration_result_en replay;
    u16 actual_pwm = 0U;

    _request_signature = sys_calibration_service_signature_mix(
        2166136261UL, level);
    if (sys_calibration_service_get_replay(session_id, seq,
                                            SYS_CALIBRATION_OP_SET_POINT,
                                            &replay, status) == BOOL_TRUE)
    {
        return replay;
    }
    if (sys_calibration_curve_validate_level(level) != BOOL_TRUE)
    {
        if (_status.session_id == session_id)
        {
            sys_calibration_service_safe_off();
        }
        return sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_SET_POINT,
            SYS_CALIBRATION_RESULT_RANGE_ERROR, status);
    }
    result = sys_calibration_service_require_session(
        session_id, now_ms, seq, (u16)(1U << SYS_CALIBRATION_STATE_ACTIVE));
    if (result != SYS_CALIBRATION_RESULT_OK)
    {
        return sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_SET_POINT, result, status);
    }
    if (level > 0U && sys_bl0942_is_fresh(now_ms) != BOOL_TRUE)
    {
        sys_calibration_service_safe_off();
        return sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_SET_POINT,
            SYS_CALIBRATION_RESULT_DATA_STALE, status);
    }
    if (_set_level == NULL || _set_level(level, &actual_pwm) != BOOL_TRUE ||
        actual_pwm != (u16)(level * 5U))
    {
        sys_calibration_service_safe_off();
        return sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_SET_POINT,
            SYS_CALIBRATION_RESULT_HARDWARE_FAULT, status);
    }
    _status.current_level = level;
    _status.current_percent = (u8)(level / 2U);
    _status.actual_pwm = actual_pwm;
    return sys_calibration_service_finish(
        session_id, seq, SYS_CALIBRATION_OP_SET_POINT,
        SYS_CALIBRATION_RESULT_OK, status);
}

sys_calibration_result_en sys_calibration_service_raw_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_raw_st *raw,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;
    sys_calibration_result_en replay;
    u16 allowed = (u16)((1U << SYS_CALIBRATION_STATE_ACTIVE) |
                        (1U << SYS_CALIBRATION_STATE_APPLIED));

    _request_signature = 2166136261UL;
    if (sys_calibration_service_get_replay(session_id, seq,
                                            SYS_CALIBRATION_OP_RAW,
                                            &replay, status) == BOOL_TRUE)
    {
        (void)sys_calibration_service_build_raw(now_ms, raw);
        return replay;
    }
    result = sys_calibration_service_require_session(session_id, now_ms, seq,
                                                      allowed);
    if (result == SYS_CALIBRATION_RESULT_OK)
    {
        result = sys_calibration_service_build_raw(now_ms, raw);
        if (result == SYS_CALIBRATION_RESULT_DATA_STALE ||
            result == SYS_CALIBRATION_RESULT_HARDWARE_FAULT)
        {
            sys_calibration_service_safe_off();
        }
    }
    return sys_calibration_service_finish(
        session_id, seq, SYS_CALIBRATION_OP_RAW, result, status);
}

sys_calibration_result_en sys_calibration_service_stage_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    const u8 payload[SYS_CALIBRATION_PAYLOAD_LENGTH],
    u16 length,
    u32 payload_crc32,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;
    sys_calibration_result_en replay;

    _request_signature = sys_calibration_service_signature_mix(
        2166136261UL, length);
    _request_signature = sys_calibration_service_signature_mix(
        _request_signature, payload_crc32);
    if (sys_calibration_service_get_replay(session_id, seq,
                                            SYS_CALIBRATION_OP_STAGE,
                                            &replay, status) == BOOL_TRUE)
    {
        return replay;
    }
    result = sys_calibration_service_require_session(
        session_id, now_ms, seq, (u16)(1U << SYS_CALIBRATION_STATE_ACTIVE));
    if (result == SYS_CALIBRATION_RESULT_OK)
    {
        result = sys_calibration_service_validate_payload(payload, length,
                                                            payload_crc32);
        sys_calibration_service_safe_off();
    }
    if (result == SYS_CALIBRATION_RESULT_OK)
    {
        memcpy(_staged_payload, payload, SYS_CALIBRATION_PAYLOAD_LENGTH);
        _status.staged_valid = BOOL_TRUE;
        _status.staged_length = SYS_CALIBRATION_PAYLOAD_LENGTH;
        _status.staged_crc32 = payload_crc32;
        _status.state = SYS_CALIBRATION_STATE_STAGED;
    }
    return sys_calibration_service_finish(
        session_id, seq, SYS_CALIBRATION_OP_STAGE, result, status);
}

sys_calibration_result_en sys_calibration_service_apply_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;
    sys_calibration_result_en replay;

    _request_signature = 2166136261UL;
    if (sys_calibration_service_get_replay(session_id, seq,
                                            SYS_CALIBRATION_OP_APPLY,
                                            &replay, status) == BOOL_TRUE)
    {
        return replay;
    }
    result = sys_calibration_service_require_session(
        session_id, now_ms, seq, (u16)(1U << SYS_CALIBRATION_STATE_STAGED));
    if (result == SYS_CALIBRATION_RESULT_OK &&
        _status.staged_valid != BOOL_TRUE)
    {
        result = SYS_CALIBRATION_RESULT_BAD_STATE;
    }
    if (result == SYS_CALIBRATION_RESULT_OK)
    {
        sys_calibration_service_safe_off();
        _status.state = SYS_CALIBRATION_STATE_APPLIED;
    }
    return sys_calibration_service_finish(
        session_id, seq, SYS_CALIBRATION_OP_APPLY, result, status);
}

sys_calibration_result_en sys_calibration_service_set_output_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    u8 percent,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;
    sys_calibration_result_en replay;
    u16 actual_pwm = 0U;

    _request_signature = sys_calibration_service_signature_mix(
        2166136261UL, percent);
    if (sys_calibration_service_get_replay(session_id, seq,
                                            SYS_CALIBRATION_OP_SET_OUTPUT,
                                            &replay, status) == BOOL_TRUE)
    {
        return replay;
    }
    if (percent > 100U)
    {
        if (_status.session_id == session_id)
        {
            sys_calibration_service_safe_off();
        }
        return sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_SET_OUTPUT,
            SYS_CALIBRATION_RESULT_RANGE_ERROR, status);
    }
    result = sys_calibration_service_require_session(
        session_id, now_ms, seq, (u16)(1U << SYS_CALIBRATION_STATE_APPLIED));
    if (result == SYS_CALIBRATION_RESULT_OK && percent > 0U &&
        sys_bl0942_is_fresh(now_ms) != BOOL_TRUE)
    {
        sys_calibration_service_safe_off();
        result = SYS_CALIBRATION_RESULT_DATA_STALE;
    }
    if (result == SYS_CALIBRATION_RESULT_OK &&
        (_set_output == NULL ||
         _set_output(percent, &actual_pwm) != BOOL_TRUE))
    {
        sys_calibration_service_safe_off();
        result = SYS_CALIBRATION_RESULT_HARDWARE_FAULT;
    }
    if (result == SYS_CALIBRATION_RESULT_OK)
    {
        _status.current_percent = percent;
        _status.current_level = (u16)percent * 2U;
        _status.actual_pwm = actual_pwm;
    }
    return sys_calibration_service_finish(
        session_id, seq, SYS_CALIBRATION_OP_SET_OUTPUT, result, status);
}

sys_calibration_result_en sys_calibration_service_commit_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;
    sys_calibration_result_en replay;
    u32 generation = 0U;

    _request_signature = 2166136261UL;
    if (sys_calibration_service_get_replay(session_id, seq,
                                            SYS_CALIBRATION_OP_COMMIT,
                                            &replay, status) == BOOL_TRUE)
    {
        return replay;
    }
    result = sys_calibration_service_require_session(
        session_id, now_ms, seq, (u16)(1U << SYS_CALIBRATION_STATE_APPLIED));
    if (result == SYS_CALIBRATION_RESULT_OK &&
        (_status.staged_valid != BOOL_TRUE || _commit_v3 == NULL))
    {
        result = SYS_CALIBRATION_RESULT_BAD_STATE;
    }
    if (result == SYS_CALIBRATION_RESULT_OK)
    {
        sys_calibration_service_safe_off();
        if (_commit_v3(_staged_payload, SYS_CALIBRATION_PAYLOAD_LENGTH,
                       _status.staged_crc32, &generation) != BOOL_TRUE ||
            generation == 0U)
        {
            result = SYS_CALIBRATION_RESULT_FLASH_ERROR;
        }
    }
    if (result == SYS_CALIBRATION_RESULT_OK)
    {
        memcpy(_committed_payload, _staged_payload,
               SYS_CALIBRATION_PAYLOAD_LENGTH);
        _status.committed_valid = BOOL_TRUE;
        _status.committed_length = SYS_CALIBRATION_PAYLOAD_LENGTH;
        _status.committed_crc32 = _status.staged_crc32;
        _status.committed_generation = generation;
        _status.staged_valid = BOOL_FALSE;
        _status.staged_length = 0U;
        _status.staged_crc32 = 0U;
        _status.state = SYS_CALIBRATION_STATE_COMMITTED;
    }
    return sys_calibration_service_finish(
        session_id, seq, SYS_CALIBRATION_OP_COMMIT, result, status);
}

sys_calibration_result_en sys_calibration_service_read_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    u8 *payload,
    u16 capacity,
    u16 *length,
    u32 *payload_crc32,
    u32 *generation,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;
    sys_calibration_result_en replay;

    _request_signature = 2166136261UL;
    if (payload == NULL || length == NULL || payload_crc32 == NULL ||
        generation == NULL || capacity < SYS_CALIBRATION_PAYLOAD_LENGTH)
    {
        return sys_calibration_service_finish(
            session_id, seq, SYS_CALIBRATION_OP_READ,
            SYS_CALIBRATION_RESULT_BAD_REQUEST, status);
    }
    if (sys_calibration_service_get_replay(session_id, seq,
                                            SYS_CALIBRATION_OP_READ,
                                            &replay, status) == BOOL_TRUE)
    {
        if (replay == SYS_CALIBRATION_RESULT_OK)
        {
            memcpy(payload, _committed_payload,
                   SYS_CALIBRATION_PAYLOAD_LENGTH);
            *length = SYS_CALIBRATION_PAYLOAD_LENGTH;
            *payload_crc32 = _status.committed_crc32;
            *generation = _status.committed_generation;
        }
        return replay;
    }
    result = sys_calibration_service_require_session(
        session_id, now_ms, seq,
        (u16)(1U << SYS_CALIBRATION_STATE_COMMITTED));
    if (result == SYS_CALIBRATION_RESULT_OK &&
        _status.committed_valid != BOOL_TRUE)
    {
        result = SYS_CALIBRATION_RESULT_BAD_STATE;
    }
    if (result == SYS_CALIBRATION_RESULT_OK)
    {
        memcpy(payload, _committed_payload, SYS_CALIBRATION_PAYLOAD_LENGTH);
        *length = SYS_CALIBRATION_PAYLOAD_LENGTH;
        *payload_crc32 = _status.committed_crc32;
        *generation = _status.committed_generation;
    }
    return sys_calibration_service_finish(
        session_id, seq, SYS_CALIBRATION_OP_READ, result, status);
}

sys_calibration_result_en sys_calibration_service_abort_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;
    sys_calibration_result_en replay;
    u16 allowed = (u16)((1U << SYS_CALIBRATION_STATE_ACTIVE) |
                        (1U << SYS_CALIBRATION_STATE_STAGED) |
                        (1U << SYS_CALIBRATION_STATE_APPLIED));

    _request_signature = 2166136261UL;
    if (sys_calibration_service_get_replay(session_id, seq,
                                            SYS_CALIBRATION_OP_ABORT,
                                            &replay, status) == BOOL_TRUE)
    {
        return replay;
    }
    result = sys_calibration_service_require_session(session_id, now_ms, seq,
                                                      allowed);
    if (result == SYS_CALIBRATION_RESULT_OK)
    {
        sys_calibration_service_safe_off();
        _status.staged_valid = BOOL_FALSE;
        if (_set_inhibit == NULL || _set_inhibit(BOOL_FALSE) != BOOL_TRUE)
        {
            _status.persistence_ready = BOOL_FALSE;
            result = SYS_CALIBRATION_RESULT_FLASH_ERROR;
        }
        else
        {
            _status.boot_inhibit_active = BOOL_FALSE;
            sys_calibration_service_clear_session();
        }
    }
    return sys_calibration_service_finish(
        session_id, seq, SYS_CALIBRATION_OP_ABORT, result, status);
}

sys_calibration_result_en sys_calibration_service_release_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    sys_calibration_result_en result;
    sys_calibration_result_en replay;

    _request_signature = 2166136261UL;
    if (sys_calibration_service_get_replay(session_id, seq,
                                            SYS_CALIBRATION_OP_RELEASE,
                                            &replay, status) == BOOL_TRUE)
    {
        return replay;
    }
    result = sys_calibration_service_require_session(
        session_id, now_ms, seq,
        (u16)(1U << SYS_CALIBRATION_STATE_COMMITTED));
    if (result == SYS_CALIBRATION_RESULT_OK)
    {
        sys_calibration_service_safe_off();
        if (_set_inhibit == NULL || _set_inhibit(BOOL_FALSE) != BOOL_TRUE)
        {
            _status.persistence_ready = BOOL_FALSE;
            result = SYS_CALIBRATION_RESULT_FLASH_ERROR;
        }
        else
        {
            _status.boot_inhibit_active = BOOL_FALSE;
            sys_calibration_service_clear_session();
        }
    }
    return sys_calibration_service_finish(
        session_id, seq, SYS_CALIBRATION_OP_RELEASE, result, status);
}

boolean_en sys_calibration_service_timer(
    u32 now_ms,
    sys_calibration_service_status_st *status)
{
    boolean_en expired = BOOL_FALSE;

    if (sys_calibration_service_lease_expired(now_ms) == BOOL_TRUE)
    {
        sys_calibration_service_expire(_status.session_id);
        expired = BOOL_TRUE;
    }
    (void)sys_calibration_service_copy_status(status);
    return expired;
}

boolean_en sys_calibration_service_output_pwm_for_current(
    u16 target_current_ma,
    u16 *logical_pwm)
{
    const u8 *payload = sys_calibration_service_runtime_payload();
    u16 reference[SYS_CALIBRATION_POINT_COUNT];
    u16 pwm[SYS_CALIBRATION_POINT_COUNT];

    if (payload == NULL || logical_pwm == NULL)
    {
        return BOOL_FALSE;
    }
    sys_calibration_service_load_output_curve(payload, reference, pwm);
    if (sys_calibration_curve_interpolate_u16(
            reference, pwm, SYS_CALIBRATION_POINT_COUNT,
            target_current_ma, logical_pwm) != BOOL_TRUE)
    {
        sys_calibration_counter_increment(&_status.output_fallback_count);
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

boolean_en sys_calibration_service_correct_output_current_raw(
    u16 oco_raw,
    u16 *corrected_current_ma)
{
    const u8 *payload = sys_calibration_service_runtime_payload();
    u16 raw[SYS_CALIBRATION_POINT_COUNT];
    u16 reference[SYS_CALIBRATION_POINT_COUNT];

    if (payload == NULL || corrected_current_ma == NULL)
    {
        return BOOL_FALSE;
    }
    sys_calibration_service_load_oco_curve(payload, raw, reference);
    return sys_calibration_curve_interpolate_u16(
        raw, reference, SYS_CALIBRATION_POINT_COUNT,
        oco_raw, corrected_current_ma);
}

boolean_en sys_calibration_service_correct_bl_voltage(
    u32 bl_voltage_raw,
    u16 *corrected_voltage_01v)
{
    const u8 *payload = sys_calibration_service_runtime_payload();

    if (payload == NULL)
    {
        return BOOL_FALSE;
    }
    return sys_bl0942_voltage_apply_gain_q24(
        bl_voltage_raw,
        sys_calibration_get_u32_le(payload + CALP_VOLTAGE_GAIN_OFFSET),
        corrected_voltage_01v);
}

boolean_en sys_calibration_service_correct_bl_current(
    u32 bl_current_raw,
    u16 *corrected_current_ma)
{
    const u8 *payload = sys_calibration_service_runtime_payload();
    u32 raw[SYS_CALIBRATION_POINT_COUNT];
    u16 reference[SYS_CALIBRATION_POINT_COUNT];

    if (payload == NULL || corrected_current_ma == NULL)
    {
        return BOOL_FALSE;
    }
    sys_calibration_service_load_bl_current_curve(payload, raw, reference);
    return sys_calibration_curve_interpolate_u32(
        raw, reference, SYS_CALIBRATION_POINT_COUNT,
        bl_current_raw, corrected_current_ma);
}

boolean_en sys_calibration_service_correct_bl_power(
    s32 bl_power_raw,
    u16 *corrected_power_01w)
{
    const u8 *payload = sys_calibration_service_runtime_payload();
    s32 raw[SYS_CALIBRATION_POINT_COUNT];
    u16 reference[SYS_CALIBRATION_POINT_COUNT];

    if (payload == NULL || corrected_power_01w == NULL)
    {
        return BOOL_FALSE;
    }
    sys_calibration_service_load_bl_power_curve(payload, raw, reference);
    return sys_calibration_curve_interpolate_s32(
        raw, reference, SYS_CALIBRATION_POINT_COUNT,
        bl_power_raw, corrected_power_01w);
}

void sys_calibration_service_force_fault(void)
{
    sys_calibration_service_safe_off();
    _status.last_result = SYS_CALIBRATION_RESULT_HARDWARE_FAULT;
    _status.fault_flags = sys_calibration_service_fault_flags();
}
