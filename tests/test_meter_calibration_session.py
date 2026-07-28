from __future__ import annotations

import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
CL = Path(
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.39.33519\bin\Hostx64\x64\cl.exe"
)
MSVC_ROOT = Path(
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.39.33519"
)
SCOPE_VC = Path(
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\SDK\ScopeCppSDK\vc15\VC"
)
WINSDK = Path(r"C:\Program Files (x86)\Windows Kits\10")
WINSDK_VERSION = "10.0.22621.0"


PREINCLUDE = r"""
#ifndef METER_SESSION_HOST_PREINCLUDE_H
#define METER_SESSION_HOST_PREINCLUDE_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMON_H
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;
typedef enum { BOOL_FALSE = 0, BOOL_TRUE = 1 } boolean_en;

#define METER_CALIBRATION_HOST_TEST
#include "meter_calibration.h"

#define CURRENT_CAL_POINT_COUNT 21U
#define CURRENT_CAL_CURVE_VERSION_LEGACY 1U
#define CURRENT_CAL_CURVE_VERSION 2U
#define CURRENT_CAL_PWM_PERIOD_COUNTS 1000U
#define CURRENT_CAL_CURVE_H
typedef enum {
    CURRENT_CAL_CURVE_OK = 0, CURRENT_CAL_CURVE_NULL,
    CURRENT_CAL_CURVE_BAD_VERSION, CURRENT_CAL_CURVE_BAD_COUNT,
    CURRENT_CAL_CURVE_BAD_ZERO, CURRENT_CAL_CURVE_NOT_MONOTONIC,
    CURRENT_CAL_CURVE_OUT_OF_RANGE, CURRENT_CAL_CURVE_PROFILE_MISMATCH,
    CURRENT_CAL_CURVE_CRC_MISMATCH, CURRENT_CAL_CURVE_BAD_CURRENT_RANGE
} current_cal_curve_result_en;
typedef struct {
    u16 curve_version; u16 point_count; u32 calibration_max_current_ma;
    u16 logical_pwm[CURRENT_CAL_POINT_COUNT]; u32 context_crc; u32 curve_crc;
} current_cal_curve_t;
u16 current_cal_pwm_logical_max(void);
u32 current_cal_context_crc(void);
u32 current_cal_legacy_profile_crc(void);
u32 current_cal_profile_crc(void);
current_cal_curve_result_en current_cal_curve_validate(const current_cal_curve_t *, u32);

#define SYS_PWM_H
typedef struct {
    u8 requested_percent; u8 effective_percent;
    u16 requested_logical_pwm; u16 applied_logical_pwm; u16 compare_value;
    u16 protect_code; boolean_en output_enabled; boolean_en calibration_locked;
    boolean_en limited;
} sys_pwm_status_t;
void sys_pwm_calibration_lock(void);
void sys_pwm_calibration_unlock(void);
boolean_en sys_pwm_calibration_set_direct(u16);
boolean_en sys_pwm_calibration_set_percent(const current_cal_curve_t *, u8);
boolean_en sys_pwm_calibration_safety_ready(void);
void sys_pwm_force_off(void);
void sys_pwm_get_status(sys_pwm_status_t *);

#define _SYS_VO_IO_H
typedef struct {
    u32 adc_raw[4]; u32 adc_voltage_raw; u32 adc_current_raw;
    u32 output_voltage_01v; u32 output_current_ma; u32 output_power_01w;
    s16 temperature_01c; u16 protect_code; u32 sequence; u32 sample_tick;
    u32 sample_age_ms;
} sys_vo_io_snapshot_t;
boolean_en sys_vo_io_get_snapshot(sys_vo_io_snapshot_t *);
extern u8 Error_1_OL;
extern u8 Error_3_OV;

#define CURRENT_CAL_STORAGE_H
typedef enum { CURRENT_CAL_SLOT_NONE=0, CURRENT_CAL_SLOT_A=1,
               CURRENT_CAL_SLOT_B=2 } current_cal_slot_en;
typedef enum { CURRENT_CAL_SECTION_EMPTY=0, CURRENT_CAL_SECTION_VALID=1,
               CURRENT_CAL_SECTION_TOMBSTONE=2 } current_cal_section_state_en;
typedef enum { CURRENT_CAL_METER_STATUS_ABSENT=0,
               CURRENT_CAL_METER_STATUS_TOMBSTONE,
               CURRENT_CAL_METER_STATUS_VALID,
               CURRENT_CAL_METER_STATUS_INVALID,
               CURRENT_CAL_METER_STATUS_CONFLICT } current_cal_meter_status_en;
#define CURRENT_CAL_METER_PAYLOAD_SIZE 128U
typedef struct {
    current_cal_section_state_en state; u32 context_crc; u32 data_crc;
    u16 section_version; u16 data_length;
    u8 payload[CURRENT_CAL_METER_PAYLOAD_SIZE];
} current_cal_meter_section_t;
void current_cal_storage_init(void);
boolean_en current_cal_storage_has_active_curve(void);
const current_cal_curve_t *current_cal_storage_active_curve(void);
u32 current_cal_storage_sequence(void);
boolean_en current_cal_storage_commit(const current_cal_curve_t *);
boolean_en current_cal_storage_get_meter_section(current_cal_meter_section_t *);
boolean_en current_cal_storage_commit_meter_section(const current_cal_meter_section_t *);
current_cal_meter_status_en current_cal_storage_meter_status(void);

#define METER_RUNTIME_H
typedef enum { METER_RUNTIME_MODE_INVALID=0, METER_RUNTIME_MODE_FALLBACK=1,
               METER_RUNTIME_MODE_CALIBRATED=2 } meter_runtime_mode_en;
typedef struct {
    meter_runtime_mode_en mode; meter_cal_result_en coefficient_result;
    u32 context_crc; u8 storage_status; boolean_en input_valid;
    boolean_en output_valid; u32 input_voltage_raw; u32 input_current_raw;
    u32 input_fast_current_raw; u32 input_watt_raw24; s32 input_watt_signed;
    u32 input_period_raw; u32 input_cf_raw24; u8 input_status;
    u32 input_sequence; u32 input_tick; u32 input_age_ms;
    u32 output_voltage_raw; u32 output_current_raw; u16 output_protect_code;
    u32 output_sequence; u32 output_tick; u32 output_age_ms;
} meter_runtime_calibration_snapshot_t;
boolean_en meter_runtime_get_calibration_snapshot(meter_runtime_calibration_snapshot_t *);
boolean_en meter_runtime_prepare_calibration_reload(void);
boolean_en meter_runtime_reload_calibration(void);

#define PORTABLE_H_
u32 Timer_GetTickCount(void);
#define SYS_TEMP_OVER_PROTECT_H
extern u8 driver_temperarure_warn;
#define FACTORY_DATA_H
extern u16 SET_OUTCUR_temp;
extern u16 HWMAX_OUTCUR_temp;
#define SET_OUTCUR SET_OUTCUR_temp
#define HWMAX_OUTCUR HWMAX_OUTCUR_temp
#define HW_FLASH_H
void hw_flash_latch_update_fault(void);
#include "current_calibration.h"
#endif
"""


HARNESS = r"""
#define CONTEXT_CRC 0x12345678UL
u8 Error_1_OL, Error_3_OV, driver_temperarure_warn;
u16 SET_OUTCUR_temp=890U, HWMAX_OUTCUR_temp=1680U;
static u32 now_tick;
static boolean_en active_curve_valid, pwm_locked, output_enabled;
static current_cal_curve_t active_curve;
static current_cal_meter_section_t stored_meter;
static boolean_en stored_meter_valid;
static boolean_en corrupt_readback, fail_meter_store, fail_prepare, fail_reload;
static int force_off_count, flash_fault_count, prepare_count, reload_count;
static const current_cal_curve_t *last_percent_curve;
static u8 last_percent;

u32 current_cal_crc32(const u8 *data, u32 length) {
    u32 crc=0xffffffffUL, index; u8 bit;
    for(index=0U; index<length; ++index) {
        crc ^= data[index];
        for(bit=0U; bit<8U; ++bit)
            crc=(crc>>1)^((crc&1U)?0xedb88320UL:0U);
    }
    return ~crc;
}
u32 current_cal_context_crc(void) { return CONTEXT_CRC; }
u32 current_cal_profile_crc(void) { return CONTEXT_CRC; }
u32 current_cal_legacy_profile_crc(void) { return 0x87654321UL; }
u16 current_cal_pwm_logical_max(void) { return 999U; }
current_cal_curve_result_en current_cal_curve_validate(const current_cal_curve_t *curve, u32 context) {
    if(curve==NULL) return CURRENT_CAL_CURVE_NULL;
    return context==CONTEXT_CRC && curve->context_crc==CONTEXT_CRC ?
           CURRENT_CAL_CURVE_OK : CURRENT_CAL_CURVE_PROFILE_MISMATCH;
}
void current_cal_storage_init(void) {}
boolean_en current_cal_storage_has_active_curve(void) { return active_curve_valid; }
const current_cal_curve_t *current_cal_storage_active_curve(void) {
    return active_curve_valid ? &active_curve : NULL;
}
u32 current_cal_storage_sequence(void) { return active_curve_valid ? 1U : 0U; }
boolean_en current_cal_storage_commit(const current_cal_curve_t *curve) {
    active_curve=*curve; active_curve_valid=BOOL_TRUE; return BOOL_TRUE;
}
boolean_en current_cal_storage_commit_meter_section(const current_cal_meter_section_t *section) {
    if(fail_meter_store) { fail_meter_store=BOOL_FALSE; return BOOL_FALSE; }
    stored_meter=*section; stored_meter_valid=BOOL_TRUE; return BOOL_TRUE;
}
boolean_en current_cal_storage_get_meter_section(current_cal_meter_section_t *section) {
    if(!stored_meter_valid) return BOOL_FALSE;
    *section=stored_meter;
    if(corrupt_readback) section->payload[20]^=1U;
    return BOOL_TRUE;
}
current_cal_meter_status_en current_cal_storage_meter_status(void) {
    return stored_meter_valid ? CURRENT_CAL_METER_STATUS_VALID :
                                CURRENT_CAL_METER_STATUS_ABSENT;
}
u32 Timer_GetTickCount(void) { return now_tick; }
void sys_pwm_calibration_lock(void) { pwm_locked=BOOL_TRUE; }
void sys_pwm_calibration_unlock(void) { pwm_locked=BOOL_FALSE; }
boolean_en sys_pwm_calibration_set_direct(u16 logical) {
    output_enabled=logical?BOOL_TRUE:BOOL_FALSE; return pwm_locked;
}
boolean_en sys_pwm_calibration_set_percent(const current_cal_curve_t *curve, u8 percent) {
    last_percent_curve=curve; last_percent=percent;
    output_enabled=percent?BOOL_TRUE:BOOL_FALSE; return pwm_locked;
}
boolean_en sys_pwm_calibration_safety_ready(void) { return BOOL_TRUE; }
void sys_pwm_force_off(void) { ++force_off_count; output_enabled=BOOL_FALSE; }
void sys_pwm_get_status(sys_pwm_status_t *status) {
    memset(status,0,sizeof(*status)); status->output_enabled=output_enabled;
    status->calibration_locked=pwm_locked;
}
boolean_en sys_vo_io_get_snapshot(sys_vo_io_snapshot_t *snapshot) {
    memset(snapshot,0,sizeof(*snapshot)); return BOOL_TRUE;
}
boolean_en meter_runtime_get_calibration_snapshot(meter_runtime_calibration_snapshot_t *snapshot) {
    memset(snapshot,0,sizeof(*snapshot)); snapshot->mode=METER_RUNTIME_MODE_CALIBRATED;
    snapshot->coefficient_result=METER_CAL_OK; return BOOL_TRUE;
}
boolean_en meter_runtime_prepare_calibration_reload(void) {
    ++prepare_count; return fail_prepare?BOOL_FALSE:BOOL_TRUE;
}
boolean_en meter_runtime_reload_calibration(void) {
    ++reload_count; return fail_reload?BOOL_FALSE:BOOL_TRUE;
}
void hw_flash_latch_update_fault(void) { ++flash_fault_count; }

static void put32(u8 *p, u32 value) {
    p[0]=(u8)value; p[1]=(u8)(value>>8); p[2]=(u8)(value>>16); p[3]=(u8)(value>>24);
}
static void make_coefficients(meter_cal_coefficients_t *c, u8 payload[96]) {
    static const s32 zeros[6]={1000,200,-20,0,10,5};
    static const u64 factors[6]={671089ULL,6710886ULL,1677722ULL,
        16777216000000000ULL,251658240ULL,83886080000ULL};
    u32 i;
    memset(c,0,sizeof(*c)); c->version=2U; c->channel_count=6U;
    c->context_crc=CONTEXT_CRC;
    for(i=0U;i<6U;++i) { c->zero_raw[i]=zeros[i]; c->factor_q24[i]=factors[i]; }
    c->energy_gain_q24=10ULL*METER_CAL_Q24_ONE;
    c->flags=METER_CAL_FLAGS_ENERGY_CF24;
    c->data_crc=meter_calibration_coefficients_crc(c);
    if(meter_calibration_coefficients_encode(c,CONTEXT_CRC,payload,96U)!=METER_CAL_OK) abort();
}
static current_cal_result_en write_all(const u8 payload[96], u32 crc) {
    current_cal_result_en result;
    result=current_calibration_write_meter_chunk(2U,CONTEXT_CRC,crc,0U,payload,32U);
    if(result!=CAL_OK) return result;
    result=current_calibration_write_meter_chunk(2U,CONTEXT_CRC,crc,32U,payload+32,32U);
    if(result!=CAL_OK) return result;
    return current_calibration_write_meter_chunk(2U,CONTEXT_CRC,crc,64U,payload+64,32U);
}
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK failed line %d: %s\n",__LINE__,#x); return 1; } } while(0)

int main(void) {
    current_cal_status_t status;
    meter_cal_coefficients_t coefficients;
    current_cal_result_en result;
    boolean_en duplicate;
    u8 payload[96], corrupt[96], conflict[32];
    u16 curve_values[7]={0U,50U,100U,150U,200U,250U,300U};
    u32 meter_crc;

    current_calibration_init();
    CHECK(current_calibration_enter("no-curve",1U,11U,CONTEXT_CRC,10U,BOOL_FALSE)==CAL_OK);
    CHECK(current_calibration_begin_meter()==CAL_CURVE_INCOMPLETE);
    CHECK(force_off_count==1);
    CHECK(current_calibration_abort()==CAL_OK);

    /* Exercise the production PWM commit path, then re-enter meter mode. */
    CHECK(current_calibration_enter("pwm",1U,12U,CONTEXT_CRC,10U,BOOL_FALSE)==CAL_OK);
    CHECK(current_calibration_write_curve_chunk(2U,CONTEXT_CRC,0xaabbccddUL,890U,0U,curve_values,7U)==CAL_OK);
    CHECK(current_calibration_write_curve_chunk(2U,CONTEXT_CRC,0xaabbccddUL,890U,7U,curve_values,7U)==CAL_OK);
    CHECK(current_calibration_write_curve_chunk(2U,CONTEXT_CRC,0xaabbccddUL,890U,14U,curve_values,7U)==CAL_OK);
    CHECK(current_calibration_apply_temporary(0xaabbccddUL)==CAL_OK);
    CHECK(current_calibration_commit(CONTEXT_CRC,0xaabbccddUL)==CAL_OK);
    CHECK(current_calibration_exit()==CAL_OK);

    CHECK(current_calibration_enter("meter",10U,100U,CONTEXT_CRC,10U,BOOL_FALSE)==CAL_OK);
    CHECK(current_calibration_prepare_command("meter",11U,CAL_ACTION_BEGIN_METER,101U,&duplicate)==CAL_OK);
    CHECK(duplicate==BOOL_FALSE);
    CHECK(current_calibration_begin_meter()==CAL_OK);
    current_calibration_complete_command(11U,CAL_ACTION_BEGIN_METER,101U,CAL_OK);
    CHECK(current_calibration_prepare_command("meter",11U,CAL_ACTION_BEGIN_METER,101U,&duplicate)==CAL_OK);
    CHECK(duplicate==BOOL_TRUE);
    CHECK(current_calibration_prepare_command("meter",11U,CAL_ACTION_BEGIN_METER,102U,&duplicate)==CAL_SEQ_CONFLICT);
    CHECK(current_calibration_state()==CAL_STATE_METER_READY);
    CHECK(current_calibration_set_test_percent(50U)==CAL_OK);
    CHECK(last_percent_curve==&active_curve && last_percent==50U);
    CHECK(current_calibration_set_test_percent(101U)==CAL_INVALID_PARAM);

    make_coefficients(&coefficients,payload); meter_crc=coefficients.data_crc;
    CHECK(current_calibration_write_meter_chunk(2U,CONTEXT_CRC+1U,meter_crc,0U,payload,32U)==CAL_PROFILE_MISMATCH);
    CHECK(current_calibration_write_meter_chunk(2U,CONTEXT_CRC,meter_crc,0U,payload,32U)==CAL_OK);
    CHECK(current_calibration_write_meter_chunk(2U,CONTEXT_CRC,meter_crc,0U,payload,32U)==CAL_OK);
    memcpy(conflict,payload,32U); conflict[3]^=1U;
    CHECK(current_calibration_write_meter_chunk(2U,CONTEXT_CRC,meter_crc,0U,conflict,32U)==CAL_CHUNK_CONFLICT);
    CHECK(current_calibration_commit_meter(CONTEXT_CRC,meter_crc)==CAL_METER_INCOMPLETE);
    CHECK(current_calibration_write_meter_chunk(2U,CONTEXT_CRC,meter_crc,32U,payload+32,32U)==CAL_OK);
    CHECK(current_calibration_write_meter_chunk(2U,CONTEXT_CRC,meter_crc,64U,payload+64,32U)==CAL_OK);
    current_calibration_get_status(&status);
    CHECK(status.meter_received_count==96U && status.meter_missing_count==0U);
    CHECK(status.meter_complete==BOOL_TRUE && status.meter_validated==BOOL_TRUE);
    CHECK(current_calibration_commit_meter(CONTEXT_CRC,meter_crc+1U)==CAL_METER_CRC_ERROR);
    CHECK(current_calibration_commit_meter(CONTEXT_CRC,meter_crc)==CAL_OK);
    CHECK(prepare_count==1 && reload_count==1 && current_calibration_state()==CAL_STATE_COMMITTED);
    CHECK(stored_meter.data_length==96U && !memcmp(stored_meter.payload,payload,96U));
    CHECK(current_calibration_exit()==CAL_OK);
    current_calibration_get_status(&status);
    CHECK(status.state==CAL_STATE_IDLE && status.meter_received_count==0U);

    /* Complete payload with a bad CRC must not become validated. */
    CHECK(current_calibration_enter("bad-crc",1U,1U,CONTEXT_CRC,10U,BOOL_FALSE)==CAL_OK);
    CHECK(current_calibration_begin_meter()==CAL_OK);
    memcpy(corrupt,payload,96U); corrupt[32]^=1U;
    CHECK(write_all(corrupt,meter_crc)==CAL_METER_CRC_ERROR);
    current_calibration_get_status(&status);
    CHECK(status.meter_complete==BOOL_TRUE && status.meter_validated==BOOL_FALSE);
    CHECK(current_calibration_abort()==CAL_OK);

    /* CRC-correct but physically impossible Q24 factor is rejected. */
    CHECK(current_calibration_enter("bad-factor",1U,1U,CONTEXT_CRC,10U,BOOL_FALSE)==CAL_OK);
    CHECK(current_calibration_begin_meter()==CAL_OK);
    memcpy(corrupt,payload,96U); memset(corrupt+32,0,8U); corrupt[32]=1U;
    put32(corrupt+92,current_cal_crc32(corrupt,92U));
    result=write_all(corrupt,current_cal_crc32(corrupt,92U));
    CHECK(result==CAL_METER_VALIDATION_ERROR);
    CHECK(current_calibration_abort()==CAL_OK);

    /* Exact readback mismatch latches Flash fail-safe and keeps output off. */
    CHECK(current_calibration_enter("verify",1U,1U,CONTEXT_CRC,10U,BOOL_FALSE)==CAL_OK);
    CHECK(current_calibration_begin_meter()==CAL_OK);
    CHECK(write_all(payload,meter_crc)==CAL_OK);
    corrupt_readback=BOOL_TRUE;
    result=current_calibration_commit_meter(CONTEXT_CRC,meter_crc);
    CHECK(result==CAL_METER_VERIFY_ERROR && flash_fault_count==1);
    CHECK(output_enabled==BOOL_FALSE);
    corrupt_readback=BOOL_FALSE;
    CHECK(current_calibration_abort()==CAL_OK);

    /* Abort and timeout both erase all staged meter bytes/bitmaps. */
    CHECK(current_calibration_enter("abort",1U,1U,CONTEXT_CRC,10U,BOOL_FALSE)==CAL_OK);
    CHECK(current_calibration_begin_meter()==CAL_OK);
    CHECK(current_calibration_write_meter_chunk(2U,CONTEXT_CRC,meter_crc,0U,payload,32U)==CAL_OK);
    CHECK(current_calibration_abort()==CAL_OK);
    current_calibration_get_status(&status);
    CHECK(status.meter_received_count==0U && status.meter_missing_count==96U);
    now_tick=0U;
    CHECK(current_calibration_enter("timeout",1U,1U,CONTEXT_CRC,10U,BOOL_FALSE)==CAL_OK);
    CHECK(current_calibration_begin_meter()==CAL_OK);
    CHECK(current_calibration_write_meter_chunk(2U,CONTEXT_CRC,meter_crc,0U,payload,32U)==CAL_OK);
    now_tick=10000U; current_calibration_process();
    current_calibration_get_status(&status);
    CHECK(status.state==CAL_STATE_IDLE && status.meter_received_count==0U);
    CHECK(current_calibration_write_meter_chunk(2U,CONTEXT_CRC,meter_crc,0U,payload,32U)==CAL_INVALID_STATE);

    puts("host meter calibration session harness: PASS");
    return 0;
}
"""


class MeterCalibrationSessionHostTests(unittest.TestCase):
    def test_production_meter_session_state_staging_commit_and_cleanup(self) -> None:
        if not CL.is_file() or not (SCOPE_VC / "include/stdint.h").is_file():
            self.fail("MSVC host compiler and headers are required")
        cache = ROOT / ".cache"
        cache.mkdir(exist_ok=True)
        original_mkdir = tempfile._os.mkdir

        def inherited_acl_mkdir(path: str, mode: int = 0o777) -> None:
            original_mkdir(path, 0o777)

        with patch.object(tempfile._os, "mkdir", side_effect=inherited_acl_mkdir):
            temporary = tempfile.TemporaryDirectory(prefix="meter_session_host_", dir=cache)
        with temporary as directory:
            temp = Path(directory)
            preinclude = temp / "host_preinclude.h"
            harness = temp / "host_harness.c"
            executable = temp / "host_harness.exe"
            batch = temp / "build_host.bat"
            preinclude.write_text(PREINCLUDE, encoding="utf-8")
            harness.write_text(HARNESS, encoding="utf-8")
            batch.write_text(
                textwrap.dedent(
                    f"""\
                    @echo off
                    set "PATH={CL.parent};%PATH%"
                    set "INCLUDE={SCOPE_VC / 'include'};{WINSDK / 'Include' / WINSDK_VERSION / 'ucrt'};{WINSDK / 'Include' / WINSDK_VERSION / 'shared'};{WINSDK / 'Include' / WINSDK_VERSION / 'um'}"
                    set "LIB={MSVC_ROOT / 'lib/onecore/x64'};{WINSDK / 'Lib' / WINSDK_VERSION / 'ucrt/x64'};{WINSDK / 'Lib' / WINSDK_VERSION / 'um/x64'}"
                    "{CL}" /nologo /W3 /TC /I"{ROOT / 'Core/Src'}" /I"{ROOT / 'Core/Src/LampProtocolLib'}" /FI"{preinclude}" \
                      "{ROOT / 'Core/Src/current_calibration.c'}" \
                      "{ROOT / 'Core/Src/meter_calibration.c'}" \
                      "{harness}" /Fe:"{executable}"
                    """
                ).replace("\\\n", " "),
                encoding="utf-8",
            )
            built = subprocess.run(
                ["cmd.exe", "/d", "/c", str(batch)], cwd=temp, text=True,
                encoding="utf-8", errors="replace", capture_output=True,
                timeout=120, check=False,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run(
                [str(executable)], cwd=temp, text=True, encoding="utf-8",
                errors="replace", capture_output=True, timeout=30, check=False,
            )
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("host meter calibration session harness: PASS", ran.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
