#ifndef SYS_CALIBRATION_SERVICE_H
#define SYS_CALIBRATION_SERVICE_H

#include "sys_calibration_driver_protocol.h"

#define SYS_CALIBRATION_PROTOCOL_VERSION             3U
#define SYS_CALIBRATION_POINT_COUNT \
    SYS_CALIBRATION_PAYLOAD_POINT_COUNT
#define SYS_CALIBRATION_LEVEL_STEP \
    SYS_CALIBRATION_PAYLOAD_LEVEL_STEP
#define SYS_CALIBRATION_LEVEL_MAX                   200U
#define SYS_CALIBRATION_PWM_MAX                    1000U
#define SYS_CALIBRATION_REQUIRED_SECTIONS \
    SYS_CALIBRATION_PAYLOAD_VALID_FLAGS
#define SYS_CALIBRATION_LEASE_MIN_MS              1000UL
#define SYS_CALIBRATION_LEASE_MAX_MS            600000UL

#define SYS_CALIBRATION_RAW_PWM_VALID             0x0001U
#define SYS_CALIBRATION_RAW_OCO_VALID             0x0002U
#define SYS_CALIBRATION_RAW_BL_V_VALID            0x0004U
#define SYS_CALIBRATION_RAW_BL_I_VALID            0x0008U
#define SYS_CALIBRATION_RAW_BL_P_VALID            0x0010U
#define SYS_CALIBRATION_RAW_BL_FRESH              0x0020U
#define SYS_CALIBRATION_RAW_OUTPUT_CORRECTED       0x0040U
#define SYS_CALIBRATION_RAW_BL_V_CORRECTED         0x0080U
#define SYS_CALIBRATION_RAW_BL_I_CORRECTED         0x0100U
#define SYS_CALIBRATION_RAW_BL_P_CORRECTED         0x0200U
#define SYS_CALIBRATION_RAW_VO_VALID                0x0400U

#define SYS_CALIBRATION_FAULT_OUTPUT_OVERLOAD       0x0001U
#define SYS_CALIBRATION_FAULT_OUTPUT_LOW_VOLTAGE    0x0002U
#define SYS_CALIBRATION_FAULT_INPUT_OVERVOLTAGE     0x0004U
#define SYS_CALIBRATION_FAULT_INPUT_UNDERVOLTAGE    0x0008U
#define SYS_CALIBRATION_FAULT_OVER_TEMPERATURE      0x0010U

typedef enum
{
    SYS_CALIBRATION_RESULT_OK = 0,
    SYS_CALIBRATION_RESULT_BAD_REQUEST = 1,
    SYS_CALIBRATION_RESULT_BAD_STATE = 2,
    SYS_CALIBRATION_RESULT_BUSY = 3,
    SYS_CALIBRATION_RESULT_SESSION_EXPIRED = 4,
    SYS_CALIBRATION_RESULT_RANGE_ERROR = 5,
    SYS_CALIBRATION_RESULT_DATA_STALE = 6,
    SYS_CALIBRATION_RESULT_CRC_ERROR = 7,
    SYS_CALIBRATION_RESULT_FLASH_ERROR = 8,
    SYS_CALIBRATION_RESULT_HARDWARE_FAULT = 9,
    SYS_CALIBRATION_RESULT_PROFILE_MISMATCH = 10
} sys_calibration_result_en;

typedef enum
{
    SYS_CALIBRATION_STATE_IDLE = 0,
    SYS_CALIBRATION_STATE_ACTIVE = 1,
    SYS_CALIBRATION_STATE_STAGED = 2,
    SYS_CALIBRATION_STATE_APPLIED = 3,
    SYS_CALIBRATION_STATE_COMMITTED = 4
} sys_calibration_state_en;

typedef struct
{
    sys_calibration_state_en state;
    sys_calibration_result_en last_result;
    u32 session_id;
    u32 lease_deadline_ms;
    u32 lease_ms;
    u32 result_seq;
    u32 last_request_seq;
    u32 staged_crc32;
    u32 committed_crc32;
    u32 committed_generation;
    u32 output_fallback_count;
    u16 current_level;
    u16 actual_pwm;
    u16 staged_length;
    u16 committed_length;
    u16 calibration_voltage_01v;
    u16 calibration_span_ma;
    u16 fault_flags;
    u8 current_percent;
    boolean_en safety_ready;
    boolean_en persistence_ready;
    boolean_en boot_inhibit_active;
    boolean_en staged_valid;
    boolean_en committed_valid;
} sys_calibration_service_status_st;

typedef struct
{
    u16 level;
    u16 actual_pwm;
    u16 oco_raw;
    u32 bl_voltage_raw;
    u32 bl_current_raw;
    s32 bl_power_raw;
    u16 corrected_output_current_ma;
    u16 corrected_input_voltage_01v;
    u16 corrected_input_current_ma;
    u16 corrected_input_power_01w;
    u16 output_voltage_01v;
    u32 bl_age_ms;
    u16 valid_flags;
    u16 fault_flags;
} sys_calibration_raw_st;

typedef void (*sys_calibration_safe_off_fn)(void);
typedef boolean_en (*sys_calibration_set_level_fn)(u16 level,
                                                    u16 *actual_pwm);
typedef boolean_en (*sys_calibration_set_output_fn)(u8 percent,
                                                     u16 *actual_pwm);
typedef boolean_en (*sys_calibration_set_inhibit_fn)(boolean_en active);
typedef boolean_en (*sys_calibration_commit_v3_fn)(
    const u8 payload[SYS_CALIBRATION_PAYLOAD_LENGTH],
    u16 length,
    u32 payload_crc32,
    u32 *generation);

extern void sys_calibration_service_init(void);
extern void sys_calibration_service_bind_output(
    sys_calibration_safe_off_fn safe_off,
    sys_calibration_set_level_fn set_level,
    sys_calibration_set_output_fn set_output);
/* Production defaults call WP2 V3 storage. Tests may inject equivalent fakes. */
extern void sys_calibration_service_bind_storage(
    sys_calibration_set_inhibit_fn set_inhibit,
    sys_calibration_commit_v3_fn commit_v3);
extern void sys_calibration_service_restore_boot(boolean_en inhibited,
                                                 boolean_en persistence_ready);
extern boolean_en sys_calibration_service_load_committed_v3(
    const u8 payload[SYS_CALIBRATION_PAYLOAD_LENGTH],
    u16 length,
    u32 payload_crc32,
    u32 generation);
extern void sys_calibration_service_set_safety_ready(boolean_en ready);
extern boolean_en sys_calibration_service_get_status(
    sys_calibration_service_status_st *status);
extern boolean_en sys_calibration_service_is_boot_inhibited(void);
extern boolean_en sys_calibration_service_is_output_authorized(void);
extern u16 sys_calibration_service_calibration_span_ma(void);

/* Compatibility wrapper: legacy callers default to the 36V Product I-V span. */
extern sys_calibration_result_en sys_calibration_service_begin_seq(
    u32 session_id,
    u32 now_ms,
    u32 lease_ms,
    u32 seq,
    u16 profile_id,
    u32 profile_fingerprint,
    sys_calibration_service_status_st *status);
/* V3 real-calibration BEGIN: voltage/span are session context, never Factory
 * SET/HWMAX rewrites. The span must equal the safe Product I-V/HWMAX limit. */
extern sys_calibration_result_en sys_calibration_service_begin_range_seq(
    u32 session_id,
    u32 now_ms,
    u32 lease_ms,
    u32 seq,
    u16 profile_id,
    u32 profile_fingerprint,
    u16 calibration_voltage_01v,
    u16 calibration_span_ma,
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
/* Same SET_POINT operation, optional logicalPwm field. Used by the station to
 * search the PWM that makes the external reference hit the requested target. */
extern sys_calibration_result_en sys_calibration_service_set_point_direct_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    u16 level,
    u16 logical_pwm,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_raw_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_raw_st *raw,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_stage_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    const u8 payload[SYS_CALIBRATION_PAYLOAD_LENGTH],
    u16 length,
    u32 payload_crc32,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_apply_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_set_output_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    u8 percent,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_commit_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    sys_calibration_service_status_st *status);
extern sys_calibration_result_en sys_calibration_service_read_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    u8 *payload,
    u16 capacity,
    u16 *length,
    u32 *payload_crc32,
    u32 *generation,
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
extern boolean_en sys_calibration_service_timer(
    u32 now_ms,
    sys_calibration_service_status_st *status);

/* Runtime Correction APIs. False means use the mature Default path. */
extern boolean_en sys_calibration_service_output_pwm_for_current(
    u16 target_current_ma,
    u16 *logical_pwm);
extern boolean_en sys_calibration_service_correct_output_current_raw(
    u16 oco_raw,
    u16 *corrected_current_ma);
extern boolean_en sys_calibration_service_correct_bl_voltage(
    u32 bl_voltage_raw,
    u16 *corrected_voltage_01v);
extern boolean_en sys_calibration_service_correct_bl_current(
    u32 bl_current_raw,
    u16 *corrected_current_ma);
extern boolean_en sys_calibration_service_correct_bl_power(
    s32 bl_power_raw,
    u16 *corrected_power_01w);
extern void sys_calibration_service_force_fault(void);

#endif
