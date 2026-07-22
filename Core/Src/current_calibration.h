#ifndef CURRENT_CALIBRATION_H
#define CURRENT_CALIBRATION_H

#include "current_cal_curve.h"
#include "sys_pwm.h"
#include "sys_Vo_Io.h"

#define CURRENT_CAL_SESSION_ID_MAX 32U
#define CURRENT_CAL_RECEIVED_ALL    0x001fffffUL

typedef enum
{
    CAL_OK = 0,
    CAL_INVALID_ACTION = 1,
    CAL_INVALID_PARAM = 2,
    CAL_BUSY = 3,
    CAL_INVALID_SESSION = 4,
    CAL_DUPLICATE_SEQ = 5,
    CAL_INVALID_STATE = 6,
    CAL_PROFILE_MISMATCH = 7,
    CAL_PWM_OUT_OF_RANGE = 8,
    CAL_PROTECT_ACTIVE = 9,
    CAL_CURVE_INCOMPLETE = 10,
    CAL_CURVE_NOT_MONOTONIC = 11,
    CAL_CURVE_CRC_ERROR = 12,
    CAL_FLASH_WRITE_ERROR = 13,
    CAL_FLASH_VERIFY_ERROR = 14,
    CAL_TIMEOUT = 15,
    CAL_INTERNAL_ERROR = 16,
    CAL_STALE_SEQ = 17,
    CAL_SEQ_CONFLICT = 18,
    CAL_CHUNK_CONFLICT = 19,
    CAL_OTA_ACTIVE = 20,
    CAL_OUTPUT_NOT_STABLE = 21
} current_cal_result_en;

typedef enum
{
    CAL_STATE_IDLE = 0,
    CAL_STATE_READY,
    CAL_STATE_DIRECT_TEST,
    CAL_STATE_CURVE_RECEIVING,
    CAL_STATE_CURVE_PENDING,
    CAL_STATE_TEMP_APPLIED,
    CAL_STATE_COMMITTING,
    CAL_STATE_COMMITTED
} current_cal_state_en;

typedef enum
{
    CAL_ACTION_ENTER = 1,
    CAL_ACTION_SET_PWM,
    CAL_ACTION_READ_STATUS,
    CAL_ACTION_WRITE_CURVE_CHUNK,
    CAL_ACTION_READ_CURVE_STATUS,
    CAL_ACTION_APPLY_TEMPORARY,
    CAL_ACTION_SET_TEST_PERCENT,
    CAL_ACTION_COMMIT,
    CAL_ACTION_ABORT,
    CAL_ACTION_EXIT
} current_cal_action_en;

typedef struct
{
    current_cal_state_en state;
    char session_id[CURRENT_CAL_SESSION_ID_MAX + 1U];
    u32 context_crc;
    u32 legacy_profile_crc;
    u32 profile_crc;
    u32 curve_crc;
    u32 calibration_max_current_ma;
    u32 storage_sequence;
    u32 received_bitmap;
    u32 missing_bitmap;
    u32 timeout_remaining_ms;
    u16 curve_version;
    u8 received_count;
    u8 point_count;
    u8 point_index;
    u8 target_percent;
    current_cal_result_en last_error;
    boolean_en pending_valid;
    boolean_en active_curve_valid;
    boolean_en measurement_valid;
    sys_pwm_status_t pwm;
    sys_vo_io_snapshot_t measurement;
} current_cal_status_t;

void current_calibration_init(void);
void current_calibration_process(void);
boolean_en current_calibration_is_active(void);
current_cal_state_en current_calibration_state(void);

current_cal_result_en current_calibration_enter(const char *session_id,
                                                u32 seq,
                                                u32 digest,
                                                u32 profile_crc,
                                                u16 timeout_sec,
                                                boolean_en ota_busy);
current_cal_result_en current_calibration_prepare_command(const char *session_id,
                                                          u32 seq,
                                                          current_cal_action_en action,
                                                          u32 digest,
                                                          boolean_en *duplicate);
void current_calibration_complete_command(u32 seq,
                                          current_cal_action_en action,
                                          u32 digest,
                                          current_cal_result_en result);
current_cal_result_en current_calibration_cached_result(void);

current_cal_result_en current_calibration_set_pwm(u8 point_index,
                                                  u8 target_percent,
                                                  u16 logical_pwm);
current_cal_result_en current_calibration_write_curve_chunk(u16 curve_version,
                                                            u32 context_crc,
                                                            u32 curve_crc,
                                                            u32 calibration_max_current_ma,
                                                            u8 start_index,
                                                            const u16 *values,
                                                            u8 value_count);
current_cal_result_en current_calibration_apply_temporary(u32 curve_crc);
current_cal_result_en current_calibration_set_test_percent(u8 percent);
current_cal_result_en current_calibration_commit(u32 profile_crc, u32 curve_crc);
current_cal_result_en current_calibration_abort(void);
current_cal_result_en current_calibration_exit(void);
void current_calibration_get_status(current_cal_status_t *status);
const current_cal_curve_t *current_calibration_pending_curve(void);
const char *current_calibration_state_name(current_cal_state_en state);

#endif
