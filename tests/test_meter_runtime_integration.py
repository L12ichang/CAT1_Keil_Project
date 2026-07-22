from __future__ import annotations

import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest.mock import patch

from tests.test_meter_calibration_core import find_host_toolchain


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "Core/Src/meter_runtime.c"
CALIBRATION = ROOT / "Core/Src/meter_calibration.c"
MQTT = ROOT / "Core/Src/LampProtocolLib/mqtt_zk_protocol.c"
BL0942 = ROOT / "Core/Src/sys_bl0942.c"
VO_IO = ROOT / "Core/Src/sys_Vo_Io.c"
PROJECT = ROOT / "MDK-ARM-8008000/project.uvprojx"
POWER_DROP = ROOT / "Core/Src/sys_pow_drop_check.c"


PREINCLUDE = r"""
#ifndef METER_RUNTIME_HOST_PREINCLUDE_H
#define METER_RUNTIME_HOST_PREINCLUDE_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define COMMON_H
#define CURRENT_CAL_CURVE_H
#define CURRENT_CAL_STORAGE_H
#define FLASH_ADDRESS_ASSIGNMENT_H
#define HW_FLASH_H
#define _PORTABLE_H_
#define PORTABLE_H
#define PORTABLE_H_
#define SYS_DATA_H
#define SYS_PWM_H
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;
typedef enum { BOOL_FALSE = 0, BOOL_TRUE = 1 } boolean_en;
typedef int HAL_StatusTypeDef;
#define HAL_OK 0
#define HAL_ERROR 1
#define METER_RUNTIME_ENERGY_SLOT_OFFSET 0x340UL
#define METER_RUNTIME_ENERGY_SLOT_RESERVED 0x40UL
#define METER_RUNTIME_ENERGY_SLOT_A_ADDR 0x1000UL
#define METER_RUNTIME_ENERGY_SLOT_B_ADDR 0x2000UL
#define CURRENT_CAL_FLASH_SLOT_OFFSET 0x400UL
#define CURRENT_CAL_METER_PAYLOAD_SIZE 128U
typedef enum { CURRENT_CAL_SECTION_EMPTY=0, CURRENT_CAL_SECTION_VALID=1,
               CURRENT_CAL_SECTION_TOMBSTONE=2 } current_cal_section_state_en;
typedef struct {
    current_cal_section_state_en state; u32 context_crc; u32 data_crc;
    u16 section_version; u16 data_length; u8 payload[128];
} current_cal_meter_section_t;
typedef enum { CURRENT_CAL_SLOT_NONE=0, CURRENT_CAL_SLOT_A=1,
               CURRENT_CAL_SLOT_B=2 } current_cal_slot_en;
typedef enum { CURRENT_CAL_METER_STATUS_ABSENT=0,
               CURRENT_CAL_METER_STATUS_TOMBSTONE,
               CURRENT_CAL_METER_STATUS_VALID,
               CURRENT_CAL_METER_STATUS_INVALID,
               CURRENT_CAL_METER_STATUS_CONFLICT } current_cal_meter_status_en;
typedef boolean_en (*current_cal_shared_peer_prepare_fn)(void);
typedef struct { u32 ac_EnergyP; u16 today_Energy; } sys_data_st;
extern sys_data_st sys_data;
u32 current_cal_crc32(const u8 *, u32);
u32 current_cal_context_crc(void);
boolean_en current_cal_storage_get_meter_section(current_cal_meter_section_t *);
boolean_en current_cal_storage_prepare_shared_page_update(void);
boolean_en current_cal_storage_prepare_calibration_only(void);
void current_cal_storage_register_shared_peer(current_cal_shared_peer_prepare_fn);
current_cal_meter_status_en current_cal_storage_meter_status(void);
current_cal_slot_en current_cal_storage_active_slot(void);
boolean_en current_cal_storage_calibration_redundant(void);
boolean_en current_cal_storage_repair_required(void);
current_cal_slot_en current_cal_storage_repair_safe_peer_target(void);
void hw_flash_read_bytes(u32, u8 *, u32);
HAL_StatusTypeDef hw_flash_update_bytes_checked(u32, const u8 *, u32);
HAL_StatusTypeDef hw_flash_program_bytes_checked(u32, const u8 *, u32);
void hw_flash_latch_update_fault(void);
void sys_pwm_force_off(void);
u32 Timer_GetTickCount(void);
u32 host_get_primask(void);
void host_disable_irq(void);
void host_enable_irq(void);
#define __get_PRIMASK() host_get_primask()
#define __disable_irq() host_disable_irq()
#define __enable_irq() host_enable_irq()
#endif
"""


HARNESS = r"""
#include "meter_runtime.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL:%d:%s\n", __LINE__, #x); return 1; } } while (0)
#define Q24 0x01000000ULL

sys_data_st sys_data;
static current_cal_meter_section_t section;
static boolean_en section_present;
static current_cal_meter_status_en section_status = CURRENT_CAL_METER_STATUS_ABSENT;
static u8 flash_a[64];
static u8 flash_b[64];
static u32 now_tick;
static int fail_update;
static int fail_marker;
static int update_count;
static int marker_count;
static int irq_depth;
static int force_off_count;
static int fault_latch_count;
static current_cal_shared_peer_prepare_fn shared_prepare;
static current_cal_slot_en cal_slot = CURRENT_CAL_SLOT_NONE;
static current_cal_slot_en repair_target = CURRENT_CAL_SLOT_NONE;
static boolean_en cal_redundant = BOOL_TRUE;
static boolean_en cal_repair_required = BOOL_FALSE;
static int cal_prepare_count;

u32 current_cal_context_crc(void) { return 0x12345678UL; }
u32 current_cal_crc32(const u8 *data, u32 length) {
    u32 crc = 0xffffffffUL, i, bit;
    for (i = 0; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320UL : 0U);
    }
    return ~crc;
}
boolean_en current_cal_storage_get_meter_section(current_cal_meter_section_t *out) {
    if (!section_present) return BOOL_FALSE;
    *out = section; return BOOL_TRUE;
}
void current_cal_storage_register_shared_peer(current_cal_shared_peer_prepare_fn fn) { shared_prepare=fn; }
boolean_en current_cal_storage_prepare_shared_page_update(void) { return shared_prepare ? shared_prepare() : BOOL_TRUE; }
boolean_en current_cal_storage_prepare_calibration_only(void) { ++cal_prepare_count; return BOOL_TRUE; }
current_cal_meter_status_en current_cal_storage_meter_status(void) {
    return section_status;
}
current_cal_slot_en current_cal_storage_active_slot(void) { return cal_slot; }
boolean_en current_cal_storage_calibration_redundant(void) { return cal_redundant; }
boolean_en current_cal_storage_repair_required(void) { return cal_repair_required; }
current_cal_slot_en current_cal_storage_repair_safe_peer_target(void) { return repair_target; }
void hw_flash_latch_update_fault(void) { ++fault_latch_count; }
void sys_pwm_force_off(void) { ++force_off_count; }
void hw_flash_read_bytes(u32 address, u8 *data, u32 length) {
    memcpy(data, address == METER_RUNTIME_ENERGY_SLOT_A_ADDR ? flash_a : flash_b, length);
}
HAL_StatusTypeDef hw_flash_update_bytes_checked(u32 address, const u8 *data, u32 length) {
    ++update_count;
    if (fail_update) return HAL_ERROR;
    memcpy(address == METER_RUNTIME_ENERGY_SLOT_A_ADDR ? flash_a : flash_b, data, length);
    return HAL_OK;
}
HAL_StatusTypeDef hw_flash_program_bytes_checked(u32 address, const u8 *data, u32 length) {
    u8 *slot = address < METER_RUNTIME_ENERGY_SLOT_B_ADDR ? flash_a : flash_b;
    ++marker_count;
    if (fail_marker) return HAL_ERROR;
    memcpy(slot + 60, data, length); return HAL_OK;
}
u32 Timer_GetTickCount(void) { return now_tick; }
u32 host_get_primask(void) { return irq_depth ? 1U : 0U; }
void host_disable_irq(void) { ++irq_depth; }
void host_enable_irq(void) { --irq_depth; }

static void put24(u8 *p, u32 value) {
    p[0]=(u8)value; p[1]=(u8)(value>>8); p[2]=(u8)(value>>16);
}
static void put32le(u8 *p, u32 value) {
    p[0]=(u8)value; p[1]=(u8)(value>>8); p[2]=(u8)(value>>16); p[3]=(u8)(value>>24);
}
static void energy_set_sequence(u8 record[64], u32 sequence) {
    put32le(record+8,sequence);
    put32le(record+56,current_cal_crc32(record,56));
}
static void make_frame(u8 frame[23], u32 i, u32 v, u32 fast, u32 watt,
                       u32 cf, u16 freq, u8 status) {
    u8 sum = METER_RUNTIME_BL0942_READ_COMMAND; int n;
    memset(frame, 0, 23); frame[0]=0x55;
    put24(frame+1,i); put24(frame+4,v); put24(frame+7,fast);
    put24(frame+10,watt); put24(frame+13,cf);
    frame[16]=(u8)freq; frame[17]=(u8)(freq>>8); frame[19]=status;
    for(n=0;n<=21;++n) sum=(u8)(sum+frame[n]);
    frame[22]=(u8)~sum;
}
static int make_coefficients(void) {
    meter_cal_coefficients_t c;
    memset(&c,0,sizeof(c)); c.version=2; c.channel_count=6;
    c.context_crc=current_cal_context_crc();
    c.zero_raw[0]=1000; c.zero_raw[1]=200; c.zero_raw[2]=-20;
    c.zero_raw[3]=0; c.zero_raw[4]=10; c.zero_raw[5]=5;
    c.factor_q24[0]=671089ULL;
    c.factor_q24[1]=6710886ULL;
    c.factor_q24[2]=1677722ULL;
    c.factor_q24[3]=1000000000ULL*Q24;
    c.factor_q24[4]=15ULL*Q24;
    c.factor_q24[5]=5000ULL*Q24;
    c.energy_gain_q24=10ULL*Q24; c.flags=3;
    c.data_crc=meter_calibration_coefficients_crc(&c);
    memset(&section,0,sizeof(section)); section.state=CURRENT_CAL_SECTION_VALID;
    section.context_crc=current_cal_context_crc(); section.data_crc=1;
    section.section_version=2; section.data_length=96;
    CHECK(meter_calibration_coefficients_encode(&c,current_cal_context_crc(),
          section.payload,sizeof(section.payload)) == METER_CAL_OK);
    section_present=BOOL_TRUE;
    section_status=CURRENT_CAL_METER_STATUS_VALID;
    return 0;
}
static int parser_tests(void) {
    u8 golden[23], changed[23]; meter_runtime_bl0942_frame_t p; int i;
    make_frame(golden,0x123456,0x654321,0xabcdef,0xffffff,0x00fedc,20000,0x35);
    CHECK(meter_runtime_parse_bl0942_frame(golden,23,&p)==METER_RUNTIME_FRAME_OK);
    CHECK(p.current_rms_raw==0x123456 && p.voltage_rms_raw==0x654321);
    CHECK(p.current_fast_raw==0xabcdef && p.cf_counter24==0x00fedc);
    CHECK(p.signed_watt_raw==-1 && p.frequency_period_raw==20000 && p.status==0x35);
    CHECK(meter_runtime_parse_bl0942_frame(golden,22,&p)==METER_RUNTIME_FRAME_BAD_LENGTH);
    for(i=0;i<23;++i) {
        memcpy(changed,golden,23); changed[i]^=1;
        CHECK(meter_runtime_parse_bl0942_frame(changed,23,&p)!=METER_RUNTIME_FRAME_OK);
    }
    make_frame(golden,1,2,3,0x7fffff,4,20000,0);
    CHECK(meter_runtime_parse_bl0942_frame(golden,23,&p)==0 && p.signed_watt_raw==8388607);
    make_frame(golden,1,2,3,0x800000,4,20000,0);
    CHECK(meter_runtime_parse_bl0942_frame(golden,23,&p)==0 && p.signed_watt_raw==-8388608);
    make_frame(golden,1,2,3,0xffffff,4,20000,0);
    CHECK(meter_runtime_parse_bl0942_frame(golden,23,&p)==0 && p.signed_watt_raw==-1);
    make_frame(golden,1,2,3,4,5,20000,0); golden[21]=1;
    { u8 sum=METER_RUNTIME_BL0942_READ_COMMAND; for(i=0;i<=21;++i)sum=(u8)(sum+golden[i]); golden[22]=(u8)~sum; }
    CHECK(meter_runtime_parse_bl0942_frame(golden,23,&p)==METER_RUNTIME_FRAME_BAD_RESERVED);
    return 0;
}
static int make_energy_pair(u8 frame_raw[23],
                            meter_runtime_bl0942_frame_t *frame,
                            meter_runtime_legacy_input_t *legacy) {
    memset(flash_a,0xff,sizeof(flash_a)); memset(flash_b,0xff,sizeof(flash_b));
    fail_update=0; fail_marker=0; cal_slot=CURRENT_CAL_SLOT_NONE;
    cal_redundant=BOOL_TRUE; cal_repair_required=BOOL_FALSE;
    repair_target=CURRENT_CAL_SLOT_NONE; now_tick=100;
    CHECK(make_coefficients()==0); meter_runtime_init();
    make_frame(frame_raw,1250200,6001000,0,499980,7,20000,7);
    CHECK(meter_runtime_parse_bl0942_frame(frame_raw,23,frame)==0);
    frame->sequence=40; frame->sample_tick=now_tick;
    meter_runtime_publish_bl0942(frame,legacy); meter_runtime_process();
    CHECK(!memcmp(flash_a,flash_b,64)); return 0;
}
static int coordinator_tests(void) {
    u8 raw[23], first[64], second[64];
    meter_runtime_bl0942_frame_t frame;
    meter_runtime_legacy_input_t legacy;
    meter_runtime_snapshot_t snapshot;
    int before, force_before;
    memset(&legacy,0,sizeof(legacy));

    CHECK(make_energy_pair(raw,&frame,&legacy)==0); memcpy(first,flash_a,64);
    memset(flash_b,0xff,64); cal_slot=CURRENT_CAL_SLOT_A; cal_redundant=BOOL_FALSE;
    before=update_count; CHECK(current_cal_storage_prepare_shared_page_update()==BOOL_TRUE);
    CHECK(update_count==before+1 && cal_prepare_count>0);
    CHECK(!memcmp(flash_a,flash_b,64));

    memset(flash_b,0xff,64); cal_slot=CURRENT_CAL_SLOT_B; cal_redundant=BOOL_FALSE;
    before=update_count; force_before=force_off_count;
    CHECK(current_cal_storage_prepare_shared_page_update()==BOOL_FALSE);
    CHECK(update_count==before && force_off_count>force_before);

    memset(flash_a,0xff,64); memset(flash_b,0xff,64);
    cal_slot=CURRENT_CAL_SLOT_NONE; cal_redundant=BOOL_TRUE;
    before=update_count; CHECK(current_cal_storage_prepare_shared_page_update()==BOOL_TRUE);
    CHECK(update_count==before);

    CHECK(make_energy_pair(raw,&frame,&legacy)==0); memcpy(first,flash_a,64);
    CHECK(meter_runtime_energy_clear()==BOOL_TRUE); memcpy(second,flash_a,64);
    memcpy(flash_a,first,64); memcpy(flash_b,second,64);
    energy_set_sequence(flash_a,55U); energy_set_sequence(flash_b,55U);
    sys_data.ac_EnergyP=999U; force_before=force_off_count; before=update_count;
    meter_runtime_init(); CHECK(meter_runtime_get_snapshot(&snapshot)==BOOL_FALSE);
    CHECK(snapshot.energy_valid==BOOL_FALSE && snapshot.total_energy_uwh==0U);
    CHECK(force_off_count>force_before && update_count==before);

    memcpy(flash_a,first,64); memcpy(flash_b,second,64);
    energy_set_sequence(flash_a,0U); energy_set_sequence(flash_b,0x80000000UL);
    force_before=force_off_count; before=update_count; meter_runtime_init();
    CHECK(meter_runtime_get_snapshot(&snapshot)==BOOL_FALSE);
    CHECK(snapshot.energy_valid==BOOL_FALSE && snapshot.total_energy_uwh==0U);
    CHECK(force_off_count>force_before && update_count==before);

    memset(flash_a,0x11,64); memset(flash_b,0xff,64);
    force_before=force_off_count; before=update_count; meter_runtime_init();
    CHECK(meter_runtime_get_snapshot(&snapshot)==BOOL_FALSE);
    CHECK(snapshot.energy_valid==BOOL_FALSE && snapshot.total_energy_uwh==0U);
    CHECK(current_cal_storage_prepare_shared_page_update()==BOOL_FALSE);
    CHECK(force_off_count>force_before && update_count==before);

    memcpy(flash_b,first,64); memset(flash_a,0xff,64);
    cal_repair_required=BOOL_TRUE; repair_target=CURRENT_CAL_SLOT_NONE;
    before=update_count; CHECK(current_cal_storage_prepare_shared_page_update()==BOOL_FALSE);
    CHECK(update_count==before);
    repair_target=CURRENT_CAL_SLOT_A; before=update_count;
    CHECK(current_cal_storage_prepare_shared_page_update()==BOOL_TRUE);
    CHECK(update_count==before+1 && !memcmp(flash_a,flash_b,64));

    cal_repair_required=BOOL_FALSE; repair_target=CURRENT_CAL_SLOT_NONE;
    cal_slot=CURRENT_CAL_SLOT_NONE; cal_redundant=BOOL_TRUE;
    return 0;
}
int main(void) {
    u8 raw[23]; meter_runtime_bl0942_frame_t frame;
    meter_runtime_legacy_input_t legacy; meter_runtime_output_sample_t output;
    meter_runtime_snapshot_t snap; u64 saved_total;
    memset(flash_a,0xff,sizeof(flash_a)); memset(flash_b,0xff,sizeof(flash_b));
    CHECK(parser_tests()==0); CHECK(coordinator_tests()==0);
    update_count=0; marker_count=0;
    memset(flash_a,0xff,sizeof(flash_a)); memset(flash_b,0xff,sizeof(flash_b));
    CHECK(make_coefficients()==0); sys_data.ac_EnergyP=123;
    now_tick=1000; meter_runtime_init();
    CHECK(meter_runtime_mode()==METER_RUNTIME_MODE_CALIBRATED);
    memset(&legacy,0,sizeof(legacy));
    make_frame(raw,1250200,6001000,0,499980,100,20000,7);
    CHECK(meter_runtime_parse_bl0942_frame(raw,23,&frame)==0);
    frame.sequence=1; frame.sample_tick=now_tick;
    meter_runtime_publish_bl0942(&frame,&legacy); meter_runtime_process();
    CHECK(update_count==2 && marker_count==2);
    now_tick=1100; make_frame(raw,1250200,6001000,0,499980,110,20000,7);
    CHECK(meter_runtime_parse_bl0942_frame(raw,23,&frame)==0);
    frame.sequence=2; frame.sample_tick=now_tick;
    meter_runtime_publish_bl0942(&frame,&legacy);
    memset(&output,0,sizeof(output)); output.voltage_adc_raw=3744;
    output.current_adc_raw=183; output.sequence=9; output.sample_tick=now_tick;
    meter_runtime_publish_output(&output);
    CHECK(meter_runtime_get_snapshot(&snap)==BOOL_TRUE);
    CHECK(snap.input_sequence==2 && snap.input_voltage_mv==240000);
    CHECK(snap.input_current_ua==500000 && snap.input_active_power_mw==50000);
    CHECK(snap.input_frequency_millihz==50000 && snap.input_pf_ppm==416667);
    CHECK(snap.output_sequence==9 && snap.output_voltage_mv==56010);
    CHECK(snap.output_current_ua==890000 && snap.output_power_mw==49849);
    CHECK(snap.total_energy_uwh==1230100 && snap.session_energy_uwh==100);
    saved_total=snap.total_energy_uwh;
    CHECK(meter_runtime_power_down_save()==BOOL_TRUE);
    CHECK(update_count==4 && marker_count==4);
    /* Reboot selects the newer B record and never adds legacy migration twice. */
    now_tick=2000; meter_runtime_init();
    CHECK(meter_runtime_get_snapshot(&snap)==BOOL_FALSE);
    CHECK(snap.total_energy_uwh==saved_total && snap.session_energy_uwh==0);
    make_frame(raw,1250200,6001000,0,499980,999,20000,7);
    CHECK(meter_runtime_parse_bl0942_frame(raw,23,&frame)==0);
    frame.sequence=3; frame.sample_tick=now_tick;
    meter_runtime_publish_bl0942(&frame,&legacy);
    CHECK(meter_runtime_get_snapshot(&snap)==BOOL_TRUE);
    CHECK(snap.total_energy_uwh==saved_total);
    /* Stale samples are flagged without mutating their sequence/value. */
    now_tick+=2001; CHECK(meter_runtime_get_snapshot(&snap)==BOOL_FALSE);
    CHECK(snap.input_sequence==3);
    /* Marker failure leaves the previously committed A/B winner readable. */
    now_tick+=1; make_frame(raw,1250200,6001000,0,499980,1000,20000,7);
    CHECK(meter_runtime_parse_bl0942_frame(raw,23,&frame)==0);
    frame.sequence=4; frame.sample_tick=now_tick;
    meter_runtime_publish_bl0942(&frame,&legacy); fail_marker=1;
    CHECK(meter_runtime_power_down_save()==BOOL_FALSE); fail_marker=0;
    CHECK(force_off_count>0 && fault_latch_count>0);
    now_tick+=1; meter_runtime_init();
    CHECK(meter_runtime_get_snapshot(&snap)==BOOL_FALSE);
    CHECK(snap.total_energy_uwh==saved_total);
    /* A first-checkpoint flash fault is retried on a bounded schedule, not in
     * a hot loop on every process pass. */
    memset(flash_a,0xff,sizeof(flash_a)); memset(flash_b,0xff,sizeof(flash_b));
    CHECK(make_coefficients()==0); now_tick=5000; meter_runtime_init();
    make_frame(raw,1250200,6001000,0,499980,77,20000,7);
    CHECK(meter_runtime_parse_bl0942_frame(raw,23,&frame)==0);
    frame.sequence=20; frame.sample_tick=now_tick;
    meter_runtime_publish_bl0942(&frame,&legacy);
    fail_update=1;
    { int before=update_count;
      meter_runtime_process(); CHECK(update_count==before+1);
      meter_runtime_process(); CHECK(update_count==before+1);
      now_tick+=299999; meter_runtime_process(); CHECK(update_count==before+1);
      now_tick+=1; meter_runtime_process(); CHECK(update_count==before+2); }
    fail_update=0;
    /* An invalid meter section keeps PWM off but must not deadlock the
     * shared-page coordinator that is needed to replace that section. */
    memset(flash_a,0xff,sizeof(flash_a)); memset(flash_b,0xff,sizeof(flash_b));
    section_present=BOOL_TRUE; section_status=CURRENT_CAL_METER_STATUS_INVALID;
    meter_runtime_init();
    CHECK(meter_runtime_mode()==METER_RUNTIME_MODE_INVALID);
    CHECK(current_cal_storage_prepare_shared_page_update()==BOOL_TRUE);
    CHECK(make_coefficients()==0); meter_runtime_init();
    CHECK(meter_runtime_mode()==METER_RUNTIME_MODE_CALIBRATED);
    /* Missing coefficient section is an explicit legacy fallback mode. */
    section_present=BOOL_FALSE; section_status=CURRENT_CAL_METER_STATUS_ABSENT;
    meter_runtime_init();
    CHECK(meter_runtime_mode()==METER_RUNTIME_MODE_FALLBACK);
    CHECK(meter_runtime_get_snapshot(&snap)==BOOL_FALSE);
    CHECK(snap.energy_valid==BOOL_TRUE &&
          snap.total_energy_uwh==(u64)sys_data.ac_EnergyP*10000ULL);
    memset(&legacy,0,sizeof(legacy)); legacy.voltage_01v=2300;
    legacy.current_ma=321; legacy.active_power_001w=4567;
    legacy.pf_percent=88; legacy.session_energy_001wh=12;
    legacy.total_energy_001wh=34; frame.sequence=5; frame.sample_tick=now_tick;
    meter_runtime_publish_bl0942(&frame,&legacy);
    CHECK(meter_runtime_get_snapshot(&snap)==BOOL_TRUE);
    CHECK(snap.input_voltage_mv==230000 && snap.input_current_ua==321000);
    CHECK(snap.input_active_power_mw==45670 && snap.input_pf_ppm==880000);
    CHECK(snap.session_energy_uwh==120000 && snap.total_energy_uwh==340000);
    puts("meter runtime production-C harness PASS"); return 0;
}
"""

POWER_DROP_PREINCLUDE = r"""
#ifndef POWER_DROP_HOST_PREINCLUDE_H
#define POWER_DROP_HOST_PREINCLUDE_H
#include <stdint.h>
#include <stdio.h>
#define COMMON_H
#define SYS_POW_DROP_CHECK_H
#define SYS_DATA_H
#define SYS_BL0942_H
#define SYS_PWM_H
typedef uint8_t u8;
typedef uint16_t u16;
typedef enum { BOOL_FALSE=0, BOOL_TRUE=1 } boolean_en;
typedef struct { uint32_t Pin,Mode,Pull,Speed; } GPIO_InitTypeDef;
#define GPIO_PIN_0 1U
#define GPIO_MODE_INPUT 0U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U
#define GPIOA ((void *)0)
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
void HAL_GPIO_Init(void *, GPIO_InitTypeDef *);
boolean_en sys_bl0942_power_down_save(void);
boolean_en sys_data_store_checked(void);
void sys_pwm_force_off(void);
#endif
"""

POWER_DROP_HARNESS = r"""
u16 ac_voltage_8209;
static boolean_en meter_result;
static boolean_en system_result;
static int meter_calls, system_calls, force_off_calls;
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *init) { (void)port; (void)init; }
boolean_en sys_bl0942_power_down_save(void) { ++meter_calls; return meter_result; }
boolean_en sys_data_store_checked(void) { ++system_calls; return system_result; }
void sys_pwm_force_off(void) { ++force_off_calls; }
void sys_pow_drop_check_timer(void);
void sys_pow_drop_check_process(void);
#define CHECK(x) do { if(!(x)) { printf("FAIL:%d:%s\n",__LINE__,#x); return 1; } } while(0)
static void tick(unsigned int count) { while(count--) sys_pow_drop_check_timer(); }
int main(void) {
    ac_voltage_8209=100U; tick(11U); sys_pow_drop_check_process();
    meter_result=BOOL_FALSE; system_result=BOOL_TRUE;
    ac_voltage_8209=0U; tick(11U); sys_pow_drop_check_process();
    CHECK(meter_calls==1 && system_calls==0 && force_off_calls==1);

    /* A second edge inside the cooldown performs no persistence call. */
    ac_voltage_8209=100U; tick(11U); sys_pow_drop_check_process();
    ac_voltage_8209=0U; tick(11U); sys_pow_drop_check_process();
    CHECK(meter_calls==1 && system_calls==0 && force_off_calls==1);

    ac_voltage_8209=100U; tick(600U); sys_pow_drop_check_process();
    meter_result=BOOL_TRUE; system_result=BOOL_FALSE;
    ac_voltage_8209=0U; tick(11U); sys_pow_drop_check_process();
    CHECK(meter_calls==2 && system_calls==1 && force_off_calls==2);
    puts("power-drop short-circuit production-C harness PASS");
    return 0;
}
"""


class MeterRuntimeIntegrationTests(unittest.TestCase):
    def test_power_drop_short_circuits_system_store_after_meter_failure(self) -> None:
        toolchain = find_host_toolchain()
        if toolchain is None:
            self.fail("MSVC host compiler is required for production-C power-drop tests")
        cl, link, include_paths, library_paths = toolchain
        cache = ROOT / ".cache"
        cache.mkdir(exist_ok=True)
        original_mkdir = tempfile._os.mkdir

        def inherited_acl_mkdir(path: str, mode: int = 0o777) -> None:
            original_mkdir(path, 0o777)

        with patch.object(tempfile._os, "mkdir", side_effect=inherited_acl_mkdir):
            temporary = tempfile.TemporaryDirectory(prefix="power_drop_host_", dir=cache)
        with temporary as td:
            temp = Path(td)
            preinclude = temp / "preinclude.h"
            harness = temp / "harness.c"
            executable = temp / "power_drop_host.exe"
            preinclude.write_text(textwrap.dedent(POWER_DROP_PREINCLUDE), encoding="utf-8")
            harness.write_text(textwrap.dedent(POWER_DROP_HARNESS), encoding="utf-8")
            command = [
                str(cl), "/nologo", "/W4", "/WX", "/wd4819", "/TC", "/Od",
                f"/FI{preinclude}", *[f"/I{path}" for path in include_paths],
                str(harness), str(POWER_DROP), f"/Fe:{executable}", "/link",
                f"/LIBPATH:{link.parent}", *[f"/LIBPATH:{path}" for path in library_paths],
            ]
            built = subprocess.run(command, cwd=temp, text=True, encoding="mbcs",
                                   errors="replace", capture_output=True, timeout=90)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], cwd=temp, text=True, encoding="mbcs",
                                 errors="replace", capture_output=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("short-circuit production-C harness PASS", ran.stdout)

    def test_production_c_parser_snapshot_energy_and_checkpoint(self) -> None:
        toolchain = find_host_toolchain()
        if toolchain is None:
            self.fail("MSVC host compiler is required for production-C runtime tests")
        cl, link, include_paths, library_paths = toolchain
        cache = ROOT / ".cache"
        cache.mkdir(exist_ok=True)
        original_mkdir = tempfile._os.mkdir

        def inherited_acl_mkdir(path: str, mode: int = 0o777) -> None:
            original_mkdir(path, 0o777)

        with patch.object(tempfile._os, "mkdir",
                          side_effect=inherited_acl_mkdir):
            temporary = tempfile.TemporaryDirectory(
                prefix="meter_runtime_host_", dir=cache)
        with temporary as td:
            temp = Path(td)
            preinclude = temp / "preinclude.h"
            harness = temp / "harness.c"
            preinclude.write_text(textwrap.dedent(PREINCLUDE), encoding="utf-8")
            harness.write_text(textwrap.dedent(HARNESS), encoding="utf-8")
            command = [
                str(cl), "/nologo", "/W4", "/WX", "/wd4819", "/TC", "/Od",
                f"/FI{preinclude}", f"/I{ROOT / 'Core/Src'}",
                f"/I{ROOT / 'Core/Src/LampProtocolLib'}",
                *[f"/I{path}" for path in include_paths],
                str(harness), str(RUNTIME), str(CALIBRATION),
                f"/Fe:{temp / 'meter_runtime_host.exe'}", "/link",
                f"/LIBPATH:{link.parent}",
                *[f"/LIBPATH:{path}" for path in library_paths],
            ]
            built = subprocess.run(command, cwd=temp, text=True,
                                   encoding="mbcs", errors="replace",
                                   capture_output=True, timeout=90)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(temp / "meter_runtime_host.exe")],
                                 cwd=temp, text=True, encoding="mbcs",
                                 errors="replace", capture_output=True,
                                 timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("production-C harness PASS", ran.stdout)

    def test_runtime_integration_contracts_are_explicit(self) -> None:
        runtime = RUNTIME.read_text(encoding="utf-8")
        mqtt = MQTT.read_text(encoding="utf-8")
        bl = BL0942.read_text(encoding="utf-8")
        vo = VO_IO.read_text(encoding="utf-8")
        project = PROJECT.read_text(encoding="utf-8")
        self.assertIn("bytes[18] != 0U || bytes[20] != 0U || bytes[21] != 0U", runtime)
        self.assertIn("for (index = 0U; index <= 21U; ++index)", runtime)
        self.assertIn("meter_calibration_sign_extend_s24", runtime)
        self.assertIn("METER_RUNTIME_ENERGY_SLOT_OFFSET   0x340UL",
                      (ROOT / "Core/Src/flash_address_assignment.h").read_text())
        self.assertIn("current_cal_storage_prepare_shared_page_update", runtime)
        self.assertIn("hw_flash_program_bytes_checked", runtime)
        self.assertIn("meter_runtime_get_snapshot(&meter)", mqtt)
        self.assertIn("EleInfo.f is power factor", mqtt)
        self.assertNotIn("cJSON_CreateNumber((double)Z_ac_current)", mqtt)
        self.assertIn("meter_runtime_parse_bl0942_frame", bl)
        self.assertIn("meter_runtime_publish_output(&meter_sample)", vo)
        self.assertEqual(project.count("<FileName>meter_runtime.c</FileName>"), 2)


if __name__ == "__main__":
    unittest.main()
