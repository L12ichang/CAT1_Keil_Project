#include "zk_runtime_stats.h"
#include "common.h"
#include "Portable.h"
#include "sys_data.h"
#include "sys_bl0942.h"
#include "sys_pow_drop_check.h"
#include "net_dim.h"
#include "sys_persistent_storage.h"
#include <string.h>

#define ZK_RUNTIME_SAVE_INTERVAL_MS   (8UL * 60UL * 60UL * 1000UL)

static uint32 zk_boot_run_seconds;
static uint32 zk_boot_light_seconds;
static uint32 zk_total_run_base_seconds;
static uint32 zk_total_light_base_seconds;
static uint32 zk_total_energy_base_001wh;
static uint32 zk_runtime_last_tick;
static uint32 zk_runtime_last_save_tick;
static boolean_en zk_runtime_loaded = BOOL_FALSE;
static boolean_en zk_runtime_powerdown_saved = BOOL_FALSE;

static u32 zk_runtime_total_energy_001wh(void)
{
    return sys_data.ac_EnergyP + total_power_this_time;
}

static void zk_runtime_overlay_current(
    sys_persistent_runtime_data_st *runtime)
{
    if (sys_persistent_runtime_load(runtime, NULL) != BOOL_TRUE)
    {
        memset(runtime, 0, sizeof(*runtime));
    }
    runtime->total_run_time_sec =
        zk_total_run_base_seconds + zk_boot_run_seconds;
    runtime->total_light_time_sec =
        zk_total_light_base_seconds + zk_boot_light_seconds;
    runtime->total_energy_001wh = zk_runtime_total_energy_001wh();
}

static boolean_en zk_runtime_flash_store_current(void)
{
    sys_persistent_runtime_data_st runtime;

    /* Reload before overlaying counters so inhibit/OTA fields are never stale. */
    zk_runtime_overlay_current(&runtime);
    return sys_persistent_runtime_commit(&runtime, NULL);
}

boolean_en zk_runtime_stats_checkpoint_now(void)
{
    uint32 now;

    if (zk_runtime_loaded != BOOL_TRUE)
    {
        zk_runtime_stats_init();
    }
    now = Timer_GetTickCount();
    if (zk_runtime_flash_store_current() != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    zk_runtime_last_save_tick = now;
    return BOOL_TRUE;
}

boolean_en zk_runtime_stats_powerdown_checkpoint(void)
{
    if (zk_runtime_stats_checkpoint_now() != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    zk_runtime_powerdown_saved = BOOL_TRUE;
    return BOOL_TRUE;
}

boolean_en zk_runtime_stats_set_calibration_inhibit(boolean_en active)
{
    sys_persistent_runtime_data_st runtime;

    if (zk_runtime_loaded != BOOL_TRUE)
    {
        zk_runtime_stats_init();
    }
    zk_runtime_overlay_current(&runtime);
    runtime.calibration_inhibit =
        (active == BOOL_TRUE) ? BOOL_TRUE : BOOL_FALSE;
    return sys_persistent_runtime_commit(&runtime, NULL);
}

boolean_en zk_runtime_stats_get_ota_report(
    sys_persistent_ota_report_st *report)
{
    return sys_persistent_runtime_get_ota_report(report);
}

boolean_en zk_runtime_stats_set_ota_report(
    const sys_persistent_ota_report_st *report)
{
    sys_persistent_runtime_data_st runtime;

    if (report == NULL)
    {
        return BOOL_FALSE;
    }
    if (zk_runtime_loaded != BOOL_TRUE)
    {
        zk_runtime_stats_init();
    }
    zk_runtime_overlay_current(&runtime);
    runtime.ota_report_state = report->state;
    memcpy(runtime.ota_id, report->ota_id, sizeof(runtime.ota_id));
    runtime.ota_url_hash = report->url_hash;
    runtime.ota_image_checksum = report->image_checksum;
    runtime.ota_image_size = report->image_size;
    runtime.ota_device_type = report->device_type;
    runtime.ota_retry_count = report->retry_count;
    return sys_persistent_runtime_commit(&runtime, NULL);
}

static void zk_runtime_save_process(uint32 now)
{
    if (power_down_flag != 0U)
    {
        if (zk_runtime_powerdown_saved == BOOL_FALSE)
        {
            if (zk_runtime_flash_store_current() == BOOL_TRUE)
            {
                zk_runtime_last_save_tick = now;
                zk_runtime_powerdown_saved = BOOL_TRUE;
            }
        }
        return;
    }
    zk_runtime_powerdown_saved = BOOL_FALSE;

    if (Timer_PassedDelay(zk_runtime_last_save_tick,
                          ZK_RUNTIME_SAVE_INTERVAL_MS) == BOOL_TRUE &&
        zk_runtime_flash_store_current() == BOOL_TRUE)
    {
        zk_runtime_last_save_tick = now;
    }
}

void zk_runtime_stats_init(void)
{
    sys_persistent_runtime_data_st runtime;

    if (zk_runtime_loaded == BOOL_TRUE)
    {
        return;
    }
    if (sys_persistent_runtime_load(&runtime, NULL) == BOOL_TRUE)
    {
        zk_total_run_base_seconds = runtime.total_run_time_sec;
        zk_total_light_base_seconds = runtime.total_light_time_sec;
        zk_total_energy_base_001wh = runtime.total_energy_001wh;
    }
    else
    {
        zk_total_run_base_seconds = 0U;
        zk_total_light_base_seconds = 0U;
        zk_total_energy_base_001wh = 0U;
    }
    /* Existing telemetry reads this RAM mirror; RUN1 is now its authority. */
    sys_data.ac_EnergyP = zk_total_energy_base_001wh;
    zk_boot_run_seconds = 0U;
    zk_boot_light_seconds = 0U;
    zk_runtime_last_tick = Timer_GetTickCount();
    zk_runtime_last_save_tick = zk_runtime_last_tick;
    zk_runtime_powerdown_saved = BOOL_FALSE;
    zk_runtime_loaded = BOOL_TRUE;
}

void zk_runtime_counter_process(void)
{
    uint32 now;
    uint32 elapsed_ms;
    uint32 elapsed_seconds;

    if (zk_runtime_loaded != BOOL_TRUE)
    {
        zk_runtime_stats_init();
    }
    now = Timer_GetTickCount();
    if (zk_runtime_last_tick == 0U)
    {
        zk_runtime_last_tick = now;
        return;
    }
    elapsed_ms = now - zk_runtime_last_tick;
    if (elapsed_ms < 1000UL)
    {
        zk_runtime_save_process(now);
        return;
    }
    elapsed_seconds = elapsed_ms / 1000UL;
    zk_boot_run_seconds += elapsed_seconds;
    zk_runtime_last_tick += elapsed_seconds * 1000UL;
    if (dim_level > 0U)
    {
        zk_boot_light_seconds += elapsed_seconds;
    }
    zk_runtime_save_process(now);
}

boolean_en zk_runtime_stats_clear(void)
{
    uint32 old_boot_run;
    uint32 old_boot_light;
    uint32 old_total_run;
    uint32 old_total_light;
    uint32 old_total_energy;
    uint32 old_energy_base;
    uint32 old_current_energy;
    uint32 now;

    if (zk_runtime_loaded != BOOL_TRUE)
    {
        zk_runtime_stats_init();
    }
    zk_runtime_counter_process();
    old_boot_run = zk_boot_run_seconds;
    old_boot_light = zk_boot_light_seconds;
    old_total_run = zk_total_run_base_seconds;
    old_total_light = zk_total_light_base_seconds;
    old_total_energy = sys_data.ac_EnergyP;
    old_energy_base = zk_total_energy_base_001wh;
    old_current_energy = total_power_this_time;

    zk_boot_run_seconds = 0U;
    zk_boot_light_seconds = 0U;
    zk_total_run_base_seconds = 0U;
    zk_total_light_base_seconds = 0U;
    zk_total_energy_base_001wh = 0U;
    sys_data.ac_EnergyP = 0U;
    total_power_this_time = 0U;
    if (zk_runtime_flash_store_current() != BOOL_TRUE)
    {
        zk_boot_run_seconds = old_boot_run;
        zk_boot_light_seconds = old_boot_light;
        zk_total_run_base_seconds = old_total_run;
        zk_total_light_base_seconds = old_total_light;
        zk_total_energy_base_001wh = old_energy_base;
        sys_data.ac_EnergyP = old_total_energy;
        total_power_this_time = old_current_energy;
        return BOOL_FALSE;
    }
    now = Timer_GetTickCount();
    zk_runtime_last_tick = now;
    zk_runtime_last_save_tick = now;
    zk_runtime_powerdown_saved = BOOL_FALSE;
    return BOOL_TRUE;
}

uint32 zk_runtime_get_boot_run_seconds(void)
{
    return zk_boot_run_seconds;
}

uint32 zk_runtime_get_boot_light_seconds(void)
{
    return zk_boot_light_seconds;
}

uint32 zk_runtime_get_total_run_seconds(void)
{
    return zk_total_run_base_seconds + zk_boot_run_seconds;
}

uint32 zk_runtime_get_total_light_seconds(void)
{
    return zk_total_light_base_seconds + zk_boot_light_seconds;
}

uint32 zk_runtime_get_boot_energy_001wh(void)
{
    u32 total = zk_runtime_total_energy_001wh();
    return (total >= zk_total_energy_base_001wh) ?
           (total - zk_total_energy_base_001wh) : 0U;
}

uint32 zk_runtime_get_total_energy_001wh(void)
{
    return zk_runtime_total_energy_001wh();
}
