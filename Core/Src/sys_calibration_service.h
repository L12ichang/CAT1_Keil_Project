#ifndef SYS_CALIBRATION_SERVICE_H
#define SYS_CALIBRATION_SERVICE_H

#include "type.h"
#include "sys_product_profile.h"

#define SYS_CALIBRATION_DRIVER_PROTOCOL_FROZEN 1U
#define SYS_CALIBRATION_MQTT_V2_FIELDS_FROZEN  1U
#define SYS_CALIBRATION_CODEC_AVAILABLE        1U
#define SYS_CALIBRATION_FLASH_COMMIT_ENABLED   1U
#define SYS_CALIBRATION_NONZERO_OUTPUT_ENABLED 1U

#define SYS_CALIBRATION_LEASE_MIN_MS 1000UL
#define SYS_CALIBRATION_LEASE_MAX_MS 600000UL

typedef enum
{
    SYS_CALIBRATION_STATE_DISABLED = 0,
    SYS_CALIBRATION_STATE_IDLE,
    SYS_CALIBRATION_STATE_ACTIVE,
    SYS_CALIBRATION_STATE_STAGED,
    SYS_CALIBRATION_STATE_APPLIED,
    SYS_CALIBRATION_STATE_FAULT,
    SYS_CALIBRATION_STATE_ABORTED
} sys_calibration_state_en;

typedef enum
{
    SYS_CALIBRATION_RESULT_OK = 0,
    SYS_CALIBRATION_RESULT_NOT_AVAILABLE,
    SYS_CALIBRATION_RESULT_INVALID_STATE,
    SYS_CALIBRATION_RESULT_INVALID_ARGUMENT,
    SYS_CALIBRATION_RESULT_LEASE_EXPIRED,
    SYS_CALIBRATION_RESULT_BUSY,
    SYS_CALIBRATION_RESULT_PROTOCOL_ERROR,
    SYS_CALIBRATION_RESULT_SAFETY_NOT_READY,
    SYS_CALIBRATION_RESULT_DUPLICATE,
    SYS_CALIBRATION_RESULT_FLASH_GATED,
    SYS_CALIBRATION_RESULT_HARDWARE_FAULT,
    SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH
} sys_calibration_result_en;

typedef void (*sys_calibration_safe_off_fn)(void);
typedef boolean_en (*sys_calibration_set_level_fn)(u16 level);
typedef boolean_en (*sys_calibration_set_inhibit_fn)(boolean_en active);
typedef boolean_en (*sys_calibration_commit_fn)(
    const sys_calibration_context_st *context,
    const u8 *payload,
    u16 length,
    u32 *generation);
typedef u16 (*sys_calibration_bound_voltage_fn)(void);

typedef struct
{
    sys_calibration_state_en state;
    sys_calibration_result_en last_result;
    u32 session_id;
    u32 lease_deadline_ms;
    u32 result_seq;
    u32 last_request_seq;
    u32 staged_crc32;
    u32 committed_crc32;
    u32 committed_generation;
    u16 current_level;
    u16 staged_length;
    boolean_en codec_available;
    boolean_en commit_available;
    boolean_en nonzero_output_allowed;
    boolean_en safety_ready;
    boolean_en boot_inhibit_active;
    boolean_en persistence_ready;
    boolean_en staged_valid;
    boolean_en committed_valid;
    boolean_en context_valid;
    sys_calibration_context_st context;
} sys_calibration_service_status_st;

typedef enum
{
    SYS_CALIBRATION_RAW_QUERY = 0,
    SYS_CALIBRATION_RAW_SET = 1
} sys_calibration_raw_direction_en;

extern void sys_calibration_service_init(void);
extern void sys_calibration_service_bind_safe_off(sys_calibration_safe_off_fn safe_off);
extern void sys_calibration_service_bind_platform(
    sys_calibration_set_level_fn set_level,
    sys_calibration_set_inhibit_fn set_inhibit,
    sys_calibration_commit_fn commit);
extern void sys_calibration_service_bind_bound_voltage(
    sys_calibration_bound_voltage_fn get_bound_voltage);
extern void sys_calibration_service_restore_boot(boolean_en inhibited,
                                                 boolean_en persistence_ready);
extern boolean_en sys_calibration_service_load_committed(
    const sys_calibration_context_st *context,
    const u8 *payload,
    u16 length,
    u32 generation);
extern void sys_calibration_service_set_safety_ready(boolean_en ready);
extern boolean_en sys_calibration_service_get_status(
    sys_calibration_service_status_st *status);
extern boolean_en sys_calibration_service_get_staged_payload(
    u8 *payload,
    u16 capacity,
    u16 *length);
extern boolean_en sys_calibration_service_get_context(
    sys_calibration_context_st *context);
extern boolean_en sys_calibration_service_get_committed_context(
    sys_calibration_context_st *context);
extern boolean_en sys_calibration_service_is_boot_inhibited(void);
extern boolean_en sys_calibration_service_is_output_authorized(void);
extern boolean_en sys_calibration_service_runtime_context_matches_voltage(
    u16 bound_voltage_01v);
extern boolean_en sys_calibration_service_get_calibrated_max_current_ma(
    u16 bound_voltage_01v,
    u16 *calibrated_max_current_ma);
extern boolean_en sys_calibration_service_get_calibrated_target_current_ma(
    u16 bound_voltage_01v,
    u16 *calibrated_target_current_ma);
extern boolean_en sys_calibration_service_apply_fullscale_gain_pwm(
    u16 nominal_pwm,
    u16 pwm_limit,
    u16 *corrected_pwm);
extern boolean_en sys_calibration_service_correct_output_percent(
    u8 requested_percent,
    u16 rated_current_ma,
    u8 *corrected_percent);
extern boolean_en sys_calibration_service_correct_output_current(
    u16 device_current_ma,
    u16 *corrected_current_ma);
extern void sys_calibration_service_force_fault(void);

extern sys_calibration_result_en sys_calibration_service_begin_seq(
    u32 session_id,
    u32 now_ms,
    u32 lease_ms,
    u32 seq,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_begin_context_seq(
    u32 session_id,
    u32 now_ms,
    u32 lease_ms,
    u32 seq,
    const sys_calibration_context_st *context,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_heartbeat_seq(
    u32 session_id,
    u32 now_ms,
    u32 lease_ms,
    u32 seq,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_set_point_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    u16 level,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_set_validation_percent_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    u8 target_percent,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_raw_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    const u8 *frame,
    u16 frame_length,
    sys_calibration_raw_direction_en direction,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_snapshot_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_stage_config_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    const u8 *payload,
    u16 length,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_stage_config_context_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    const sys_calibration_context_st *context,
    const u8 *payload,
    u16 length,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_apply_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_readback_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_commit_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_commit_context_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    const sys_calibration_context_st *context,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_abort_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_release_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status);

/* 兼容原有调用点；新MQTT路径使用带seq接口。 */
extern sys_calibration_result_en sys_calibration_service_begin(
    u32 session_id,
    u32 now_ms,
    u32 lease_ms,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_set_point(
    u32 session_id,
    u32 now_ms,
    u16 level,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_stage_config(
    u32 session_id,
    const u8 *payload,
    u32 length,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_commit(
    u32 session_id,
    sys_calibration_service_status_st *status);
extern boolean_en sys_calibration_service_abort(
    u32 session_id,
    sys_calibration_service_status_st *status);
extern boolean_en sys_calibration_service_timer(
    u32 now_ms,
    sys_calibration_service_status_st *status);

#endif
