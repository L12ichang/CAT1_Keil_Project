#ifndef METER_RUNTIME_H
#define METER_RUNTIME_H

#include "common.h"
#include "meter_calibration.h"

#define METER_RUNTIME_BL0942_FRAME_SIZE       23U
#define METER_RUNTIME_BL0942_READ_COMMAND      0x58U
#define METER_RUNTIME_BL0942_RESPONSE_HEADER   0x55U
#define METER_RUNTIME_SNAPSHOT_MAX_AGE_MS      2000UL

typedef enum
{
    METER_RUNTIME_MODE_INVALID = 0,
    METER_RUNTIME_MODE_FALLBACK = 1,
    METER_RUNTIME_MODE_CALIBRATED = 2
} meter_runtime_mode_en;

typedef enum
{
    METER_RUNTIME_FRAME_OK = 0,
    METER_RUNTIME_FRAME_NULL,
    METER_RUNTIME_FRAME_BAD_LENGTH,
    METER_RUNTIME_FRAME_BAD_HEADER,
    METER_RUNTIME_FRAME_BAD_RESERVED,
    METER_RUNTIME_FRAME_BAD_CHECKSUM
} meter_runtime_frame_result_en;

/* One validated BL0942 full-read response.  WATT is sign extended exactly
 * once; raw_watt24 is retained for the fixed-point calibration core. */
typedef struct
{
    u32 current_rms_raw;
    u32 voltage_rms_raw;
    u32 current_fast_raw;
    u32 raw_watt24;
    s32 signed_watt_raw;
    u32 cf_counter24;
    u32 frequency_period_raw;
    u8 status;
    u32 sequence;
    u32 sample_tick;
} meter_runtime_bl0942_frame_t;

typedef struct
{
    u32 voltage_01v;
    u32 current_ma;
    u32 active_power_001w;
    u32 frequency_001hz;
    u32 pf_percent;
    u32 session_energy_001wh;
    u32 total_energy_001wh;
} meter_runtime_legacy_input_t;

typedef struct
{
    u32 voltage_adc_raw;
    u32 current_adc_raw;
    u32 voltage_01v;
    u32 current_ma;
    u32 power_01w;
    u16 protect_code;
    u32 sequence;
    u32 sample_tick;
} meter_runtime_output_sample_t;

typedef struct
{
    meter_runtime_mode_en mode;
    meter_cal_result_en coefficient_result;
    boolean_en input_valid;
    boolean_en output_valid;
    boolean_en energy_valid;
    u32 input_sequence;
    u32 output_sequence;
    u32 input_sample_tick;
    u32 output_sample_tick;
    u32 input_sample_age_ms;
    u32 output_sample_age_ms;
    u32 input_voltage_mv;
    u32 input_current_ua;
    u32 input_active_power_mw;
    u32 input_frequency_millihz;
    u32 input_pf_ppm;
    u32 output_voltage_mv;
    u32 output_current_ua;
    u32 output_power_mw;
    u64 session_energy_uwh;
    u64 total_energy_uwh;
    u16 meter_status;
    u16 protect_code;
    u32 continuity_epoch;
} meter_runtime_snapshot_t;

/* Raw factory-calibration view.  Input fields are copied from one validated
 * BL0942 frame and output fields from one ADC publication; neither group may
 * be assembled from independent globals. */
typedef struct
{
    meter_runtime_mode_en mode;
    meter_cal_result_en coefficient_result;
    u32 context_crc;
    u8 storage_status;
    boolean_en input_valid;
    boolean_en output_valid;
    u32 input_voltage_raw;
    u32 input_current_raw;
    u32 input_fast_current_raw;
    u32 input_watt_raw24;
    s32 input_watt_signed;
    u32 input_period_raw;
    u32 input_cf_raw24;
    u8 input_status;
    u32 input_sequence;
    u32 input_tick;
    u32 input_age_ms;
    u32 output_voltage_raw;
    u32 output_current_raw;
    u16 output_protect_code;
    u32 output_sequence;
    u32 output_tick;
    u32 output_age_ms;
} meter_runtime_calibration_snapshot_t;

meter_runtime_frame_result_en meter_runtime_parse_bl0942_frame(
    const u8 *bytes,
    u32 length,
    meter_runtime_bl0942_frame_t *frame);

/* current_cal_storage_init() must have completed before this call. */
void meter_runtime_init(void);
void meter_runtime_process(void);
meter_runtime_mode_en meter_runtime_mode(void);
void meter_runtime_publish_bl0942(
    const meter_runtime_bl0942_frame_t *frame,
    const meter_runtime_legacy_input_t *legacy);
void meter_runtime_publish_output(
    const meter_runtime_output_sample_t *sample);
void meter_runtime_mark_bl_discontinuous(void);
boolean_en meter_runtime_get_snapshot(meter_runtime_snapshot_t *snapshot);
boolean_en meter_runtime_get_calibration_snapshot(
    meter_runtime_calibration_snapshot_t *snapshot);
boolean_en meter_runtime_power_down_save(void);
boolean_en meter_runtime_energy_clear(void);
/* Called while the existing calibration lock owns a forced-off output. */
boolean_en meter_runtime_prepare_calibration_reload(void);
boolean_en meter_runtime_reload_calibration(void);

#endif
