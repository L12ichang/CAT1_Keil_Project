#include "sys_data.h"
#include "sys_persistent_storage.h"
#include "factory_user_data.h"
#include "ntc.h"
#include "sys_temp_over_protect.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

u8 factory_user_buff[128];
u16 SET_OUTCUR_temp;
u16 HWMAX_OUTCUR_temp;
u16 OUTPUT_CUR_SENSOR_temp;
u16 OP_PWM_OFFSET_temp;
u16 BOUND_OUTPUT_VOLTAGE_01V_temp;
ntcTemp_t Ntctemp;
ntcTemp_t Ntctemp2;
boolean_en power_status = BOOL_TRUE;

static u32 pwm_reload_calls;

static int expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

void sys_pwm_reload(void)
{
    ++pwm_reload_calls;
}

void factory_user_load_data(void)
{
    memcpy(factory_user_buff, sys_data.fa_Parambuf, sizeof(factory_user_buff));
}

void fac_128_data_default(void)
{
    memset(sys_data.fa_Parambuf, 0xFF, sizeof(sys_data.fa_Parambuf));
    sys_data.fa_Parambuf[0x18U] = 1U;
    sys_data.fa_Parambuf[0x19U] = 85U;
    factory_user_load_data();
}

boolean_en sys_persistent_config_read_section(
    u16 offset,
    u8 *section,
    u16 length,
    u32 *generation)
{
    u8 config[SYS_PERSISTENT_CONFIG_FACTORY_LENGTH +
              SYS_PERSISTENT_CONFIG_DEVICE_LENGTH];

    (void)generation;
    if (offset != SYS_PERSISTENT_CONFIG_FACTORY_OFFSET ||
        length != sizeof(config))
    {
        return BOOL_FALSE;
    }
    memset(config, 0xFF, sizeof(config));
    config[0x18U] = 1U;
    config[0x19U] = 85U;
    config[SYS_PERSISTENT_CONFIG_FACTORY_LENGTH + 0U] = 0x80U;
    config[SYS_PERSISTENT_CONFIG_FACTORY_LENGTH + 1U] = 0U;
    config[SYS_PERSISTENT_CONFIG_FACTORY_LENGTH + 2U] = 0U;
    config[SYS_PERSISTENT_CONFIG_FACTORY_LENGTH + 3U] = 0U;
    memcpy(section, config, sizeof(config));
    return BOOL_TRUE;
}

u32 sys_persistent_get_u32_le(const u8 *source)
{
    return (u32)source[0] |
           ((u32)source[1] << 8U) |
           ((u32)source[2] << 16U) |
           ((u32)source[3] << 24U);
}

void sys_persistent_put_u32_le(u8 *destination, u32 value)
{
    destination[0] = (u8)value;
    destination[1] = (u8)(value >> 8U);
    destination[2] = (u8)(value >> 16U);
    destination[3] = (u8)(value >> 24U);
}

boolean_en sys_persistent_config_update_section(
    u16 offset,
    const u8 *section,
    u16 length,
    u32 *generation)
{
    (void)offset;
    (void)section;
    (void)length;
    (void)generation;
    return BOOL_TRUE;
}

boolean_en sys_persistent_layout_initialize_with_defaults(
    const u8 factory_user_compat[SYS_PERSISTENT_CONFIG_FACTORY_LENGTH],
    u32 device_address,
    sys_persistent_default_section_writer_fn property_writer,
    sys_persistent_default_section_writer_fn plan_writer,
    boolean_en existing_ota_pending)
{
    (void)factory_user_compat;
    (void)device_address;
    (void)property_writer;
    (void)plan_writer;
    (void)existing_ota_pending;
    return BOOL_TRUE;
}

boolean_en sys_persistent_ota_flag_is_set(void)
{
    return BOOL_FALSE;
}

boolean_en zk_device_config_persistent_defaults(u8 *payload, u16 length)
{
    (void)payload;
    (void)length;
    return BOOL_TRUE;
}

boolean_en zk_work_plan_persistent_defaults(u8 *payload, u16 length)
{
    (void)payload;
    (void)length;
    return BOOL_TRUE;
}

void hw_flash_write_bytes(u32 address, u8 *data, u32 length)
{
    (void)address;
    (void)data;
    (void)length;
}

boolean_en user_flash_check(u32 address, u8 *data, u16 length)
{
    (void)address;
    (void)data;
    (void)length;
    return BOOL_TRUE;
}

void log_u32(u8 index, u32 value)
{
    (void)index;
    (void)value;
}

int dma_printf(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    va_end(args);
    return 0;
}

int main(void)
{
    int failures = 0;
    u16 derated_output = 0U;
    u16 tick;

    memset(&sys_data, 0xA5, sizeof(sys_data));
    sys_data_load();
    failures += expect_true(sys_data.lamp_power == 100U,
                            "boot initializes RAM-only lamp_power to 100 percent");

    Ntctemp.Ntctemp = 850;
    for (tick = 0U; tick < 1000U; ++tick)
    {
        sys_temp_over_protect_timer();
    }
    sys_temp_over_protect_process();
    failures += expect_true(driver_temperarure_warn == 1U &&
                            sys_temp_over_protect_state == SYS_TEMP_OVER_PROTECT_STATE_OVER &&
                            sys_data.lamp_power == 100U,
                            "first overtemperature warning preserves full output before timed derating");

    for (tick = 0U; tick < 1000U; ++tick)
    {
        sys_temp_over_protect_timer();
    }
    sys_temp_over_protect_process();
    failures += expect_true(sys_data.lamp_power == 90U && pwm_reload_calls == 2U,
                            "existing first timed derating step is gradual from 100 to 90 percent");
    failures += expect_true(temp_detect_is_over(&derated_output, 80U) == BOOL_TRUE &&
                            derated_output == 72U,
                            "temperature gate applies the 90 percent RAM derating");

    if (failures != 0)
    {
        fprintf(stderr, "sys_data/temperature V3 failures: %d\n", failures);
        return 1;
    }
    puts("sys_data/temperature V3 tests: PASS");
    return 0;
}
