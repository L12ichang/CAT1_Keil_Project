#include "current_calibration.h"
#include "current_cal_storage.h"
#include "Portable.h"
#include "sys_temp_over_protect.h"

#define CURRENT_CAL_DEFAULT_TIMEOUT_SEC 30U
#define CURRENT_CAL_MIN_TIMEOUT_SEC     10U
#define CURRENT_CAL_MAX_TIMEOUT_SEC     300U
#define CURRENT_CAL_OUTPUT_DWELL_MS     30000UL

typedef struct
{
    current_cal_state_en state;
    char session_id[CURRENT_CAL_SESSION_ID_MAX + 1U];
    u32 timeout_ms;
    u32 last_activity_tick;
    u32 output_start_tick;
    u32 received_bitmap;
    u8 point_index;
    u8 target_percent;
    current_cal_curve_t pending;
    boolean_en pending_metadata_set;
    boolean_en temporary_active;
    boolean_en last_command_valid;
    u32 last_seq;
    current_cal_action_en last_action;
    u32 last_digest;
    current_cal_result_en last_command_result;
    current_cal_result_en last_error;
} current_cal_context_t;

static current_cal_context_t current_cal_ctx;

static u8 current_cal_popcount21(u32 value)
{
    u8 count;

    count = 0U;
    value &= CURRENT_CAL_RECEIVED_ALL;
    while (value != 0U)
    {
        count = (u8)(count + (u8)(value & 1U));
        value >>= 1;
    }
    return count;
}

static void current_cal_clear_pending(void)
{
    memset(&current_cal_ctx.pending, 0, sizeof(current_cal_ctx.pending));
    current_cal_ctx.received_bitmap = 0U;
    current_cal_ctx.pending_metadata_set = BOOL_FALSE;
    current_cal_ctx.temporary_active = BOOL_FALSE;
}

static void current_cal_finish_session(void)
{
    sys_pwm_force_off();
    sys_pwm_calibration_unlock();
    current_cal_clear_pending();
    current_cal_ctx.state = CAL_STATE_IDLE;
    current_cal_ctx.timeout_ms = 0U;
    current_cal_ctx.output_start_tick = 0U;
}

void current_calibration_init(void)
{
    memset(&current_cal_ctx, 0, sizeof(current_cal_ctx));
    current_cal_ctx.state = CAL_STATE_IDLE;
    current_cal_storage_init();
}

void current_calibration_process(void)
{
    sys_pwm_status_t pwm;
    sys_vo_io_snapshot_t snapshot;
    u32 now;

    if (current_cal_ctx.state == CAL_STATE_IDLE)
    {
        return;
    }
    now = Timer_GetTickCount();
    if ((now - current_cal_ctx.last_activity_tick) >= current_cal_ctx.timeout_ms)
    {
        current_cal_ctx.last_error = CAL_TIMEOUT;
        current_cal_finish_session();
        return;
    }

    sys_pwm_get_status(&pwm);
    if (pwm.output_enabled == BOOL_TRUE)
    {
        if ((now - current_cal_ctx.output_start_tick) >= CURRENT_CAL_OUTPUT_DWELL_MS)
        {
            sys_pwm_force_off();
            current_cal_ctx.output_start_tick = 0U;
            return;
        }
        if (sys_vo_io_get_snapshot(&snapshot) != BOOL_TRUE)
        {
            current_cal_ctx.last_error = CAL_OUTPUT_NOT_STABLE;
            current_cal_finish_session();
            return;
        }
        if (sys_pwm_calibration_safety_ready() != BOOL_TRUE)
        {
            sys_pwm_get_status(&pwm);
            current_cal_ctx.last_error = (pwm.protect_code != 0U) ?
                                         CAL_PROTECT_ACTIVE : CAL_OUTPUT_NOT_STABLE;
            current_cal_finish_session();
            return;
        }
    }
}

boolean_en current_calibration_is_active(void)
{
    return (current_cal_ctx.state != CAL_STATE_IDLE) ? BOOL_TRUE : BOOL_FALSE;
}

current_cal_state_en current_calibration_state(void)
{
    return current_cal_ctx.state;
}

current_cal_result_en current_calibration_enter(const char *session_id,
                                                u32 seq,
                                                u32 digest,
                                                u32 profile_crc,
                                                u16 timeout_sec,
                                                boolean_en ota_busy)
{
    u32 length;

    if (session_id == NULL)
    {
        return CAL_INVALID_PARAM;
    }
    length = strlen(session_id);
    if (length == 0U || length > CURRENT_CAL_SESSION_ID_MAX || seq == 0U)
    {
        return CAL_INVALID_PARAM;
    }
    if (current_cal_ctx.state != CAL_STATE_IDLE)
    {
        return CAL_BUSY;
    }
    if (ota_busy == BOOL_TRUE)
    {
        return CAL_OTA_ACTIVE;
    }
    if (Error_3_OV != 0U || Error_1_OL != 0U || driver_temperarure_warn != 0U)
    {
        return CAL_PROTECT_ACTIVE;
    }
    if (profile_crc != current_cal_profile_crc())
    {
        return CAL_PROFILE_MISMATCH;
    }
    if (timeout_sec == 0U)
    {
        timeout_sec = CURRENT_CAL_DEFAULT_TIMEOUT_SEC;
    }
    if (timeout_sec < CURRENT_CAL_MIN_TIMEOUT_SEC ||
        timeout_sec > CURRENT_CAL_MAX_TIMEOUT_SEC)
    {
        return CAL_INVALID_PARAM;
    }

    current_cal_clear_pending();
    strncpy(current_cal_ctx.session_id, session_id, CURRENT_CAL_SESSION_ID_MAX);
    current_cal_ctx.session_id[CURRENT_CAL_SESSION_ID_MAX] = '\0';
    current_cal_ctx.timeout_ms = (u32)timeout_sec * 1000UL;
    current_cal_ctx.last_activity_tick = Timer_GetTickCount();
    current_cal_ctx.output_start_tick = 0U;
    current_cal_ctx.point_index = 0U;
    current_cal_ctx.target_percent = 0U;
    current_cal_ctx.state = CAL_STATE_READY;
    sys_pwm_calibration_lock();
    current_cal_ctx.last_command_valid = BOOL_TRUE;
    current_cal_ctx.last_seq = seq;
    current_cal_ctx.last_action = CAL_ACTION_ENTER;
    current_cal_ctx.last_digest = digest;
    current_cal_ctx.last_command_result = CAL_OK;
    current_cal_ctx.last_error = CAL_OK;
    return CAL_OK;
}

current_cal_result_en current_calibration_prepare_command(const char *session_id,
                                                          u32 seq,
                                                          current_cal_action_en action,
                                                          u32 digest,
                                                          boolean_en *duplicate)
{
    if (duplicate != NULL)
    {
        *duplicate = BOOL_FALSE;
    }
    if (seq == 0U)
    {
        return CAL_INVALID_PARAM;
    }
    if (session_id == NULL || strcmp(session_id, current_cal_ctx.session_id) != 0)
    {
        return CAL_INVALID_SESSION;
    }
    if (current_cal_ctx.last_command_valid == BOOL_TRUE)
    {
        if (seq < current_cal_ctx.last_seq)
        {
            return CAL_STALE_SEQ;
        }
        if (seq == current_cal_ctx.last_seq)
        {
            if (action == current_cal_ctx.last_action && digest == current_cal_ctx.last_digest)
            {
                if (duplicate != NULL)
                {
                    *duplicate = BOOL_TRUE;
                }
                return current_cal_ctx.last_command_result;
            }
            return CAL_SEQ_CONFLICT;
        }
    }
    if (current_cal_ctx.state == CAL_STATE_IDLE)
    {
        return CAL_INVALID_SESSION;
    }
    current_cal_ctx.last_activity_tick = Timer_GetTickCount();
    return CAL_OK;
}

void current_calibration_complete_command(u32 seq,
                                          current_cal_action_en action,
                                          u32 digest,
                                          current_cal_result_en result)
{
    current_cal_ctx.last_command_valid = BOOL_TRUE;
    current_cal_ctx.last_seq = seq;
    current_cal_ctx.last_action = action;
    current_cal_ctx.last_digest = digest;
    current_cal_ctx.last_command_result = result;
    if (result != CAL_OK)
    {
        current_cal_ctx.last_error = result;
    }
}

current_cal_result_en current_calibration_cached_result(void)
{
    return current_cal_ctx.last_command_result;
}

current_cal_result_en current_calibration_set_pwm(u8 point_index,
                                                  u8 target_percent,
                                                  u16 logical_pwm)
{
    boolean_en applied;
    sys_vo_io_snapshot_t snapshot;

    if (current_cal_ctx.state != CAL_STATE_READY &&
        current_cal_ctx.state != CAL_STATE_DIRECT_TEST)
    {
        return CAL_INVALID_STATE;
    }
    if (point_index >= CURRENT_CAL_POINT_COUNT ||
        target_percent != (u8)(point_index * 5U))
    {
        return CAL_INVALID_PARAM;
    }
    if ((point_index == 0U && logical_pwm != 0U) ||
        logical_pwm > current_cal_pwm_logical_max())
    {
        return CAL_PWM_OUT_OF_RANGE;
    }
    current_cal_ctx.point_index = point_index;
    current_cal_ctx.target_percent = target_percent;
    current_cal_ctx.state = CAL_STATE_DIRECT_TEST;
    if (logical_pwm != 0U && sys_vo_io_get_snapshot(&snapshot) != BOOL_TRUE)
    {
        sys_pwm_force_off();
        current_cal_ctx.output_start_tick = 0U;
        return CAL_OUTPUT_NOT_STABLE;
    }
    applied = sys_pwm_calibration_set_direct(logical_pwm);
    if (applied != BOOL_TRUE)
    {
        current_cal_ctx.output_start_tick = 0U;
        return CAL_PROTECT_ACTIVE;
    }
    current_cal_ctx.output_start_tick = (logical_pwm != 0U) ? Timer_GetTickCount() : 0U;
    return CAL_OK;
}

current_cal_result_en current_calibration_write_curve_chunk(u16 curve_version,
                                                            u32 profile_crc,
                                                            u32 curve_crc,
                                                            u8 start_index,
                                                            const u16 *values,
                                                            u8 value_count)
{
    u8 i;
    u8 index;
    u32 mask;

    if (current_cal_ctx.state != CAL_STATE_READY &&
        current_cal_ctx.state != CAL_STATE_DIRECT_TEST &&
        current_cal_ctx.state != CAL_STATE_CURVE_RECEIVING &&
        current_cal_ctx.state != CAL_STATE_CURVE_PENDING)
    {
        return CAL_INVALID_STATE;
    }
    if (values == NULL || value_count == 0U || value_count > 7U ||
        start_index >= CURRENT_CAL_POINT_COUNT ||
        (u16)start_index + value_count > CURRENT_CAL_POINT_COUNT)
    {
        return CAL_INVALID_PARAM;
    }
    if (profile_crc != current_cal_profile_crc())
    {
        return CAL_PROFILE_MISMATCH;
    }
    if (curve_version != CURRENT_CAL_CURVE_VERSION)
    {
        return CAL_INVALID_PARAM;
    }

    if (current_cal_ctx.pending_metadata_set != BOOL_TRUE)
    {
        memset(&current_cal_ctx.pending, 0, sizeof(current_cal_ctx.pending));
        current_cal_ctx.pending.curve_version = curve_version;
        current_cal_ctx.pending.point_count = CURRENT_CAL_POINT_COUNT;
        current_cal_ctx.pending.profile_crc = profile_crc;
        current_cal_ctx.pending.curve_crc = curve_crc;
        current_cal_ctx.pending_metadata_set = BOOL_TRUE;
    }
    else if (current_cal_ctx.pending.curve_version != curve_version ||
             current_cal_ctx.pending.profile_crc != profile_crc ||
             current_cal_ctx.pending.curve_crc != curve_crc)
    {
        return CAL_CHUNK_CONFLICT;
    }

    for (i = 0U; i < value_count; ++i)
    {
        index = (u8)(start_index + i);
        mask = 1UL << index;
        if ((current_cal_ctx.received_bitmap & mask) != 0U &&
            current_cal_ctx.pending.logical_pwm[index] != values[i])
        {
            return CAL_CHUNK_CONFLICT;
        }
    }
    for (i = 0U; i < value_count; ++i)
    {
        index = (u8)(start_index + i);
        current_cal_ctx.pending.logical_pwm[index] = values[i];
        current_cal_ctx.received_bitmap |= 1UL << index;
    }
    current_cal_ctx.state = (current_cal_ctx.received_bitmap == CURRENT_CAL_RECEIVED_ALL) ?
                            CAL_STATE_CURVE_PENDING : CAL_STATE_CURVE_RECEIVING;
    return CAL_OK;
}

static current_cal_result_en current_cal_validate_pending(void)
{
    current_cal_curve_result_en result;

    if (current_cal_ctx.received_bitmap != CURRENT_CAL_RECEIVED_ALL)
    {
        return CAL_CURVE_INCOMPLETE;
    }
    result = current_cal_curve_validate(&current_cal_ctx.pending, current_cal_profile_crc());
    if (result == CURRENT_CAL_CURVE_OUT_OF_RANGE)
    {
        return CAL_PWM_OUT_OF_RANGE;
    }
    if (result == CURRENT_CAL_CURVE_NOT_MONOTONIC ||
        result == CURRENT_CAL_CURVE_BAD_ZERO)
    {
        return CAL_CURVE_NOT_MONOTONIC;
    }
    if (result == CURRENT_CAL_CURVE_CRC_MISMATCH)
    {
        return CAL_CURVE_CRC_ERROR;
    }
    if (result == CURRENT_CAL_CURVE_PROFILE_MISMATCH)
    {
        return CAL_PROFILE_MISMATCH;
    }
    return (result == CURRENT_CAL_CURVE_OK) ? CAL_OK : CAL_INVALID_PARAM;
}

current_cal_result_en current_calibration_apply_temporary(u32 curve_crc)
{
    current_cal_result_en result;

    if (current_cal_ctx.state != CAL_STATE_CURVE_PENDING)
    {
        return CAL_INVALID_STATE;
    }
    if (curve_crc != current_cal_ctx.pending.curve_crc)
    {
        return CAL_CURVE_CRC_ERROR;
    }
    result = current_cal_validate_pending();
    if (result != CAL_OK)
    {
        return result;
    }
    sys_pwm_force_off();
    current_cal_ctx.temporary_active = BOOL_TRUE;
    current_cal_ctx.state = CAL_STATE_TEMP_APPLIED;
    return CAL_OK;
}

current_cal_result_en current_calibration_set_test_percent(u8 percent)
{
    boolean_en applied;
    sys_vo_io_snapshot_t snapshot;

    if (current_cal_ctx.state != CAL_STATE_TEMP_APPLIED ||
        current_cal_ctx.temporary_active != BOOL_TRUE)
    {
        return CAL_INVALID_STATE;
    }
    if (percent > 100U)
    {
        return CAL_INVALID_PARAM;
    }
    current_cal_ctx.point_index = (u8)(percent / 5U);
    current_cal_ctx.target_percent = percent;
    if (percent != 0U && sys_vo_io_get_snapshot(&snapshot) != BOOL_TRUE)
    {
        sys_pwm_force_off();
        current_cal_ctx.output_start_tick = 0U;
        return CAL_OUTPUT_NOT_STABLE;
    }
    applied = sys_pwm_calibration_set_percent(&current_cal_ctx.pending, percent);
    current_cal_ctx.output_start_tick = (applied == BOOL_TRUE && percent != 0U) ?
                                        Timer_GetTickCount() : 0U;
    return (applied == BOOL_TRUE) ? CAL_OK : CAL_PROTECT_ACTIVE;
}

current_cal_result_en current_calibration_commit(u32 profile_crc, u32 curve_crc)
{
    current_cal_result_en result;

    if (current_cal_ctx.state != CAL_STATE_TEMP_APPLIED)
    {
        return CAL_INVALID_STATE;
    }
    if (profile_crc != current_cal_profile_crc())
    {
        return CAL_PROFILE_MISMATCH;
    }
    if (curve_crc != current_cal_ctx.pending.curve_crc)
    {
        return CAL_CURVE_CRC_ERROR;
    }
    sys_pwm_force_off();
    result = current_cal_validate_pending();
    if (result != CAL_OK)
    {
        return result;
    }
    current_cal_ctx.state = CAL_STATE_COMMITTING;
    if (current_cal_storage_commit(&current_cal_ctx.pending) != BOOL_TRUE)
    {
        current_cal_ctx.state = CAL_STATE_TEMP_APPLIED;
        return CAL_FLASH_WRITE_ERROR;
    }
    current_cal_ctx.state = CAL_STATE_COMMITTED;
    return CAL_OK;
}

current_cal_result_en current_calibration_abort(void)
{
    if (current_cal_ctx.state == CAL_STATE_IDLE)
    {
        return CAL_INVALID_STATE;
    }
    current_cal_finish_session();
    return CAL_OK;
}

current_cal_result_en current_calibration_exit(void)
{
    if (current_cal_ctx.state != CAL_STATE_COMMITTED)
    {
        return CAL_INVALID_STATE;
    }
    current_cal_finish_session();
    return CAL_OK;
}

void current_calibration_get_status(current_cal_status_t *status)
{
    const current_cal_curve_t *active_curve;
    u32 now;

    if (status == NULL)
    {
        return;
    }
    memset(status, 0, sizeof(*status));
    status->state = current_cal_ctx.state;
    strncpy(status->session_id, current_cal_ctx.session_id, CURRENT_CAL_SESSION_ID_MAX);
    status->profile_crc = current_cal_profile_crc();
    active_curve = current_cal_storage_active_curve();
    if (current_cal_ctx.pending_metadata_set == BOOL_TRUE)
    {
        status->curve_version = current_cal_ctx.pending.curve_version;
        status->curve_crc = current_cal_ctx.pending.curve_crc;
    }
    else if (active_curve != NULL)
    {
        status->curve_version = active_curve->curve_version;
        status->curve_crc = active_curve->curve_crc;
    }
    status->storage_sequence = current_cal_storage_sequence();
    status->received_bitmap = current_cal_ctx.received_bitmap;
    status->missing_bitmap = CURRENT_CAL_RECEIVED_ALL & ~current_cal_ctx.received_bitmap;
    status->received_count = current_cal_popcount21(current_cal_ctx.received_bitmap);
    status->point_count = CURRENT_CAL_POINT_COUNT;
    status->point_index = current_cal_ctx.point_index;
    status->target_percent = current_cal_ctx.target_percent;
    status->last_error = current_cal_ctx.last_error;
    status->pending_valid = (current_cal_validate_pending() == CAL_OK) ? BOOL_TRUE : BOOL_FALSE;
    status->active_curve_valid = current_cal_storage_has_active_curve();
    sys_pwm_get_status(&status->pwm);
    status->measurement_valid = sys_vo_io_get_snapshot(&status->measurement);
    if (current_cal_ctx.state != CAL_STATE_IDLE)
    {
        now = Timer_GetTickCount();
        if ((now - current_cal_ctx.last_activity_tick) < current_cal_ctx.timeout_ms)
        {
            status->timeout_remaining_ms = current_cal_ctx.timeout_ms -
                                           (now - current_cal_ctx.last_activity_tick);
        }
    }
}

const current_cal_curve_t *current_calibration_pending_curve(void)
{
    return (current_cal_ctx.pending_metadata_set == BOOL_TRUE) ? &current_cal_ctx.pending : NULL;
}

const char *current_calibration_state_name(current_cal_state_en state)
{
    switch (state)
    {
        case CAL_STATE_IDLE: return "IDLE";
        case CAL_STATE_READY: return "READY";
        case CAL_STATE_DIRECT_TEST: return "DIRECT_TEST";
        case CAL_STATE_CURVE_RECEIVING: return "CURVE_RECEIVING";
        case CAL_STATE_CURVE_PENDING: return "CURVE_PENDING";
        case CAL_STATE_TEMP_APPLIED: return "TEMP_APPLIED";
        case CAL_STATE_COMMITTING: return "COMMITTING";
        case CAL_STATE_COMMITTED: return "COMMITTED";
        default: return "UNKNOWN";
    }
}
