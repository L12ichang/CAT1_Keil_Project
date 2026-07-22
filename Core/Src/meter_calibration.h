#ifndef METER_CALIBRATION_H
#define METER_CALIBRATION_H

#if !defined(METER_CALIBRATION_HOST_TEST)
#include "common.h"
#endif

#define METER_CAL_COEFFICIENT_VERSION       2U
#define METER_CAL_CHANNEL_COUNT             6U
#define METER_CAL_Q24_SHIFT                 24U
#define METER_CAL_Q24_ONE                   0x01000000UL
#define METER_CAL_Q24_HALF                  0x00800000UL
#define METER_CAL_CF_COUNTER_MASK           0x00ffffffUL
#define METER_CAL_PF_PPM_MAX                1000000UL
#define METER_CAL_COEFFICIENT_SERIALIZED_SIZE 96U
#define METER_CAL_ENERGY_CHECKPOINT_VERSION 1U
#define METER_CAL_ENERGY_CHECKPOINT_DOMAIN  0x31464345UL /* "ECF1" */
#define METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE 44U

#define METER_CAL_SIGNED_24_MIN             (-8388608L)
#define METER_CAL_SIGNED_24_MAX             8388607L

#define METER_CAL_FLAG_ENERGY_ENABLED       0x00000001UL
#define METER_CAL_FLAG_CF_COUNTER_24BIT     0x00000002UL
#define METER_CAL_FLAGS_ENERGY_CF24         (METER_CAL_FLAG_ENERGY_ENABLED | \
                                             METER_CAL_FLAG_CF_COUNTER_24BIT)
#define METER_CAL_FLAGS_ALLOWED             METER_CAL_FLAGS_ENERGY_CF24

/*
 * Internal engineering units are deliberately finer than the MQTT units:
 * voltage mV, current uA, active/output power mW and frequency mHz.
 */
typedef enum
{
    METER_CAL_CHANNEL_INPUT_VOLTAGE_MV = 0,
    METER_CAL_CHANNEL_INPUT_CURRENT_UA,
    METER_CAL_CHANNEL_INPUT_ACTIVE_POWER_MW,
    METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ,
    METER_CAL_CHANNEL_OUTPUT_VOLTAGE_MV,
    METER_CAL_CHANNEL_OUTPUT_CURRENT_UA
} meter_cal_channel_en;

typedef enum
{
    METER_CAL_OK = 0,
    METER_CAL_NULL,
    METER_CAL_BAD_VERSION,
    METER_CAL_BAD_CHANNEL_COUNT,
    METER_CAL_CONTEXT_MISMATCH,
    METER_CAL_BAD_CHANNEL,
    METER_CAL_RAW_OUT_OF_RANGE,
    METER_CAL_ZERO_OUT_OF_RANGE,
    METER_CAL_RAW_SPAN_TOO_SMALL,
    METER_CAL_FACTOR_ZERO,
    METER_CAL_FACTOR_OUT_OF_RANGE,
    METER_CAL_ENGINEERING_OUT_OF_RANGE,
    METER_CAL_ENERGY_CONFIGURATION_INVALID,
    METER_CAL_FLAGS_INVALID,
    METER_CAL_DATA_CRC_MISMATCH,
    METER_CAL_BUFFER_TOO_SMALL,
    METER_CAL_SERIALIZED_SIZE_INVALID,
    METER_CAL_ENERGY_DISABLED,
    METER_CAL_COUNTER_DISCONTINUITY,
    METER_CAL_CHECKPOINT_INVALID,
    METER_CAL_CHECKPOINT_BAD_VERSION,
    METER_CAL_CHECKPOINT_SIZE_INVALID,
    METER_CAL_CHECKPOINT_DOMAIN_MISMATCH,
    METER_CAL_CHECKPOINT_CONTEXT_MISMATCH,
    METER_CAL_CHECKPOINT_COEFFICIENT_MISMATCH,
    METER_CAL_CHECKPOINT_EPOCH_MISMATCH,
    METER_CAL_CHECKPOINT_CRC_MISMATCH,
    METER_CAL_CONTINUITY_INVALID,
    METER_CAL_CONVERSION_SATURATED,
    METER_CAL_ACCUMULATOR_OVERFLOW
} meter_cal_result_en;

/*
 * Persistent coefficient payload, version 2.
 *
 * zero_raw[] is a signed raw-domain zero.  Only WATT accepts a negative
 * zero; all other linear channels require a non-negative zero and the
 * frequency zero is reserved and must be zero.
 *
 * factor_q24[] is engineering-units-per-raw in Q24 for every linear
 * channel.  For the frequency channel it has a different, fixed meaning:
 *   frequency_mHz = round(reciprocal_numerator_q24 /
 *                         (period_raw * 2^24)).
 * A nominal BL0942 numerator is therefore 1,000,000,000 * 2^24.
 *
 * The CRC covers an explicit 92-byte little-endian representation; it is
 * never calculated from C object bytes.  The complete serialized payload is
 * 96 bytes and the in-memory object is asserted to fit the 128-byte slot.
 */
typedef struct
{
    u16 version;
    u16 channel_count;
    u32 context_crc;
    s32 zero_raw[METER_CAL_CHANNEL_COUNT];
    u64 factor_q24[METER_CAL_CHANNEL_COUNT];
    u64 energy_gain_q24;       /* CF count delta to uWh. */
    u32 flags;
    u32 data_crc;
} meter_cal_coefficients_t;

/* Runtime accumulator.  A single writer owns it between critical sections. */
typedef struct
{
    u32 previous_cf24;
    u32 remainder_q24;
    u64 total_energy_uwh;
    u32 continuity_epoch;
    boolean_en initialized;
} meter_cal_energy_accumulator_t;

/*
 * Persistent energy checkpoint v1.  Its CRC covers an explicit 40-byte
 * little-endian prefix; complete serialized size is exactly 44 bytes.
 * Never persist or transmit C object bytes.
 */
typedef struct
{
    u16 version;
    u16 serialized_size;
    u32 domain;
    u32 context_crc;
    u32 coefficient_data_crc;
    u32 continuity_epoch;
    u32 previous_cf24;
    u32 remainder_q24;
    u64 total_energy_uwh;
    u32 initialized;
    u32 data_crc;
} meter_cal_energy_checkpoint_t;

u32 meter_calibration_raw_max(meter_cal_channel_en channel);
u32 meter_calibration_engineering_max(meter_cal_channel_en channel);
u32 meter_calibration_coefficients_crc(const meter_cal_coefficients_t *coefficients);
meter_cal_result_en meter_calibration_coefficients_validate(
    const meter_cal_coefficients_t *coefficients,
    u32 expected_context_crc);
meter_cal_result_en meter_calibration_coefficients_encode(
    const meter_cal_coefficients_t *coefficients,
    u32 expected_context_crc,
    u8 *serialized,
    u32 capacity);
meter_cal_result_en meter_calibration_coefficients_decode(
    const u8 *serialized,
    u32 serialized_size,
    u32 expected_context_crc,
    meter_cal_coefficients_t *coefficients);

s32 meter_calibration_sign_extend_s24(u32 raw24);
meter_cal_result_en meter_calibration_convert(
    const meter_cal_coefficients_t *coefficients,
    u32 expected_context_crc,
    meter_cal_channel_en channel,
    u32 raw,
    u32 *value);

/* mV * uA, returned as mW with half-up rounding and u32 saturation. */
u32 meter_calibration_output_power_mw(u32 output_voltage_mv,
                                      u32 output_current_ua);

/*
 * PF must be derived from one sequence-stamped input snapshot: voltage,
 * current and power from different BL0942 frames must never be mixed.  This
 * pure function assumes the snapshot layer has already enforced that rule.
 * A zero denominator returns zero; a result above unity clamps to 1,000,000.
 */
u32 meter_calibration_input_pf_ppm(u32 input_active_power_mw,
                                   u32 input_voltage_mv,
                                   u32 input_current_ua);

u32 meter_calibration_cf24_delta(u32 previous_cf24, u32 current_cf24);
void meter_calibration_energy_accumulator_init(
    meter_cal_energy_accumulator_t *accumulator,
    u64 initial_energy_uwh,
    u32 continuity_epoch);
/* Caller must capture while the accumulator writer is excluded. */
meter_cal_result_en meter_calibration_energy_checkpoint_capture(
    const meter_cal_coefficients_t *coefficients,
    u32 expected_context_crc,
    const meter_cal_energy_accumulator_t *accumulator,
    meter_cal_energy_checkpoint_t *checkpoint);
u32 meter_calibration_energy_checkpoint_crc(
    const meter_cal_energy_checkpoint_t *checkpoint);
meter_cal_result_en meter_calibration_energy_checkpoint_validate(
    const meter_cal_energy_checkpoint_t *checkpoint,
    u32 expected_context_crc,
    u32 expected_coefficient_data_crc,
    u32 expected_continuity_epoch);
meter_cal_result_en meter_calibration_energy_checkpoint_encode(
    const meter_cal_energy_checkpoint_t *checkpoint,
    u32 expected_context_crc,
    u32 expected_coefficient_data_crc,
    u32 expected_continuity_epoch,
    u8 *serialized,
    u32 capacity);
meter_cal_result_en meter_calibration_energy_checkpoint_decode(
    const u8 *serialized,
    u32 serialized_size,
    u32 expected_context_crc,
    u32 expected_coefficient_data_crc,
    u32 expected_continuity_epoch,
    meter_cal_energy_checkpoint_t *checkpoint);
meter_cal_result_en meter_calibration_energy_accumulator_restore(
    meter_cal_energy_accumulator_t *accumulator,
    const meter_cal_energy_checkpoint_t *checkpoint,
    u32 expected_context_crc,
    u32 expected_coefficient_data_crc,
    u32 expected_continuity_epoch);
meter_cal_result_en meter_calibration_energy_checkpoint_rounded_uwh(
    const meter_cal_energy_checkpoint_t *checkpoint,
    u32 expected_context_crc,
    u32 expected_coefficient_data_crc,
    u32 expected_continuity_epoch,
    u64 *rounded_energy_uwh);
/*
 * maximum_trusted_cf_delta is supplied by the caller from elapsed time and
 * the maximum credible power.  A larger modular delta is treated as a meter
 * reset/discontinuity: previous_cf24 becomes the new baseline, while total
 * and remainder are unchanged.  A total overflow saturates total and delta
 * reports only the uWh accepted before saturation.  On that overflow, the
 * trusted CF baseline and Q24 remainder still advance for the entire sampled
 * delta, so the saturated accumulator never re-counts it.
 *
 * delta_energy_uwh is a compatibility u32 report.  If the fully committed
 * u64 increment exceeds 0xffffffff, it is saturated to 0xffffffff and the
 * function returns METER_CAL_CONVERSION_SATURATED; total, baseline and
 * remainder have nevertheless been fully committed.  If total itself
 * overflows, METER_CAL_ACCUMULATOR_OVERFLOW takes precedence and the report
 * is saturated from the uWh accepted before total reached its u64 limit.
 * Validation and conversion failures leave accumulator state unchanged,
 * except for the explicit discontinuity re-baselining described above.
 *
 * counter_continuous is mandatory evidence from the snapshot/driver layer.
 * BOOL_FALSE always re-baselines, even when a reset near 0xffffff resembles
 * a natural wrap.  BOOL_TRUE is safe only when the caller also proves from
 * elapsed time and maximum physical power that the hidden delta is less than
 * one complete 24-bit counter revolution.
 */
meter_cal_result_en meter_calibration_energy_accumulate_cf24(
    const meter_cal_coefficients_t *coefficients,
    u32 expected_context_crc,
    u32 current_cf24,
    u32 maximum_trusted_cf_delta,
    u32 continuity_epoch,
    boolean_en counter_continuous,
    meter_cal_energy_accumulator_t *accumulator,
    u32 *delta_energy_uwh);
/* Restart helper: a continuous same-epoch counter may recover trusted delta. */
meter_cal_result_en meter_calibration_energy_resume_cf24(
    const meter_cal_coefficients_t *coefficients,
    u32 expected_context_crc,
    u32 current_cf24,
    u32 maximum_trusted_cf_delta,
    u32 continuity_epoch,
    boolean_en counter_continuous,
    meter_cal_energy_accumulator_t *accumulator,
    u32 *delta_energy_uwh);

#endif
