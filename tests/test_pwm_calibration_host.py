from __future__ import annotations

import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
VCVARS = Path(
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)
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
#ifndef HOST_PREINCLUDE_H
#define HOST_PREINCLUDE_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMON_H
#define FACTORY_DATA_H
#define FLASH_ADDRESS_ASSIGNMENT_H
#define HW_FLASH_H
#define CURRENT_CAL_CURVE_H
#define CURRENT_CAL_STORAGE_H

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef enum { BOOL_FALSE = 0, BOOL_TRUE = 1 } boolean_en;
typedef int HAL_StatusTypeDef;
#define HAL_OK 0
#define HAL_ERROR 1

extern u8 factory_user_buff[128];
extern u16 SET_OUTCUR_temp;
extern u16 HWMAX_OUTCUR_temp;
extern u16 OUTPUT_CUR_SENSOR_temp;
extern u16 OP_PWM_OFFSET_temp;
#define SID (*(u8 *)(factory_user_buff + 0x04))
#define MID (*(u8 *)(factory_user_buff + 0x05))
#define DRV_VERSION (*(u8 *)(factory_user_buff + 0x06))
#define SET_OUTCUR SET_OUTCUR_temp
#define HWMAX_OUTCUR HWMAX_OUTCUR_temp
#define OUTPUT_CUR_SENSOR OUTPUT_CUR_SENSOR_temp
#define OP_PWM_OFFSET OP_PWM_OFFSET_temp

#define CURRENT_CAL_POINT_COUNT 21U
#define CURRENT_CAL_CURVE_VERSION_LEGACY 1U
#define CURRENT_CAL_CURVE_VERSION 2U
#define CURRENT_CAL_PWM_PERIOD_COUNTS 1000U
#define CURRENT_CAL_FLASH_SLOT_RESERVED 0x100U
#define FLASH_PAGE_SIZE 0x800U
#define SHARED_PAGE_A_ADDR 0x08005800UL
#define SHARED_PAGE_B_ADDR 0x08007000UL
#define CURRENT_CAL_FLASH_SLOT_A_ADDR 0x08005c00UL
#define CURRENT_CAL_FLASH_SLOT_B_ADDR 0x08007400UL
#define CURRENT_CAL_METER_PAYLOAD_SIZE 128U
#define CURRENT_CAL_STATIC_ASSERT_JOIN_(a,b) a##b
#define CURRENT_CAL_STATIC_ASSERT_JOIN(a,b) CURRENT_CAL_STATIC_ASSERT_JOIN_(a,b)
#define CURRENT_CAL_STATIC_ASSERT(x) typedef char CURRENT_CAL_STATIC_ASSERT_JOIN(host_assert_,__LINE__)[(x)?1:-1]

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
typedef enum { CURRENT_CAL_SLOT_NONE=0, CURRENT_CAL_SLOT_A=1, CURRENT_CAL_SLOT_B=2 }
    current_cal_slot_en;
typedef enum { CURRENT_CAL_SECTION_EMPTY=0, CURRENT_CAL_SECTION_VALID=1,
               CURRENT_CAL_SECTION_TOMBSTONE=2 } current_cal_section_state_en;
typedef struct {
    current_cal_section_state_en state; u32 context_crc; u32 data_crc;
    u16 section_version; u16 data_length; u8 payload[CURRENT_CAL_METER_PAYLOAD_SIZE];
} current_cal_meter_section_t;

u32 current_cal_crc32(const u8 *, u32);
u16 current_cal_pwm_logical_max(void);
u32 current_cal_context_crc(void);
u32 current_cal_legacy_profile_crc(void);
u32 current_cal_profile_crc(void);
u32 current_cal_curve_crc(const current_cal_curve_t *);
current_cal_curve_result_en current_cal_curve_validate(const current_cal_curve_t *, u32);
u16 current_cal_curve_interpolate_current(const current_cal_curve_t *, u32);
u16 current_cal_curve_interpolate_setpoint(const current_cal_curve_t *, u8, u32);
u16 current_cal_curve_interpolate(const current_cal_curve_t *, u8);
void current_cal_storage_init(void);
boolean_en current_cal_storage_has_active_curve(void);
boolean_en current_cal_storage_active_curve_is_legacy(void);
const current_cal_curve_t *current_cal_storage_active_curve(void);
u32 current_cal_storage_sequence(void);
current_cal_slot_en current_cal_storage_active_slot(void);
boolean_en current_cal_storage_commit(const current_cal_curve_t *);
boolean_en current_cal_storage_ensure_v2(void);
boolean_en current_cal_storage_invalidate(void);
boolean_en current_cal_storage_get_meter_section(current_cal_meter_section_t *);
boolean_en current_cal_storage_commit_meter_section(const current_cal_meter_section_t *);
boolean_en current_cal_storage_prepare_shared_page_update(void);

void hw_flash_read_bytes(u32, u8 *, u32);
HAL_StatusTypeDef hw_flash_update_bytes_checked(uint32_t, const u8 *, uint32_t);
HAL_StatusTypeDef hw_flash_program_bytes_checked(uint32_t, const u8 *, uint32_t);
#endif
"""


HARNESS = r"""
u8 factory_user_buff[128];
u16 SET_OUTCUR_temp = 890U;
u16 HWMAX_OUTCUR_temp = 1680U;
u16 OUTPUT_CUR_SENSOR_temp = 100U;
u16 OP_PWM_OFFSET_temp = 412U;
static u8 page_a[FLASH_PAGE_SIZE], page_b[FLASH_PAGE_SIZE];
#define slot_a (page_a + 0x400U)
#define slot_b (page_b + 0x400U)
static int fail_update, fail_update_countdown, fail_update_late, fail_marker;

static u8 *flash_for(u32 address) {
    if (address >= SHARED_PAGE_A_ADDR && address < SHARED_PAGE_A_ADDR + FLASH_PAGE_SIZE)
        return page_a + (address - SHARED_PAGE_A_ADDR);
    if (address >= SHARED_PAGE_B_ADDR && address < SHARED_PAGE_B_ADDR + FLASH_PAGE_SIZE)
        return page_b + (address - SHARED_PAGE_B_ADDR);
    abort();
}
void hw_flash_read_bytes(u32 address, u8 *data, u32 length) {
    memcpy(data, flash_for(address), length);
}
HAL_StatusTypeDef hw_flash_update_bytes_checked(uint32_t address, const u8 *data, uint32_t length) {
    static u8 expected[FLASH_PAGE_SIZE]; u8 *page; u32 offset;
    if (fail_update) { fail_update = 0; return HAL_ERROR; }
    if (fail_update_countdown>0 && --fail_update_countdown==0) return HAL_ERROR;
    page = (address < SHARED_PAGE_B_ADDR) ? page_a : page_b;
    offset = address - ((address < SHARED_PAGE_B_ADDR) ? SHARED_PAGE_A_ADDR : SHARED_PAGE_B_ADDR);
    memcpy(expected,page,FLASH_PAGE_SIZE); memcpy(expected+offset,data,length);
    if (fail_update_late) {
        fail_update_late=0; memset(page,0xff,FLASH_PAGE_SIZE);
        memcpy(page,expected,0x480U); return HAL_ERROR;
    }
    memcpy(page,expected,FLASH_PAGE_SIZE); return HAL_OK;
}
HAL_StatusTypeDef hw_flash_program_bytes_checked(uint32_t address, const u8 *data, uint32_t length) {
    if (fail_marker) { fail_marker = 0; return HAL_ERROR; }
    memcpy(flash_for(address), data, length); return HAL_OK;
}
static void put16(u8 *p, u16 value) { p[0]=(u8)value; p[1]=(u8)(value>>8); }
static void put32(u8 *p, u32 value) {
    p[0]=(u8)value; p[1]=(u8)(value>>8); p[2]=(u8)(value>>16); p[3]=(u8)(value>>24);
}
static u32 get32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24);
}
static void reset_flash(void) { memset(page_a,0xff,sizeof(page_a)); memset(page_b,0xff,sizeof(page_b)); }
static void make_curve(current_cal_curve_t *curve) {
    static const u16 values[21] = {0,56,80,110,138,164,195,220,248,280,306,335,367,392,420,448,476,504,532,560,588};
    memset(curve,0,sizeof(*curve)); curve->curve_version=2; curve->point_count=21;
    curve->calibration_max_current_ma=890; memcpy(curve->logical_pwm,values,sizeof(values));
    curve->context_crc=current_cal_context_crc(); curve->curve_crc=current_cal_curve_crc(curve);
}
static void make_v1(u8 *record, u32 sequence) {
    current_cal_curve_t curve; u8 serialized[46]; u32 i;
    make_curve(&curve); memset(record,0,80); put32(record,0x43414c31UL); put16(record+4,1); put16(record+6,80);
    put32(record+8,sequence); put32(record+12,1); put32(record+16,current_cal_legacy_profile_crc());
    put16(serialized,1); put16(serialized+2,21);
    for(i=0;i<21;i++){ put16(serialized+4+i*2,curve.logical_pwm[i]); put16(record+28+i*2,curve.logical_pwm[i]); }
    put32(record+20,current_cal_crc32(serialized,46)); put16(record+24,1); put16(record+26,21);
    put32(record+72,current_cal_crc32(record,72)); put32(record+76,0x56414c43UL);
}
#define CHECK(x) do { if(!(x)){ fprintf(stderr,"CHECK failed line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static int shared_fault_case(u8 active_b, u32 record_offset, u8 fail_backup) {
    current_cal_curve_t curve, newer; u8 expected[240], payload[60];
    u8 *selected; u32 expected_seq, expected_crc, i;
    reset_flash(); SET_OUTCUR_temp=890U; make_curve(&curve);
    current_cal_storage_init(); CHECK(current_cal_storage_commit(&curve));
    if(active_b) {
        newer=curve; newer.logical_pwm[10]++; newer.curve_crc=current_cal_curve_crc(&newer);
        CHECK(current_cal_storage_commit(&newer));
    }
    selected=current_cal_storage_active_slot()==CURRENT_CAL_SLOT_A ? slot_a : slot_b;
    memcpy(expected,selected,sizeof(expected)); expected_seq=get32(expected+8); expected_crc=get32(expected+232);
    CHECK(current_cal_storage_prepare_shared_page_update());
    CHECK(!memcmp(slot_a,expected,sizeof(expected)) && !memcmp(slot_b,expected,sizeof(expected)));
    for(i=0U;i<sizeof(payload);i++) payload[i]=(u8)(record_offset+i+3U);
    if(fail_backup) {
        CHECK(hw_flash_update_bytes_checked(SHARED_PAGE_A_ADDR+record_offset,payload,sizeof(payload))==HAL_OK);
        fail_update_late=1;
        CHECK(hw_flash_update_bytes_checked(SHARED_PAGE_B_ADDR+record_offset,payload,sizeof(payload))==HAL_ERROR);
    } else {
        fail_update_late=1;
        CHECK(hw_flash_update_bytes_checked(SHARED_PAGE_A_ADDR+record_offset,payload,sizeof(payload))==HAL_ERROR);
    }
    current_cal_storage_init(); CHECK(current_cal_storage_has_active_curve());
    selected=current_cal_storage_active_slot()==CURRENT_CAL_SLOT_A ? slot_a : slot_b;
    CHECK(get32(selected+8)==expected_seq && get32(selected+232)==expected_crc);
    CHECK(!memcmp(selected,expected,sizeof(expected)));
    return 0;
}
int main(void) {
    current_cal_curve_t curve, changed; current_cal_meter_section_t meter, loaded;
    u8 saved_meter[128]; u8 *latest; u32 seq, index;
    factory_user_buff[4]=1; factory_user_buff[5]=7; factory_user_buff[6]=3;
    make_curve(&curve);
    CHECK(curve.curve_crc==0x16b912d1UL);
    for(index=0U;index<21U;index++)
        CHECK(current_cal_curve_interpolate(&curve,(u8)(index*5U))==curve.logical_pwm[index]);
    CHECK(current_cal_curve_interpolate(&curve,2U)==22U);
    CHECK(current_cal_curve_interpolate(&curve,7U)==66U);
    CHECK(current_cal_curve_interpolate(&curve,33U)==210U);
    CHECK(current_cal_curve_interpolate(&curve,73U)==437U);
    CHECK(current_cal_curve_interpolate(&curve,99U)==582U);
    CHECK(current_cal_curve_interpolate_setpoint(&curve,100,890)==588);
    CHECK(current_cal_curve_interpolate_setpoint(&curve,100,536)==368);

    reset_flash(); current_cal_storage_init(); CHECK(!current_cal_storage_has_active_curve());
    CHECK(current_cal_storage_commit(&curve)); CHECK(current_cal_storage_sequence()==1);
    latest = current_cal_storage_active_slot()==CURRENT_CAL_SLOT_A ? slot_a : slot_b;
    CHECK(get32(latest)==0x43414c32UL && latest[4]==2 && latest[6]==240);
    CHECK(get32(latest+236)==0x56414c43UL);

    memset(&meter,0,sizeof(meter)); meter.state=CURRENT_CAL_SECTION_VALID;
    meter.context_crc=0xaabbccddUL; meter.section_version=1; meter.data_length=68;
    for(seq=0;seq<meter.data_length;seq++) meter.payload[seq]=(u8)(seq+3);
    CHECK(current_cal_storage_commit_meter_section(&meter));
    CHECK(current_cal_storage_get_meter_section(&loaded)); memcpy(saved_meter,loaded.payload,128);
    changed=curve; changed.logical_pwm[10]++; changed.curve_crc=current_cal_curve_crc(&changed);
    CHECK(current_cal_storage_commit(&changed));
    CHECK(current_cal_storage_get_meter_section(&loaded)); CHECK(!memcmp(saved_meter,loaded.payload,128));
    CHECK(current_cal_storage_prepare_shared_page_update());
    CHECK(!memcmp(slot_a,slot_b,240));

    seq=current_cal_storage_sequence(); fail_update=1; CHECK(!current_cal_storage_commit(&curve));
    CHECK(current_cal_storage_sequence()==seq);
    fail_marker=1; CHECK(!current_cal_storage_commit(&curve)); CHECK(current_cal_storage_sequence()==seq);

    current_cal_storage_init(); CHECK(current_cal_storage_has_active_curve());
    CHECK(current_cal_storage_sequence()==seq);

    reset_flash(); CHECK(FLASH_PAGE_SIZE-0x400U>=240U); current_cal_storage_init(); CHECK(current_cal_storage_commit(&curve));
    memcpy(slot_b,slot_a,240); current_cal_storage_init(); CHECK(current_cal_storage_has_active_curve());
    put32(slot_b+16,2); put32(slot_b+232,current_cal_crc32(slot_b,232));
    current_cal_storage_init(); CHECK(!current_cal_storage_has_active_curve()); CHECK(current_cal_storage_sequence()==1);
    fail_update=1; CHECK(!current_cal_storage_invalidate());
    current_cal_storage_init(); CHECK(!current_cal_storage_has_active_curve());
    CHECK(current_cal_storage_invalidate()); CHECK(current_cal_storage_sequence()==1);
    CHECK(!memcmp(slot_a,slot_b,240));

    reset_flash(); current_cal_storage_init(); CHECK(current_cal_storage_commit(&curve));
    memcpy(slot_b,slot_a,240); put32(slot_b+16,2);
    put32(slot_b+232,current_cal_crc32(slot_b,232)); current_cal_storage_init();
    CHECK(!current_cal_storage_has_active_curve()); fail_update_countdown=2;
    CHECK(!current_cal_storage_invalidate()); current_cal_storage_init();
    CHECK(!current_cal_storage_has_active_curve()); CHECK(current_cal_storage_invalidate());
    CHECK(!memcmp(slot_a,slot_b,240));

    reset_flash(); current_cal_storage_init(); CHECK(current_cal_storage_commit(&curve));
    memcpy(slot_b,slot_a,240);
    put32(slot_a+8,0U); put32(slot_a+232,current_cal_crc32(slot_a,232));
    put32(slot_b+8,0x80000000UL); put32(slot_b+232,current_cal_crc32(slot_b,232));
    current_cal_storage_init(); CHECK(!current_cal_storage_has_active_curve());
    fail_update=1; CHECK(!current_cal_storage_commit(&curve));
    current_cal_storage_init(); CHECK(!current_cal_storage_has_active_curve());
    CHECK(current_cal_storage_commit(&curve)); CHECK(!memcmp(slot_a,slot_b,240));
    CHECK(current_cal_storage_sequence()==0x80000000UL);

    reset_flash(); current_cal_storage_init(); CHECK(current_cal_storage_commit(&curve));
    memcpy(slot_b,slot_a,240);
    put32(slot_a+8,0U); put32(slot_a+232,current_cal_crc32(slot_a,232));
    put32(slot_b+8,0x80000000UL); put32(slot_b+232,current_cal_crc32(slot_b,232));
    current_cal_storage_init(); CHECK(!current_cal_storage_has_active_curve());
    fail_update_countdown=2; CHECK(!current_cal_storage_commit(&curve));
    current_cal_storage_init(); CHECK(!current_cal_storage_has_active_curve());
    CHECK(current_cal_storage_commit(&curve)); CHECK(!memcmp(slot_a,slot_b,240));
    CHECK(current_cal_storage_sequence()==0x80000000UL);

    put32(slot_a+8,0xffffffffUL); put32(slot_a+232,current_cal_crc32(slot_a,232));
    put32(slot_b+8,1U); put32(slot_b+232,current_cal_crc32(slot_b,232));
    current_cal_storage_init(); CHECK(current_cal_storage_has_active_curve());
    CHECK(current_cal_storage_sequence()==1U);

    for(index=0U;index<3U;index++) {
        static const u32 writer_offsets[3]={0U,0x200U,0x300U};
        CHECK(shared_fault_case(0U,writer_offsets[index],0U)==0);
        CHECK(shared_fault_case(0U,writer_offsets[index],1U)==0);
        CHECK(shared_fault_case(1U,writer_offsets[index],0U)==0);
        CHECK(shared_fault_case(1U,writer_offsets[index],1U)==0);
    }

    reset_flash(); make_v1(slot_a,7); SET_OUTCUR_temp=890; current_cal_storage_init();
    CHECK(current_cal_storage_has_active_curve() && current_cal_storage_active_curve_is_legacy());
    fail_update=1; CHECK(!current_cal_storage_ensure_v2()); CHECK(current_cal_storage_active_curve_is_legacy());
    CHECK(current_cal_storage_ensure_v2()); CHECK(!current_cal_storage_active_curve_is_legacy());
    SET_OUTCUR_temp=536; current_cal_storage_init(); CHECK(current_cal_storage_has_active_curve());
    CHECK(current_cal_storage_active_curve()->calibration_max_current_ma==890);
    CHECK(current_cal_curve_interpolate_setpoint(current_cal_storage_active_curve(),100,536)==368);
    puts("host PWM/storage harness: PASS"); return 0;
}
"""


PWM_PREINCLUDE = r"""
#ifndef HOST_PWM_PREINCLUDE_H
#define HOST_PWM_PREINCLUDE_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SYS_PWM_H
#define SYS_DATA_H
#define SYS_TEMP_OVER_PROTECT_H
#define HW_TIM1_PWM2_H
#define HW_FLASH_H
#define _SYS_VO_IO_H
#define FACTORY_DATA_H
#define CURRENT_CAL_STORAGE_H
#define CURRENT_CAL_CURVE_H

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef enum { BOOL_FALSE = 0, BOOL_TRUE = 1 } boolean_en;
typedef enum { SYS_PWM_SOURCE_INTERNAL = 0, SYS_PWM_SOURCE_NETWORK,
               SYS_PWM_SOURCE_OFFLINE } sys_pwm_source_en;
typedef struct {
    u16 curve_version; u16 point_count; u32 calibration_max_current_ma;
    u16 logical_pwm[21]; u32 context_crc; u32 curve_crc;
} current_cal_curve_t;
typedef struct { u8 placeholder; } sys_vo_io_snapshot_t;
typedef struct {
    u8 requested_percent; u8 effective_percent;
    u16 requested_logical_pwm; u16 applied_logical_pwm;
    u16 compare_value; u16 protect_code;
    boolean_en output_enabled; boolean_en calibration_locked;
    boolean_en limited;
} sys_pwm_status_t;

extern u8 factory_user_buff[128];
extern u16 SET_OUTCUR_temp, HWMAX_OUTCUR_temp;
#define MID (*(u8 *)(factory_user_buff + 0x05))
#define SET_OUTCUR SET_OUTCUR_temp
#define HWMAX_OUTCUR HWMAX_OUTCUR_temp
extern u8 Error_1_OL;
extern u8 dim_bak_to_low_acin;
extern volatile u8 set_percent;

boolean_en low_temp_detect_is_low(u16 *, u16);
boolean_en DC_low_voltage_detect_is_low(u16 *, u16);
boolean_en High_voltage_detect_is_high(u16 *, u16);
boolean_en temp_detect_is_over(u16 *, u16);
void hw_tim1_pwm2_set_PWM_OUT(u16);
u16 hw_tim1_pwm2_get_logical_pwm(void);
u16 hw_tim1_pwm2_get_compare(void);
u16 hw_tim1_pwm2_get_logical_max(void);
boolean_en hw_tim1_pwm2_output_enabled(void);
const current_cal_curve_t *current_cal_storage_active_curve(void);
u16 current_cal_curve_interpolate_setpoint(const current_cal_curve_t *, u8, u32);
u16 current_cal_curve_interpolate(const current_cal_curve_t *, u8);
boolean_en sys_vo_io_get_snapshot(sys_vo_io_snapshot_t *);
boolean_en hw_flash_update_fault_latched(void);

void sys_pwm_output(u8);
void pwm_output(u8);
void sys_pwm_fade_output(u8, u8);
void sys_pwm_output_for_temp_protect(u8);
void sys_pwm_output_on_fade(u8);
void sys_pwm_timer(void);
void sys_pwm_process(void);
void sys_pwm_reload(void);
void sys_pwm_release_and_reload(void);
void sys_pwm_force_off(void);
void sys_pwm_get_status(sys_pwm_status_t *);
void sys_pwm_calibration_lock(void);
void sys_pwm_calibration_unlock(void);
boolean_en sys_pwm_calibration_set_direct(u16);
#endif
"""


PWM_HARNESS = r"""
u8 factory_user_buff[128];
u16 SET_OUTCUR_temp = 890U;
u16 HWMAX_OUTCUR_temp = 1680U;
u8 Error_1_OL;
u8 dim_bak_to_low_acin;
static u16 hardware_pwm;

boolean_en low_temp_detect_is_low(u16 *out, u16 in) { *out=in; return BOOL_FALSE; }
boolean_en DC_low_voltage_detect_is_low(u16 *out, u16 in) { *out=in; return BOOL_FALSE; }
boolean_en High_voltage_detect_is_high(u16 *out, u16 in) { *out=in; return BOOL_FALSE; }
boolean_en temp_detect_is_over(u16 *out, u16 in) { *out=in; return BOOL_FALSE; }
void hw_tim1_pwm2_set_PWM_OUT(u16 value) { hardware_pwm = value > 999U ? 999U : value; }
u16 hw_tim1_pwm2_get_logical_pwm(void) { return hardware_pwm; }
u16 hw_tim1_pwm2_get_compare(void) { return hardware_pwm; }
u16 hw_tim1_pwm2_get_logical_max(void) { return 999U; }
boolean_en hw_tim1_pwm2_output_enabled(void) { return hardware_pwm ? BOOL_TRUE : BOOL_FALSE; }
const current_cal_curve_t *current_cal_storage_active_curve(void) { return NULL; }
u16 current_cal_curve_interpolate_setpoint(const current_cal_curve_t *c, u8 p, u32 s)
{ (void)c; (void)p; (void)s; return 0U; }
u16 current_cal_curve_interpolate(const current_cal_curve_t *c, u8 p)
{ (void)c; (void)p; return 0U; }
boolean_en sys_vo_io_get_snapshot(sys_vo_io_snapshot_t *snapshot)
{ snapshot->placeholder=0U; return BOOL_TRUE; }
boolean_en hw_flash_update_fault_latched(void) { return BOOL_FALSE; }

#define CHECK(x) do { if(!(x)){ fprintf(stderr,"CHECK failed line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void) {
    unsigned int i;
    sys_pwm_status_t status;
    factory_user_buff[5] = 1U;

    sys_pwm_output(50U);
    CHECK(hardware_pwm > 0U);
    sys_pwm_fade_output(10U, 90U);
    sys_pwm_reload();
    CHECK(hardware_pwm > 0U);

    sys_pwm_force_off();
    CHECK(hardware_pwm == 0U);
    CHECK(set_percent == 0U);
    pwm_output(75U);                       /* periodic HALFLOAD-style path */
    CHECK(hardware_pwm == 0U);
    sys_pwm_output_for_temp_protect(70U); /* temperature internal path */
    CHECK(hardware_pwm == 0U);
    sys_pwm_output_on_fade(65U);          /* legacy internal fade path */
    CHECK(hardware_pwm == 0U);
    sys_pwm_fade_output(10U, 90U);
    CHECK(hardware_pwm == 0U);
    sys_pwm_reload();
    for (i=0U; i<300U; ++i) sys_pwm_timer();
    sys_pwm_process();
    CHECK(hardware_pwm == 0U);
    sys_pwm_get_status(&status);
    CHECK(status.output_enabled == BOOL_FALSE);

    sys_pwm_output(25U);
    CHECK(hardware_pwm > 0U);
    sys_pwm_fade_output(20U, 80U);
    sys_pwm_reload();
    sys_pwm_force_off();
    for (i=0U; i<300U; ++i) sys_pwm_timer();
    sys_pwm_process();
    CHECK(hardware_pwm == 0U);
    sys_pwm_release_and_reload();
    sys_pwm_process();
    CHECK(hardware_pwm > 0U);

    sys_pwm_calibration_lock();
    CHECK(hardware_pwm == 0U);
    CHECK(sys_pwm_calibration_set_direct(100U) == BOOL_TRUE);
    CHECK(hardware_pwm == 100U);
    sys_pwm_calibration_unlock();
    sys_pwm_reload();
    sys_pwm_process();
    for (i=0U; i<300U; ++i) sys_pwm_timer();
    CHECK(hardware_pwm == 0U);
    puts("host sys_pwm force-off latch harness: PASS");
    return 0;
}
"""


@unittest.skipUnless(CL.is_file() and (SCOPE_VC / "include/stdint.h").is_file(),
                     "MSVC host compiler or headers are unavailable")
class PwmCalibrationCompiledHostTests(unittest.TestCase):
    def test_real_curve_and_storage_sources_with_flash_faults(self) -> None:
        cache = ROOT / ".cache"
        cache.mkdir(exist_ok=True)
        original_mkdir = tempfile._os.mkdir

        def inherited_acl_mkdir(path: str, mode: int = 0o777) -> None:
            original_mkdir(path, 0o777)

        # Python 3.14 applies a private Windows ACL for mode 0700.  The
        # sandbox cannot traverse that ACL, so inherit the workspace ACL.
        with patch.object(tempfile._os, "mkdir", side_effect=inherited_acl_mkdir):
            temporary = tempfile.TemporaryDirectory(prefix="pwm_cal_host_", dir=cache)
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
                    "{CL}" /nologo /W3 /TC /FI"{preinclude}" \
                      "{ROOT / 'Core/Src/current_cal_curve.c'}" \
                      "{ROOT / 'Core/Src/current_cal_storage.c'}" \
                      "{harness}" /Fe:"{executable}"
                    """
                ).replace("\\\n", " "),
                encoding="utf-8",
            )
            built = subprocess.run(
                ["cmd.exe", "/d", "/c", str(batch)],
                cwd=temp,
                text=True,
                encoding="utf-8",
                errors="replace",
                capture_output=True,
                timeout=120,
                check=False,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run(
                [str(executable)], cwd=temp, text=True, encoding="utf-8",
                errors="replace", capture_output=True,
                timeout=30, check=False,
            )
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("host PWM/storage harness: PASS", ran.stdout)

    def test_real_sys_pwm_force_off_cancels_pending_reload_and_fade(self) -> None:
        cache = ROOT / ".cache"
        cache.mkdir(exist_ok=True)
        original_mkdir = tempfile._os.mkdir

        def inherited_acl_mkdir(path: str, mode: int = 0o777) -> None:
            original_mkdir(path, 0o777)

        with patch.object(tempfile._os, "mkdir", side_effect=inherited_acl_mkdir):
            temporary = tempfile.TemporaryDirectory(prefix="sys_pwm_host_", dir=cache)
        with temporary as directory:
            temp = Path(directory)
            preinclude = temp / "host_pwm_preinclude.h"
            harness = temp / "host_pwm_harness.c"
            executable = temp / "host_pwm_harness.exe"
            batch = temp / "build_host_pwm.bat"
            preinclude.write_text(PWM_PREINCLUDE, encoding="utf-8")
            harness.write_text(PWM_HARNESS, encoding="utf-8")
            batch.write_text(
                textwrap.dedent(
                    f"""\
                    @echo off
                    set "PATH={CL.parent};%PATH%"
                    set "INCLUDE={SCOPE_VC / 'include'};{WINSDK / 'Include' / WINSDK_VERSION / 'ucrt'};{WINSDK / 'Include' / WINSDK_VERSION / 'shared'};{WINSDK / 'Include' / WINSDK_VERSION / 'um'}"
                    set "LIB={MSVC_ROOT / 'lib/onecore/x64'};{WINSDK / 'Lib' / WINSDK_VERSION / 'ucrt/x64'};{WINSDK / 'Lib' / WINSDK_VERSION / 'um/x64'}"
                    "{CL}" /nologo /W3 /TC /FI"{preinclude}" \
                      "{ROOT / 'Core/Src/sys_pwm.c'}" "{harness}" /Fe:"{executable}"
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
            self.assertIn("host sys_pwm force-off latch harness: PASS", ran.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
