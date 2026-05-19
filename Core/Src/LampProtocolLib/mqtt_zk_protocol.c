#include "mqtt_zk_protocol.h"
#include "Portable.h"
#include "TcpClient.h"
#include "net_dim.h"
#include "ota.h"
#include "NbDriver.h"
#include "crc16_modbus.h"
#include "sys_bl0942.h"
#include "sys_Vo_Io.h"
#include "sys_aip1302.h"
#include "danger_current_check.h"
#include "sys_pow_drop_check.h"
#include "sys_temp_over_protect.h"
#include "factory_user_data.h"
#include "sys_data.h"
#include "hw_flash.h"
#include "ntc.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

extern uint8 IMEI[18];
extern uint8 simCardICCID[22];
extern u8 online;
extern char firm_name_buffer[256];
extern u32 dangeo_out;

static zk_mqtt_config_t zk_mqtt_cfg;
static zk_device_config_t zk_dev_cfg;
static zk_login_state_en zk_login_state = ZK_LOGIN_STATE_IDLE;
static uint32 zk_login_tick = 0;
static uint32 zk_heartbeat_tick = 0;
static uint32 zk_report_tick = 0;
static uint32 zk_time_request_tick = 0;
static uint32 zk_json_message_counter = ZK_JSON_ID_FIRST_REPORT - 1;
static uint16 zk_mqtt_packet_counter = 0;
static u8 zk_last_brightness = 100;
static char zk_tx_buf[ZK_JSON_BUF_SIZE];
static char zk_last_heartbeat_id[8];
static char zk_last_ota_id[8];
static char zk_ota_url[192];
static uint8 static_rx_pool[ZK_CJSON_RX_POOL_SIZE] __attribute__((aligned(8)));
static uint8 static_tx_pool[ZK_CJSON_TX_POOL_SIZE] __attribute__((aligned(8)));
static uint8 *zk_cjson_active_pool = static_rx_pool;
static uint32 zk_cjson_active_pool_size = sizeof(static_rx_pool);
static uint32 zk_cjson_pool_offset = 0;
static boolean_en zk_cjson_hooks_ready = BOOL_FALSE;
static boolean_en zk_cjson_tx_exhausted_logged = BOOL_FALSE;
static boolean_en zk_change_report_pending = BOOL_FALSE;
static uint32 zk_change_report_tick = 0;
static boolean_en zk_patrol_report_pending = BOOL_FALSE;
static boolean_en zk_response_pending = BOOL_FALSE;
static zk_message_header_t zk_response_pending_header;
static int zk_response_pending_err_code = 0;
static boolean_en zk_ota_progress_pending = BOOL_FALSE;
static uint32 zk_ota_progress_value = 0;
static boolean_en zk_ota_error_pending = BOOL_FALSE;
static int zk_ota_error_code = 0;
static u8 zk_send_busy_fail_count = 0;
static boolean_en zk_reboot_pending = BOOL_FALSE;
static uint32 zk_reboot_tick = 0;
static boolean_en zk_control_restore_pending = BOOL_FALSE;
static uint32 zk_control_restore_tick = 0;
static uint32 zk_control_restore_delay_ms = 0;
static int zk_control_restore_brightness = 0;

/* 运行时间统计：非阻塞、低频更新（秒级），周期低频写Flash */
static uint32 zk_boot_run_seconds;              /* 本次上电累计运行秒数 */
static uint32 zk_boot_light_seconds;            /* 本次上电累计亮灯秒数（仅dim_level>0时累加） */
static uint32 zk_total_run_base_seconds;        /* 历史总运行秒数 */
static uint32 zk_total_light_base_seconds;      /* 历史总亮灯秒数 */
static uint32 zk_runtime_last_tick;             /* 上次统计tick */
static uint32 zk_runtime_flash_seq;
static uint32 zk_runtime_last_save_tick;
static boolean_en zk_runtime_loaded = BOOL_FALSE;
static boolean_en zk_runtime_powerdown_saved = BOOL_FALSE;
static uint32 zk_signal_query_tick;
static char zk_signal_qeng_cmd[] = "AT+QENG=\"servingcell\"\r\n";
static char zk_signal_qeng_resp[] = "OK";

#define ZK_CNCTRL_SUPPORTED_CNS 1
#define ZK_CNCTRL_LAST_MAX_SEC  (24UL * 60UL * 60UL)
#define ZK_ALARM_POWER_DOWN_INDEX 7U
#define ZK_SEND_BUSY_CLEAR_THRESHOLD 3U
#define ZK_CHANGE_REPORT_SETTLE_MS 3000UL
#define ZK_PROPERTY_FLASH_MAGIC 0x5A4B5052UL
#define ZK_PROPERTY_FLASH_VERSION 1U
#define ZK_PROPERTY_FLASH_MAIN_ADDR (DATAROM_STARTADDR + FLASH_PAGE_SIZE)
#define ZK_PROPERTY_FLASH_BACKUP_ADDR (BAKDATAROM_STARTADDR + FLASH_PAGE_SIZE)
#define ZK_RUNTIME_FLASH_MAGIC 0x5A4B5254UL
#define ZK_RUNTIME_FLASH_VERSION 1U
#define ZK_RUNTIME_FLASH_OFFSET 0x200UL
#define ZK_RUNTIME_FLASH_MAIN_ADDR (ZK_PROPERTY_FLASH_MAIN_ADDR + ZK_RUNTIME_FLASH_OFFSET)
#define ZK_RUNTIME_FLASH_BACKUP_ADDR (ZK_PROPERTY_FLASH_BACKUP_ADDR + ZK_RUNTIME_FLASH_OFFSET)
#define ZK_RUNTIME_SAVE_INTERVAL_MS (6UL * 60UL * 60UL * 1000UL)
#define ZK_SIGNAL_QUERY_INTERVAL_MS (4UL * 60UL * 1000UL)
#define ZK_FLASH_SAVE_ERROR 99

typedef struct
{
    u32 magic;
    u16 version;
    u16 size;
    u32 seq;
    s32 lng;
    s32 lat;
    s32 zone;
    s32 cns;
    s32 dimTp;
    s32 polar;
    s32 dlmt;
    s32 ulmt;
    s32 rti;
    s32 rtPwr;
    s32 di;
    s32 sBri;
    s32 sBriTm;
    char svrIp[32];
    s32 svrPort;
    s32 uPeriod;
    s32 hPeriod;
    s32 tPeriod;
    u32 checksum;
} zk_property_flash_record_t;

typedef struct
{
    u32 magic;
    u16 version;
    u16 size;
    u32 seq;
    u32 total_run_seconds;
    u32 total_light_seconds;
    u32 checksum;
} zk_runtime_flash_record_t;

typedef struct
{
    uint16 alm_id;
    u8 pending;
    u8 pending_status;
    u8 reported;
    u8 one_shot;
    uint32 value;
    uint32 threshold;
} zk_alarm_state_t;

static zk_alarm_state_t zk_alarm_states[] =
{
    {ZK_ALARM_OVER_VOLTAGE, 0, 0, 0, 0, 0, 3200},
    {ZK_ALARM_UNDER_VOLTAGE, 0, 0, 0, 0, 0, 800},
    {ZK_ALARM_OVER_CURRENT, 0, 0, 0, 0, 0, 0},
    {ZK_ALARM_UNDER_CURRENT, 0, 0, 0, 0, 0, 0},
    {ZK_ALARM_LIGHT_ON_FAIL, 0, 0, 0, 0, 0, 0},
    {ZK_ALARM_LEAK_CURRENT, 0, 0, 0, 0, 0, 30},
    {ZK_ALARM_DEVICE_FAULT, 0, 0, 0, 0, 0, 0},
    {ZK_ALARM_POWER_DOWN, 0, 0, 0, 1, 0, 70},
};
#define ZK_ALARM_STATE_COUNT (sizeof(zk_alarm_states) / sizeof(zk_alarm_states[0]))

static u32 zk_property_flash_seq = 0;

static void zk_apply_brightness(int brightness);
static void zk_alarm_reset_states(void);
static boolean_en zk_alarm_process(void);

static void *ZK_Cjson_Malloc(size_t size)
{
    void *ptr;

    size = (size + 7U) & ~((size_t)7U);
    if (zk_cjson_pool_offset + size > zk_cjson_active_pool_size)
    {
        if (zk_cjson_active_pool == static_tx_pool &&
            zk_cjson_tx_exhausted_logged == BOOL_FALSE)
        {
            printf("TX Pool Exhausted\r\n");
            zk_cjson_tx_exhausted_logged = BOOL_TRUE;
        }
        return NULL;
    }
    ptr = &zk_cjson_active_pool[zk_cjson_pool_offset];
    zk_cjson_pool_offset += (uint32)size;
    return ptr;
}

static void ZK_Cjson_Free(void *ptr)
{
    /*
     * cJSON nodes are served from a linear RX/TX static pool. Individual free
     * is intentionally a no-op; the selected pool is reclaimed in one shot by
     * zk_cjson_init_rx() or zk_cjson_init_tx() before the next JSON operation.
     */
    (void)ptr;
}

static void zk_cjson_init_hooks(void)
{
    cJSON_Hooks hooks;

    if (zk_cjson_hooks_ready == BOOL_TRUE)
    {
        return;
    }
    hooks.malloc_fn = ZK_Cjson_Malloc;
    hooks.free_fn = ZK_Cjson_Free;
    cJSON_InitHooks(&hooks);
    zk_cjson_hooks_ready = BOOL_TRUE;
}

static void zk_cjson_init_rx(void)
{
    zk_cjson_init_hooks();
    zk_cjson_active_pool = static_rx_pool;
    zk_cjson_active_pool_size = sizeof(static_rx_pool);
    zk_cjson_pool_offset = 0;
}

static void zk_cjson_init_tx(void)
{
    zk_cjson_init_hooks();
    zk_cjson_active_pool = static_tx_pool;
    zk_cjson_active_pool_size = sizeof(static_tx_pool);
    zk_cjson_pool_offset = 0;
    zk_cjson_tx_exhausted_logged = BOOL_FALSE;
}

void zk_cjson_prepare_parse(void)
{
    zk_cjson_init_rx();
}

static void zk_cjson_prepare_tx(void)
{
    zk_cjson_init_tx();
}

static void zk_cjson_log_tx_pool_exhausted(const char *context)
{
    if (zk_cjson_tx_exhausted_logged == BOOL_TRUE)
    {
        return;
    }
    printf("TX Pool Exhausted: %s\r\n", (context != NULL) ? context : "cJSON");
    zk_cjson_tx_exhausted_logged = BOOL_TRUE;
}

static cJSON *zk_cjson_create_tx_object(const char *context)
{
    cJSON *object;

    object = cJSON_CreateObject();
    if (object == NULL)
    {
        zk_cjson_log_tx_pool_exhausted(context);
    }
    return object;
}

static cJSON *zk_cjson_create_tx_array(const char *context)
{
    cJSON *array;

    array = cJSON_CreateArray();
    if (array == NULL)
    {
        zk_cjson_log_tx_pool_exhausted(context);
    }
    return array;
}

static void zk_copy_digits(char *dst, int dst_size, const char *src, int max_digits)
{
    int i;
    int out;

    out = 0;
    if (dst_size <= 0)
    {
        return;
    }
    for (i = 0; src[i] != '\0' && out < dst_size - 1 && out < max_digits; ++i)
    {
        if (src[i] >= '0' && src[i] <= '9')
        {
            dst[out++] = src[i];
        }
    }
    dst[out] = '\0';
}

static boolean_en zk_imei_valid(const char *imei)
{
    int i;

    for (i = 0; i < 15; ++i)
    {
        if (imei[i] < '0' || imei[i] > '9')
        {
            return BOOL_FALSE;
        }
    }
    return (imei[15] == '\0') ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en zk_load_imei(char *imei, int imei_size)
{
    zk_copy_digits(imei, imei_size, (const char *)IMEI, 15);
    if (zk_imei_valid(imei) == BOOL_FALSE)
    {
        if (imei_size > 0)
        {
            imei[0] = '\0';
        }
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}
//获取rtc时间
static void zk_get_time_text(char *buf, int buf_size)
{
    if (apprtc_RtcTime.ready == BOOL_TRUE &&
        apprtc_RtcTime.year >= 2020 &&
        apprtc_RtcTime.mon >= 1 &&
        apprtc_RtcTime.mon <= 12 &&
        apprtc_RtcTime.day >= 1 &&
        apprtc_RtcTime.day <= 31)
    {
        snprintf(buf, buf_size, "%04d-%02d-%02d %02d:%02d:%02d",
                 apprtc_RtcTime.year,
                 apprtc_RtcTime.mon,
                 apprtc_RtcTime.day,
                 apprtc_RtcTime.hour,
                 apprtc_RtcTime.min,
                 apprtc_RtcTime.sec);
    }
    else
    {
        strncpy(buf, "2026-04-02 08:30:30", buf_size - 1);
        buf[buf_size - 1] = '\0';
    }
}

static int zk_is_leap_year(int year)
{
    if ((year % 400) == 0)
    {
        return 1;
    }
    if ((year % 100) == 0)
    {
        return 0;
    }
    return ((year % 4) == 0) ? 1 : 0;
}

static long zk_days_before_month(int year, int month)
{
    static const int days[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    long value;

    value = days[month - 1];
    if (month > 2 && zk_is_leap_year(year))
    {
        ++value;
    }
    return value;
}

static long zk_rtc_to_unix_seconds(const RtcTime_t *rtc)
{
    int year;
    long days;

    if (rtc == NULL)
    {
        return 0;
    }

    days = 0;
    for (year = 1970; year < (int)rtc->year; ++year)
    {
        days += zk_is_leap_year(year) ? 366L : 365L;
    }
    days += zk_days_before_month(rtc->year, rtc->mon);
    days += (long)rtc->day - 1L;
    return (((days * 24L) + rtc->hour) * 60L + rtc->min) * 60L + rtc->sec;
}

static boolean_en zk_parse_rtc_text(const char *text, RtcTime_t *rtc)
{
    unsigned int y;
    unsigned int mo;
    unsigned int d;
    unsigned int h;
    unsigned int mi;
    unsigned int s;

    if (text == NULL || rtc == NULL)
    {
        return BOOL_FALSE;
    }

    if (sscanf(text, "%04u-%02u-%02u %02u:%02u:%02u", &y, &mo, &d, &h, &mi, &s) != 6)
    {
        return BOOL_FALSE;
    }
    if (mo < 1U || mo > 12U || d < 1U || d > 31U || h > 23U || mi > 59U || s > 59U)
    {
        return BOOL_FALSE;
    }

    memset(rtc, 0, sizeof(*rtc));
    rtc->year = (u16)y;
    rtc->mon = (u8)mo;
    rtc->day = (u8)d;
    rtc->hour = (u8)h;
    rtc->min = (u8)mi;
    rtc->sec = (u8)s;
    rtc->week = (u8)(GetWeek(rtc->year, rtc->mon, rtc->day) + 1U);
    rtc->ready = BOOL_TRUE;
    return BOOL_TRUE;
}

static const char *zk_json_get_rtc_time_text(cJSON *node)
{
    cJSON *time_node;

    if (node == NULL)
    {
        return NULL;
    }
    if (cJSON_IsString(node) && node->valuestring != NULL)
    {
        return node->valuestring;
    }
    if (cJSON_IsObject(node))
    {
        time_node = cJSON_GetObjectItem(node, "time");
        if (time_node != NULL && cJSON_IsString(time_node) && time_node->valuestring != NULL)
        {
            return time_node->valuestring;
        }
    }
    return NULL;
}

static void zk_set_local_rtc(const RtcTime_t *rtc)
{
    if (rtc == NULL)
    {
        return;
    }
    apprtc_RtcTime = *rtc;
    SetTime();
}

static boolean_en zk_apply_server_time_text(const char *time_text)
{
    RtcTime_t server_time;
    long server_seconds;
    long local_seconds;
    long diff_seconds;

    if (time_text == NULL || time_text[0] == '\0')
    {
        return BOOL_FALSE;
    }
    if (zk_parse_rtc_text(time_text, &server_time) == BOOL_FALSE)
    {
        return BOOL_FALSE;
    }
    if (apprtc_RtcTime.ready != BOOL_TRUE)
    {
        zk_set_local_rtc(&server_time);
        return BOOL_TRUE;
    }

    server_seconds = zk_rtc_to_unix_seconds(&server_time);
    local_seconds = zk_rtc_to_unix_seconds(&apprtc_RtcTime);
    diff_seconds = server_seconds - local_seconds;
    if (diff_seconds < 0)
    {
        diff_seconds = -diff_seconds;
    }
    if (diff_seconds > 30)
    {
        zk_set_local_rtc(&server_time);
    }
    return BOOL_TRUE;
}

void zk_apply_server_time_from_header(const zk_message_header_t *header)
{
    if (header == NULL)
    {
        return;
    }
    (void)zk_apply_server_time_text(header->tm);
}

static uint32 zk_get_effective_period_sec(int configured, uint32 default_value)
{
    return (configured > 0) ? (uint32)configured : default_value;
}

static int zk_get_run_status_code(void)
{
    return (dim_level > 0U) ? 31 : 32;
}

static int zk_get_current_brightness(void)
{
    if (dim_level > 100U)
    {
        return 100;
    }
    return (int)dim_level;
}

const char *zk_get_ota_url(void)
{
    return zk_ota_url;
}

static void zk_ota_set_url(const char *url)
{
    if (url == NULL)
    {
        zk_ota_url[0] = '\0';
        return;
    }
    strncpy(zk_ota_url, url, sizeof(zk_ota_url) - 1);
    zk_ota_url[sizeof(zk_ota_url) - 1] = '\0';
}

static boolean_en zk_ota_extract_filename(const char *url, char *file_name, int file_name_size)
{
    const char *last_slash;
    const char *query;
    int len;

    if (url == NULL || file_name == NULL || file_name_size <= 1)
    {
        return BOOL_FALSE;
    }
    last_slash = strrchr(url, '/');
    if (last_slash == NULL || last_slash[1] == '\0')
    {
        return BOOL_FALSE;
    }
    query = strchr(last_slash + 1, '?');
    len = (query != NULL) ? (int)(query - (last_slash + 1)) : (int)strlen(last_slash + 1);
    if (len <= 0 || len >= file_name_size)
    {
        return BOOL_FALSE;
    }
    memcpy(file_name, last_slash + 1, (size_t)len);
    file_name[len] = '\0';
    return BOOL_TRUE;
}

static boolean_en zk_ota_is_busy(void)
{
    if (OTA_ENABLE_state != 0U)
    {
        return BOOL_TRUE;
    }
    if (OTA_ENABLE_IS_SET() == BOOL_TRUE)
    {
        return BOOL_TRUE;
    }
    if (MCU_OTA_state != MCU_OTA_STATE_IDLE)
    {
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

void zk_mqtt_generate_password(const char *imei, char *password)
{
    uint16 p0;
    uint16 p1;
    uint16 p2;

    if (password == NULL)
    {
        return;
    }
    if (imei == NULL || zk_imei_valid(imei) == BOOL_FALSE)
    {
        password[0] = '\0';
        return;
    }

    p0 = (uint16)(~crc16_modbus_get((unsigned char *)imei, 5));
    p1 = (uint16)(~crc16_modbus_get((unsigned char *)(imei + 5), 5));
    p2 = (uint16)(~crc16_modbus_get((unsigned char *)(imei + 10), 5));
    snprintf(password, 13, "%04X%04X%04X", p2, p0, p1);
}

static u16 zk_property_flash_checksum(zk_property_flash_record_t *record)
{
    return crc16_modbus_get((unsigned char *)record,
                            sizeof(*record) - sizeof(record->checksum));
}

static void zk_property_record_from_config(zk_property_flash_record_t *record,
                                           const zk_device_config_t *config,
                                           u32 seq)
{
    memset(record, 0, sizeof(*record));
    record->magic = ZK_PROPERTY_FLASH_MAGIC;
    record->version = ZK_PROPERTY_FLASH_VERSION;
    record->size = (u16)sizeof(*record);
    record->seq = seq;
    record->lng = (s32)config->lng;
    record->lat = (s32)config->lat;
    record->zone = config->zone;
    record->cns = config->cns;
    record->dimTp = config->dimTp;
    record->polar = config->polar;
    record->dlmt = config->dlmt;
    record->ulmt = config->ulmt;
    record->rti = config->rti;
    record->rtPwr = config->rtPwr;
    record->di = config->di;
    record->sBri = config->sBri;
    record->sBriTm = config->sBriTm;
    strncpy(record->svrIp, config->svrIp, sizeof(record->svrIp) - 1);
    record->svrPort = config->svrPort;
    record->uPeriod = config->uPeriod;
    record->hPeriod = config->hPeriod;
    record->tPeriod = config->tPeriod;
    record->checksum = zk_property_flash_checksum(record);
}

static boolean_en zk_property_record_valid(zk_property_flash_record_t *record)
{
    if (record->magic != ZK_PROPERTY_FLASH_MAGIC ||
        record->version != ZK_PROPERTY_FLASH_VERSION ||
        record->size != sizeof(*record))
    {
        return BOOL_FALSE;
    }
    return ((u16)record->checksum == zk_property_flash_checksum(record)) ? BOOL_TRUE : BOOL_FALSE;
}

static void zk_property_record_to_config(zk_device_config_t *config,
                                         zk_property_flash_record_t *record)
{
    config->lng = record->lng;
    config->lat = record->lat;
    config->zone = record->zone;
    config->cns = record->cns;
    config->dimTp = record->dimTp;
    config->polar = record->polar;
    config->dlmt = record->dlmt;
    config->ulmt = record->ulmt;
    config->rti = record->rti;
    config->rtPwr = record->rtPwr;
    config->di = record->di;
    config->sBri = record->sBri;
    config->sBriTm = record->sBriTm;
    strncpy(config->svrIp, record->svrIp, sizeof(config->svrIp) - 1);
    config->svrIp[sizeof(config->svrIp) - 1] = '\0';
    config->svrPort = record->svrPort;
    config->uPeriod = record->uPeriod;
    config->hPeriod = record->hPeriod;
    config->tPeriod = record->tPeriod;
}

static boolean_en zk_property_flash_read_record(u32 addr,
                                                zk_property_flash_record_t *record)
{
    hw_flash_read_bytes(addr, (u8 *)record, sizeof(*record));
    return zk_property_record_valid(record);
}

static boolean_en zk_property_flash_write_record(u32 addr,
                                                 zk_property_flash_record_t *record)
{
    hw_flash_write_bytes(addr, (u8 *)record, sizeof(*record));
    return user_flash_check(addr, (u8 *)record, sizeof(*record));
}

static boolean_en zk_property_flash_load(zk_device_config_t *config)
{
    zk_property_flash_record_t main_record;
    zk_property_flash_record_t backup_record;
    zk_property_flash_record_t *selected;
    boolean_en main_ok;
    boolean_en backup_ok;

    main_ok = zk_property_flash_read_record(ZK_PROPERTY_FLASH_MAIN_ADDR, &main_record);
    backup_ok = zk_property_flash_read_record(ZK_PROPERTY_FLASH_BACKUP_ADDR, &backup_record);

    selected = NULL;
    if (main_ok == BOOL_TRUE && backup_ok == BOOL_TRUE)
    {
        selected = (main_record.seq >= backup_record.seq) ? &main_record : &backup_record;
    }
    else if (main_ok == BOOL_TRUE)
    {
        selected = &main_record;
    }
    else if (backup_ok == BOOL_TRUE)
    {
        selected = &backup_record;
    }

    if (selected == NULL)
    {
        zk_property_flash_seq = 0;
        return BOOL_FALSE;
    }

    zk_property_record_to_config(config, selected);
    zk_property_flash_seq = selected->seq;
    return BOOL_TRUE;
}

static boolean_en zk_property_flash_store_config(const zk_device_config_t *config)
{
    zk_property_flash_record_t record;
    u32 next_seq;
    boolean_en main_ok;
    boolean_en backup_ok;

    next_seq = zk_property_flash_seq + 1U;
    if (next_seq == 0U)
    {
        next_seq = 1U;
    }

    zk_property_record_from_config(&record, config, next_seq);
    main_ok = zk_property_flash_write_record(ZK_PROPERTY_FLASH_MAIN_ADDR, &record);
    backup_ok = zk_property_flash_write_record(ZK_PROPERTY_FLASH_BACKUP_ADDR, &record);
    if (main_ok == BOOL_TRUE || backup_ok == BOOL_TRUE)
    {
        zk_property_flash_seq = next_seq;
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static u16 zk_runtime_flash_checksum(zk_runtime_flash_record_t *record)
{
    return crc16_modbus_get((unsigned char *)record,
                            sizeof(*record) - sizeof(record->checksum));
}

static boolean_en zk_runtime_record_valid(zk_runtime_flash_record_t *record)
{
    if (record->magic != ZK_RUNTIME_FLASH_MAGIC ||
        record->version != ZK_RUNTIME_FLASH_VERSION ||
        record->size != sizeof(*record))
    {
        return BOOL_FALSE;
    }
    return ((u16)record->checksum == zk_runtime_flash_checksum(record)) ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en zk_runtime_flash_read_record(u32 addr,
                                               zk_runtime_flash_record_t *record)
{
    hw_flash_read_bytes(addr, (u8 *)record, sizeof(*record));
    return zk_runtime_record_valid(record);
}

static void zk_runtime_record_from_current(zk_runtime_flash_record_t *record,
                                           u32 seq)
{
    memset(record, 0, sizeof(*record));
    record->magic = ZK_RUNTIME_FLASH_MAGIC;
    record->version = ZK_RUNTIME_FLASH_VERSION;
    record->size = (u16)sizeof(*record);
    record->seq = seq;
    record->total_run_seconds = zk_total_run_base_seconds + zk_boot_run_seconds;
    record->total_light_seconds = zk_total_light_base_seconds + zk_boot_light_seconds;
    record->checksum = zk_runtime_flash_checksum(record);
}

static boolean_en zk_runtime_flash_write_record(u32 addr,
                                                zk_runtime_flash_record_t *record)
{
    hw_flash_write_bytes(addr, (u8 *)record, sizeof(*record));
    return user_flash_check(addr, (u8 *)record, sizeof(*record));
}

static boolean_en zk_runtime_flash_store_current(void)
{
    zk_runtime_flash_record_t record;
    u32 next_seq;
    boolean_en main_ok;
    boolean_en backup_ok;

    next_seq = zk_runtime_flash_seq + 1U;
    if (next_seq == 0U)
    {
        next_seq = 1U;
    }

    zk_runtime_record_from_current(&record, next_seq);
    main_ok = zk_runtime_flash_write_record(ZK_RUNTIME_FLASH_MAIN_ADDR, &record);
    backup_ok = zk_runtime_flash_write_record(ZK_RUNTIME_FLASH_BACKUP_ADDR, &record);
    if (main_ok == BOOL_TRUE || backup_ok == BOOL_TRUE)
    {
        zk_runtime_flash_seq = next_seq;
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static void zk_runtime_save_process(uint32 now)
{
    if (Timer_PassedDelay(zk_runtime_last_save_tick, ZK_RUNTIME_SAVE_INTERVAL_MS) == BOOL_TRUE)
    {
        (void)zk_runtime_flash_store_current();
        zk_runtime_last_save_tick = now;
    }

    if (power_down_flag != 0)
    {
        if (zk_runtime_powerdown_saved == BOOL_FALSE)
        {
            (void)zk_runtime_flash_store_current();
            zk_runtime_last_save_tick = now;
            zk_runtime_powerdown_saved = BOOL_TRUE;
        }
    }
    else
    {
        zk_runtime_powerdown_saved = BOOL_FALSE;
    }
}

void zk_runtime_stats_init(void)
{
    zk_runtime_flash_record_t main_record;
    zk_runtime_flash_record_t backup_record;
    zk_runtime_flash_record_t *selected;
    boolean_en main_ok;
    boolean_en backup_ok;

    if (zk_runtime_loaded == BOOL_TRUE)
    {
        return;
    }

    main_ok = zk_runtime_flash_read_record(ZK_RUNTIME_FLASH_MAIN_ADDR, &main_record);
    backup_ok = zk_runtime_flash_read_record(ZK_RUNTIME_FLASH_BACKUP_ADDR, &backup_record);

    selected = NULL;
    if (main_ok == BOOL_TRUE && backup_ok == BOOL_TRUE)
    {
        selected = (main_record.seq >= backup_record.seq) ? &main_record : &backup_record;
    }
    else if (main_ok == BOOL_TRUE)
    {
        selected = &main_record;
    }
    else if (backup_ok == BOOL_TRUE)
    {
        selected = &backup_record;
    }

    if (selected != NULL)
    {
        zk_runtime_flash_seq = selected->seq;
        zk_total_run_base_seconds = selected->total_run_seconds;
        zk_total_light_base_seconds = selected->total_light_seconds;
    }
    else
    {
        zk_runtime_flash_seq = 0;
        zk_total_run_base_seconds = 0;
        zk_total_light_base_seconds = 0;
    }

    zk_boot_run_seconds = 0;
    zk_boot_light_seconds = 0;
    zk_runtime_last_tick = Timer_GetTickCount();
    zk_runtime_last_save_tick = zk_runtime_last_tick;
    zk_runtime_powerdown_saved = BOOL_FALSE;
    zk_runtime_loaded = BOOL_TRUE;
}

static void zk_device_config_refresh_iccid_field(zk_device_config_t *config)
{
    if (config == NULL)
    {
        return;
    }
    zk_copy_digits(config->iccid, sizeof(config->iccid), (const char *)simCardICCID, 20);
    if (strlen(config->iccid) == 0)
    {
        strcpy(config->iccid, NB_ICCID_DEFAULT);
    }
}

static void zk_device_config_set_defaults(zk_device_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->protId = 100;
    strcpy(config->clas, "MS-SLC-01");
    strcpy(config->prottp, "iX7-075SC028-4G");
    config->hver = 100;
    config->sver = APP_VERSION;
    strcpy(config->mver, "BC28GJAR01A01");
    zk_device_config_refresh_iccid_field(config);
    config->lng = 120000000;
    config->lat = 30000000;
    config->zone = 8;
    config->cns = 1;
    config->dimTp = 0;
    config->polar = 0;
    config->dlmt = 1000;
    config->ulmt = 9000;
    config->rti = 0;
    config->rtPwr = 200;
    config->di = 1;
    config->sBri = 80;
    config->sBriTm = 5;
    strncpy(config->svrIp, ZK_MQTT_SERVER_IP, sizeof(config->svrIp) - 1);
    config->svrPort = ZK_MQTT_SERVER_PORT;
    config->uPeriod = ZK_UPLOAD_INTERVAL_SEC;
    config->hPeriod = ZK_HEARTBEAT_INTERVAL_SEC;
    config->tPeriod = ZK_TIME_REQUEST_INTERVAL_SEC;
    config->commMain = 1;
    config->commSub = 1;
    config->commSAuto = 0;
    config->spreadOffset = 6000;
    config->spreadWindow = 60;
    config->spreadInterval = 10;
}

void zk_device_config_init(void)
{
    zk_device_config_set_defaults(&zk_dev_cfg);
    (void)zk_property_flash_load(&zk_dev_cfg);
    zk_device_config_refresh_iccid();
}

void zk_device_config_refresh_iccid(void)
{
    zk_device_config_refresh_iccid_field(&zk_dev_cfg);
}

boolean_en zk_mqtt_init(void)
{
    char imei[16];

    zk_cjson_init_hooks();
    memset(imei, 0, sizeof(imei));
    if (zk_load_imei(imei, sizeof(imei)) == BOOL_FALSE)
    {
        memset(&zk_mqtt_cfg, 0, sizeof(zk_mqtt_cfg));
        return BOOL_FALSE;
    }

    memset(&zk_mqtt_cfg, 0, sizeof(zk_mqtt_cfg));
    strcpy(zk_mqtt_cfg.imei, imei);
    strcpy(zk_mqtt_cfg.client_id, imei);
    strcpy(zk_mqtt_cfg.username, imei);
    zk_mqtt_generate_password(imei, zk_mqtt_cfg.password);
    snprintf(zk_mqtt_cfg.sub_topic, sizeof(zk_mqtt_cfg.sub_topic), "%s/%s/plt2dev", ZK_TOPIC_PREFIX, imei);
    snprintf(zk_mqtt_cfg.pub_topic, sizeof(zk_mqtt_cfg.pub_topic), "%s/%s/dev2plt", ZK_TOPIC_PREFIX, imei);
    snprintf(zk_mqtt_cfg.will_topic, sizeof(zk_mqtt_cfg.will_topic), "%s/%s/offline", ZK_TOPIC_PREFIX, imei);
    snprintf(zk_mqtt_cfg.sub_upgrade_topic, sizeof(zk_mqtt_cfg.sub_upgrade_topic), "%s/%s/pcp2dev", ZK_TOPIC_PREFIX, imei);
    snprintf(zk_mqtt_cfg.pub_upgrade_topic, sizeof(zk_mqtt_cfg.pub_upgrade_topic), "%s/%s/dev2pcp", ZK_TOPIC_PREFIX, imei);
    zk_device_config_init();
    return BOOL_TRUE;
}

void zk_mqtt_reset_session(void)
{
    zk_login_state = ZK_LOGIN_STATE_IDLE;
    zk_login_tick = 0;
    zk_heartbeat_tick = 0;
    zk_report_tick = 0;
    zk_time_request_tick = 0;
    zk_last_heartbeat_id[0] = '\0';
    zk_last_ota_id[0] = '\0';
    zk_change_report_pending = BOOL_FALSE;
    zk_change_report_tick = 0;
    zk_patrol_report_pending = BOOL_FALSE;
    zk_response_pending = BOOL_FALSE;
    zk_ota_progress_pending = BOOL_FALSE;
    zk_ota_error_pending = BOOL_FALSE;
    zk_send_busy_fail_count = 0;
    zk_reboot_pending = BOOL_FALSE;
    zk_alarm_reset_states();
}

const zk_mqtt_config_t *zk_mqtt_get_config(void)
{
    if (zk_mqtt_cfg.imei[0] == '\0')
    {
        if (zk_mqtt_init() == BOOL_FALSE)
        {
            return NULL;
        }
    }
    return &zk_mqtt_cfg;
}

const char *zk_mqtt_get_pub_topic(void)
{
    const zk_mqtt_config_t *cfg;

    cfg = zk_mqtt_get_config();
    return (cfg != NULL) ? cfg->pub_topic : NULL;
}

const char *zk_mqtt_get_sub_topic(void)
{
    const zk_mqtt_config_t *cfg;

    cfg = zk_mqtt_get_config();
    return (cfg != NULL) ? cfg->sub_topic : NULL;
}

const char *zk_mqtt_get_upgrade_sub_topic(void)
{
    const zk_mqtt_config_t *cfg;

    cfg = zk_mqtt_get_config();
    return (cfg != NULL) ? cfg->sub_upgrade_topic : NULL;
}

uint32 zk_mqtt_next_json_id(void)
{
    if (zk_json_message_counter < ZK_JSON_ID_FIRST_REPORT ||
        zk_json_message_counter >= ZK_JSON_ID_MAX)
    {
        zk_json_message_counter = ZK_JSON_ID_FIRST_REPORT;
    }
    else
    {
        ++zk_json_message_counter;
    }
    return zk_json_message_counter;
}

uint16 zk_mqtt_next_packet_id(void)
{
    ++zk_mqtt_packet_counter;
    if (zk_mqtt_packet_counter == 0)
    {
        zk_mqtt_packet_counter = 1;
    }
    return zk_mqtt_packet_counter;
}

int zk_build_qmt_open_cmd(char *buf, int buf_size)
{
    return snprintf(buf, buf_size, "AT+QMTOPEN=%d,\"%s\",%d\r\n",
                    ZK_MQTT_CLIENT_IDX,
                    zk_dev_cfg.svrIp[0] != '\0' ? zk_dev_cfg.svrIp : ZK_MQTT_SERVER_IP,
                    zk_dev_cfg.svrPort > 0 ? zk_dev_cfg.svrPort : ZK_MQTT_SERVER_PORT);
}

int zk_build_qmt_conn_cmd(char *buf, int buf_size)
{
    const zk_mqtt_config_t *cfg;

    cfg = zk_mqtt_get_config();
    if (cfg == NULL)
    {
        return -1;
    }
    return snprintf(buf, buf_size, "AT+QMTCONN=%d,\"%s\",\"%s\",\"%s\"\r\n",
                    ZK_MQTT_CLIENT_IDX,
                    cfg->client_id,
                    cfg->username,
                    cfg->password);
}

int zk_build_qmt_sub_cmd(char *buf, int buf_size)
{
    const zk_mqtt_config_t *cfg;

    cfg = zk_mqtt_get_config();
    if (cfg == NULL)
    {
        return -1;
    }
    return snprintf(buf, buf_size, "AT+QMTSUB=%d,1,\"%s\",%d\r\n",
                    ZK_MQTT_CLIENT_IDX,
                    cfg->sub_topic,
                    ZK_MQTT_SUB_QOS);
}

int zk_build_qmt_sub_upgrade_cmd(char *buf, int buf_size)
{
    const zk_mqtt_config_t *cfg;

    cfg = zk_mqtt_get_config();
    if (cfg == NULL)
    {
        return -1;
    }
    return snprintf(buf, buf_size, "AT+QMTSUB=%d,2,\"%s\",%d\r\n",
                    ZK_MQTT_CLIENT_IDX,
                    cfg->sub_upgrade_topic,
                    ZK_MQTT_SUB_QOS);
}

static cJSON *zk_create_root_from_header(const zk_message_header_t *header, int with_er, int er_code)
{
    cJSON *root;
    char tm[20];

    if (header == NULL)
    {
        return NULL;
    }

    zk_cjson_prepare_tx();
    root = zk_cjson_create_tx_object("root");
    if (root == NULL)
    {
        return NULL;
    }

    zk_get_time_text(tm, sizeof(tm));
    cJSON_AddStringToObject(root, "SN", header->sn);
    cJSON_AddStringToObject(root, "TM", tm);
    cJSON_AddStringToObject(root, "SV", header->sv);
    cJSON_AddStringToObject(root, "ID", header->id);
    cJSON_AddStringToObject(root, "CT", header->ct);
    if (with_er)
    {
        cJSON_AddNumberToObject(root, "ER", er_code);
    }
    return root;
}

static void zk_note_send_payload_result(uint8 result)
{
    if (result == NB_ERROR_NONE)
    {
        zk_send_busy_fail_count = 0;
        return;
    }

    if (zk_send_busy_fail_count < ZK_SEND_BUSY_CLEAR_THRESHOLD)
    {
        ++zk_send_busy_fail_count;
    }
    if (zk_send_busy_fail_count >= ZK_SEND_BUSY_CLEAR_THRESHOLD)
    {
        pubsend_state_set_idle();
        zk_send_busy_fail_count = 0;
        printf("ZK MQTT publish slot busy cleared\r\n");
    }
}

static int zk_send_payload(const char *payload, uint16 length, const char *topic)
{
    uint8 result;

    if (payload == NULL || length == 0)
    {
        return -1;
    }

    if (topic == NULL)
    {
        result = nbSendTcpData((uint8 *)payload, length);
    }
    else
    {
        result = g4Send_MQTT_Data((char *)topic, (char *)payload);
    }
    zk_note_send_payload_result(result);
    return (result == NB_ERROR_NONE) ? 0 : -1;
}

static int zk_send_json_root(cJSON *root, const char *topic)
{
    int len;

    if (root == NULL)
    {
        return -1;
    }
    memset(zk_tx_buf, 0, sizeof(zk_tx_buf));
    if (cJSON_PrintPreallocated(root, zk_tx_buf, (int)sizeof(zk_tx_buf), 0) == 0)
    {
        return -1;
    }

    len = (int)strlen(zk_tx_buf);
    if (len <= 0)
    {
        return -1;
    }

    return zk_send_payload(zk_tx_buf, (uint16)len, topic);
}

static void zk_schedule_simple_response(const zk_message_header_t *request, int err_code)
{
    if (request == NULL)
    {
        return;
    }
    memcpy(&zk_response_pending_header, request, sizeof(zk_response_pending_header));
    zk_response_pending_err_code = err_code;
    zk_response_pending = BOOL_TRUE;
}

static int zk_publish_simple_response_now(const zk_message_header_t *request, int err_code)
{
    cJSON *root;
    int ret;

    root = zk_create_root_from_header(request, 1, err_code);
    if (root == NULL)
    {
        return -1;
    }
    ret = zk_send_json_root(root, NULL);
    cJSON_Delete(root);
    return ret;
}

static int zk_publish_simple_response(const zk_message_header_t *request, int err_code)
{
    if (zk_publish_simple_response_now(request, err_code) == 0)
    {
        return 0;
    }
    zk_schedule_simple_response(request, err_code);
    return -1;
}

void zk_fill_message_header(zk_message_header_t *header,
                            const char *sv,
                            const char *id,
                            const char *ct)
{
    const zk_mqtt_config_t *cfg;

    if (header == NULL)
    {
        return;
    }

    memset(header, 0, sizeof(*header));
    cfg = zk_mqtt_get_config();
    if (cfg == NULL)
    {
        return;
    }
    strncpy(header->sn, cfg->imei, sizeof(header->sn) - 1);
    zk_get_time_text(header->tm, sizeof(header->tm));
    strncpy(header->sv, sv, sizeof(header->sv) - 1);
    strncpy(header->id, id, sizeof(header->id) - 1);
    strncpy(header->ct, ct, sizeof(header->ct) - 1);
}

int zk_make_login_packet(char *buf, int buf_size)
{
    zk_message_header_t header;

    if (zk_mqtt_get_config() == NULL)
    {
        return -1;
    }
    zk_fill_message_header(&header, ZK_SV_REPT, ZK_LOGIN_REQUEST_ID, ZK_CT_LOGIN);
    if (header.sn[0] == '\0')
    {
        return -1;
    }
    zk_device_config_refresh_iccid();
    /* 登录上报：DevInfo.prodtp按协议使用prodtp字段名，Dim包含dimTp调光方式 */
    return snprintf(buf, buf_size,
                    "{\"SN\":\"%s\",\"TM\":\"%s\",\"SV\":\"%s\",\"ID\":\"%s\",\"CT\":\"%s\","
                    "\"DT\":{\"DevInfo\":{\"protId\":%d,\"clas\":\"%s\",\"prodtp\":\"%s\",\"hver\":%d,\"sver\":%d},"
                    "\"MdlInfo\":{\"mver\":\"%s\",\"iccid\":\"%s\"},"
                    "\"Gis\":{\"lng\":%ld,\"lat\":%ld,\"zone\":%d},"
                    "\"Dim\":[{\"cns\":%d,\"dimTp\":%d,\"polar\":%d,\"dlmt\":%d,\"ulmt\":%d,\"rti\":%d,\"rtPwr\":%d}],"
                    "\"Sense\":{\"di\":%d,\"sBri\":%d,\"sBriTm\":%d}}}",
                    header.sn,
                    header.tm,
                    header.sv,
                    header.id,
                    header.ct,
                    zk_dev_cfg.protId,
                    zk_dev_cfg.clas,
                    zk_dev_cfg.prottp,
                    zk_dev_cfg.hver,
                    zk_dev_cfg.sver,
                    zk_dev_cfg.mver,
                    zk_dev_cfg.iccid,
                    zk_dev_cfg.lng,
                    zk_dev_cfg.lat,
                    zk_dev_cfg.zone,
                    zk_dev_cfg.cns,
                    zk_dev_cfg.dimTp,
                    zk_dev_cfg.polar,
                    zk_dev_cfg.dlmt,
                    zk_dev_cfg.ulmt,
                    zk_dev_cfg.rti,
                    zk_dev_cfg.rtPwr,
                    zk_dev_cfg.di,
                    zk_dev_cfg.sBri,
                    zk_dev_cfg.sBriTm);
}

int zk_make_heartbeat_packet(char *buf, int buf_size)
{
    zk_message_header_t header;
    char message_id[8];

    if (zk_mqtt_get_config() == NULL)
    {
        return -1;
    }
    snprintf(message_id, sizeof(message_id), "%06lu", (unsigned long)zk_mqtt_next_json_id());
    strncpy(zk_last_heartbeat_id, message_id, sizeof(zk_last_heartbeat_id) - 1);
    zk_last_heartbeat_id[sizeof(zk_last_heartbeat_id) - 1] = '\0';
    zk_fill_message_header(&header, ZK_SV_REPT, message_id, ZK_CT_HEARTBEAT);
    if (header.sn[0] == '\0')
    {
        return -1;
    }
    return snprintf(buf, buf_size,
                    "{\"SN\":\"%s\",\"TM\":\"%s\",\"SV\":\"%s\",\"ID\":\"%s\",\"CT\":\"%s\"}",
                    header.sn,
                    header.tm,
                    header.sv,
                    header.id,
                    header.ct);
}

int zk_publish_login_packet(void)
{
    int len;

    if (zk_mqtt_init() == BOOL_FALSE)
    {
        return -1;
    }
    len = zk_make_login_packet(zk_tx_buf, sizeof(zk_tx_buf));
    if (len <= 0 || len >= (int)sizeof(zk_tx_buf))
    {
        return -1;
    }
    if (zk_send_payload(zk_tx_buf, (uint16)len, NULL) != 0)
    {
        return -1;
    }
    zk_login_state = ZK_LOGIN_STATE_WAIT_ACK;
    zk_login_tick = Timer_GetTickCount();
    return 0;
}

int zk_publish_heartbeat_packet(void)
{
    int len;

    if (zk_mqtt_get_config() == NULL)
    {
        return -1;
    }
    len = zk_make_heartbeat_packet(zk_tx_buf, sizeof(zk_tx_buf));
    if (len <= 0 || len >= (int)sizeof(zk_tx_buf))
    {
        return -1;
    }
    if (zk_send_payload(zk_tx_buf, (uint16)len, NULL) != 0)
    {
        return -1;
    }
    zk_heartbeat_tick = Timer_GetTickCount();
    return 0;
}

int zk_publish_error_response(const zk_message_header_t *request, int err_code)
{
    if (request == NULL || zk_mqtt_get_config() == NULL)
    {
        return -1;
    }
    return zk_publish_simple_response(request, err_code);
}

int zk_publish_response_with_dt(const zk_message_header_t *request,
                                int err_code,
                                zk_response_dt_builder_t builder,
                                void *ctx)
{
    cJSON *root;
    cJSON *dt;
    int ret;

    if (request == NULL || zk_mqtt_get_config() == NULL)
    {
        return -1;
    }

    root = zk_create_root_from_header(request, 1, err_code);
    if (root == NULL)
    {
        return -1;
    }
    dt = zk_cjson_create_tx_object("DT");
    if (dt == NULL)
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_AddItemToObject(root, "DT", dt);
    if (builder != NULL && builder(dt, ctx) != 0)
    {
        cJSON_Delete(root);
        return -1;
    }
    ret = zk_send_json_root(root, NULL);
    cJSON_Delete(root);
    return ret;
}

boolean_en zk_dispatch_message(cJSON *root, const zk_message_header_t *header)
{
    if (root == NULL || header == NULL)
    {
        return BOOL_FALSE;
    }
    if (zk_handle_property_read(root, header))
    {
        return BOOL_TRUE;
    }
    if (zk_handle_property_write(root, header))
    {
        return BOOL_TRUE;
    }
    if (zk_handle_control_message(root, header))
    {
        return BOOL_TRUE;
    }
    if (zk_handle_plan_message(root, header))
    {
        return BOOL_TRUE;
    }
    if (zk_handle_request_message(root, header))
    {
        return BOOL_TRUE;
    }
    if (zk_handle_ota_message(root, header))
    {
        return BOOL_TRUE;
    }
    if (strcmp(header->ct, ZK_CT_WRITE) == 0 || strcmp(header->ct, ZK_CT_READ) == 0)
    {
        zk_publish_error_response(header, 1);
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

/* 运行时间非阻塞统计：秒级更新RAM，6小时或掉电时低频持久化 */
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
    if (zk_runtime_last_tick == 0)
    {
        zk_runtime_last_tick = now;
        return;
    }

    elapsed_ms = now - zk_runtime_last_tick;
    if (elapsed_ms < 1000UL)
    {
        zk_runtime_save_process(now);
        return;                             /* 不到1秒不处理，降低CPU开销 */
    }

    elapsed_seconds = elapsed_ms / 1000UL;
    zk_boot_run_seconds += elapsed_seconds;
    zk_runtime_last_tick += elapsed_seconds * 1000UL;

    /* 仅当灯亮时累加亮灯时间（dim_level>0表示灯亮） */
    if (dim_level > 0U)
    {
        zk_boot_light_seconds += elapsed_seconds;
    }

    zk_runtime_save_process(now);
}

static boolean_en zk_signal_query_process(uint32 now)
{
    if (online == 0 || pubsend_state_idle() == BOOL_FALSE)
    {
        return BOOL_FALSE;
    }
    if (zk_signal_query_tick != 0 &&
        Timer_PassedDelay(zk_signal_query_tick, ZK_SIGNAL_QUERY_INTERVAL_MS) == BOOL_FALSE)
    {
        return BOOL_FALSE;
    }
    if (send_AT_Command_machine_finish() != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }

    send_AT_Command_machine_star(zk_signal_qeng_cmd,
                                 (uint8)strlen(zk_signal_qeng_cmd),
                                 zk_signal_qeng_resp,
                                 25,
                                 1);
    zk_signal_query_tick = now;
    return BOOL_TRUE;
}

static void zk_add_runtime_time_groups(cJSON *dt_root)
{
    cJSON *run_tm;
    cJSON *light_tm;
    cJSON *light_total;
    cJSON *light_run;
    uint32 run_minutes;                     /* 本次上电运行分钟 */
    uint32 light_run_minutes;               /* 本次上电亮灯分钟 */

    /* 先更新统计，确保上报的是最新累计值 */
    zk_runtime_counter_process();

    run_minutes = zk_boot_run_seconds / 60U;
    light_run_minutes = zk_boot_light_seconds / 60U;

    run_tm = zk_cjson_create_tx_object("RunTm");
    light_tm = zk_cjson_create_tx_object("LightTm");
    light_total = zk_cjson_create_tx_array("LightTm.tLtTime");
    light_run = zk_cjson_create_tx_array("LightTm.rLtTime");
    if (run_tm == NULL || light_tm == NULL || light_total == NULL || light_run == NULL)
    {
        return;
    }

    /* RunTm.tTime = Flash历史累计 + 本次上电累计，单位分钟 */
    cJSON_AddNumberToObject(run_tm, "tTime", (double)((zk_total_run_base_seconds + zk_boot_run_seconds) / 60U));
    /* RunTm.rTime = 本次上电运行分钟 */
    cJSON_AddNumberToObject(run_tm, "rTime", (double)run_minutes);
    cJSON_AddItemToObject(dt_root, "RunTm", run_tm);

    /* LightTm.tLtTime = Flash历史亮灯累计 + 本次亮灯累计，单位分钟 */
    cJSON_AddItemToArray(light_total, cJSON_CreateNumber((double)((zk_total_light_base_seconds + zk_boot_light_seconds) / 60U)));
    /* LightTm.rLtTime = 本次上电亮灯分钟（关灯后保持历史值不归零） */
    cJSON_AddItemToArray(light_run, cJSON_CreateNumber((double)light_run_minutes));
    cJSON_AddItemToObject(light_tm, "tLtTime", light_total);
    cJSON_AddItemToObject(light_tm, "rLtTime", light_run);
    cJSON_AddItemToObject(dt_root, "LightTm", light_tm);
}

static void zk_add_run_status_group(cJSON *dt_root)
{
    cJSON *run_status;
    cJSON *sts;
    cJSON *bri;

    run_status = zk_cjson_create_tx_object("RunSts");
    sts = zk_cjson_create_tx_array("RunSts.sts");
    bri = zk_cjson_create_tx_array("RunSts.bri");
    if (run_status == NULL || sts == NULL || bri == NULL)
    {
        return;
    }

    cJSON_AddItemToArray(sts, cJSON_CreateNumber(zk_get_run_status_code()));
    cJSON_AddItemToArray(bri, cJSON_CreateNumber(zk_get_current_brightness()));
    cJSON_AddItemToObject(run_status, "sts", sts);
    cJSON_AddItemToObject(run_status, "bri", bri);
    cJSON_AddItemToObject(dt_root, "RunSts", run_status);
}

static void zk_add_ele_info_group(cJSON *dt_root)
{
    cJSON *ele_info;
    cJSON *e;
    cJSON *c;
    cJSON *v;
    cJSON *f;
    cJSON *p;
    cJSON *r_ec;
    cJSON *t_ec;

    ele_info = zk_cjson_create_tx_object("EleInfo");
    e = zk_cjson_create_tx_array("EleInfo.e");
    c = zk_cjson_create_tx_array("EleInfo.c");
    v = zk_cjson_create_tx_array("EleInfo.v");
    f = zk_cjson_create_tx_array("EleInfo.f");
    p = zk_cjson_create_tx_array("EleInfo.p");
    r_ec = zk_cjson_create_tx_array("EleInfo.rEc");
    t_ec = zk_cjson_create_tx_array("EleInfo.tEc");
    if (ele_info == NULL || e == NULL || c == NULL || v == NULL ||
        f == NULL || p == NULL || r_ec == NULL || t_ec == NULL)
    {
        return;
    }

    /* EleInfo.e[0]: 0表示无故障；有故障时上报当前电源故障位图 */
    cJSON_AddItemToArray(e, cJSON_CreateNumber((double)error_flag_byte));
    cJSON_AddItemToArray(c, cJSON_CreateNumber((double)Z_ac_current));
    cJSON_AddItemToArray(v, cJSON_CreateNumber((double)ac_voltage_8209));
    cJSON_AddItemToArray(f, cJSON_CreateNumber((double)ac_pf));
    /* EleInfo.p: BL0942有功功率ac_powerpa原始单位0.01W，平台按原始数字显示W，故/100转为整数W（四舍五入） */
    cJSON_AddItemToArray(p, cJSON_CreateNumber((double)((ac_powerpa + 50U) / 100U)));
    cJSON_AddItemToArray(r_ec, cJSON_CreateNumber((double)energy_this_time));
    cJSON_AddItemToArray(t_ec, cJSON_CreateNumber((double)sys_data.ac_EnergyP));
    cJSON_AddItemToObject(ele_info, "e", e);
    cJSON_AddItemToObject(ele_info, "c", c);
    cJSON_AddItemToObject(ele_info, "v", v);
    cJSON_AddItemToObject(ele_info, "f", f);
    cJSON_AddItemToObject(ele_info, "p", p);
    cJSON_AddItemToObject(ele_info, "rEc", r_ec);
    cJSON_AddItemToObject(ele_info, "tEc", t_ec);
    cJSON_AddNumberToObject(ele_info, "pwr", power_down_flag ? 1 : 0);
    cJSON_AddNumberToObject(ele_info, "lc", danger_current_warn ? 30 : 0);
    cJSON_AddItemToObject(dt_root, "EleInfo", ele_info);
}

static void zk_add_per_sts_group(cJSON *dt_root)
{
    cJSON *per_sts;

    per_sts = zk_cjson_create_tx_object("PerSts");
    if (per_sts == NULL)
    {
        return;
    }
    /* PerSts: 仅上报temp（有NTC真实来源），lux无光照传感器硬件故不上报 */
    cJSON_AddNumberToObject(per_sts, "temp", Ntctemp.Ntctemp);
    cJSON_AddItemToObject(dt_root, "PerSts", per_sts);
}

static void zk_add_signal_group(cJSON *dt_root)
{
    cJSON *signal;
    s32 rsrp;

    signal = zk_cjson_create_tx_object("Signal");
    if (signal == NULL)
    {
        return;
    }
    /* Signal.rsrp只使用AT+QENG真实解析值；未获取到时不上报rsrp，避免用等级值伪造 */
    if (nb_get_rsrp_dbm10(&rsrp) == BOOL_TRUE)
    {
        cJSON_AddNumberToObject(signal, "rsrp", (double)rsrp);
    }
    cJSON_AddNumberToObject(signal, "reg", online ? 1 : 0);
    cJSON_AddItemToObject(dt_root, "Signal", signal);
}

static void zk_add_angle_group(cJSON *dt_root)
{
    cJSON *angle;

    angle = zk_cjson_create_tx_object("Angle");
    if (angle == NULL)
    {
        return;
    }
    cJSON_AddNumberToObject(angle, "x", 0);
    cJSON_AddNumberToObject(angle, "y", 0);
    cJSON_AddNumberToObject(angle, "z", 0);
    cJSON_AddItemToObject(dt_root, "Angle", angle);
}

static int zk_publish_runtime_report(const char *ct)
{
    zk_message_header_t header;
    cJSON *root;
    cJSON *dt;
    char message_id[8];

    if (zk_mqtt_get_config() == NULL)
    {
        return -1;
    }

    snprintf(message_id, sizeof(message_id), "%06lu", (unsigned long)zk_mqtt_next_json_id());
    zk_fill_message_header(&header, ZK_SV_REPT, message_id, ct);
    root = zk_create_root_from_header(&header, 0, 0);
    if (root == NULL)
    {
        return -1;
    }

    dt = zk_cjson_create_tx_object("DT");
    if (dt == NULL)
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_AddItemToObject(root, "DT", dt);
    zk_add_runtime_time_groups(dt);
    zk_add_run_status_group(dt);
    zk_add_ele_info_group(dt);
    zk_add_per_sts_group(dt);
    if (strcmp(ct, ZK_CT_CYCLIC) == 0)
    {
        zk_add_signal_group(dt);
        /* zk_add_angle_group: 当前无倾角传感器硬件，不上报Angle字段；后续接入硬件后恢复调用 */
    }

    if (zk_send_json_root(root, NULL) != 0)
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_Delete(root);
    return 0;
}

static int zk_publish_time_request(void)
{
    zk_message_header_t header;
    cJSON *root;
    cJSON *dt;
    char message_id[8];

    if (zk_mqtt_get_config() == NULL)
    {
        return -1;
    }

    snprintf(message_id, sizeof(message_id), "%06lu", (unsigned long)zk_mqtt_next_json_id());
    zk_fill_message_header(&header, ZK_SV_RQST, message_id, ZK_CT_READ);
    root = zk_create_root_from_header(&header, 0, 0);
    if (root == NULL)
    {
        return -1;
    }
    dt = zk_cjson_create_tx_object("DT");
    if (dt == NULL)
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_AddStringToObject(dt, "DO", "TmCali");
    cJSON_AddItemToObject(root, "DT", dt);
    if (zk_send_json_root(root, NULL) != 0)
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_Delete(root);
    return 0;
}

static void zk_alarm_reset_states(void)
{
    uint32 i;

    for (i = 0; i < ZK_ALARM_STATE_COUNT; ++i)
    {
        zk_alarm_states[i].pending = 0;
        zk_alarm_states[i].pending_status = 0;
        zk_alarm_states[i].reported = 0;
        zk_alarm_states[i].value = 0;
    }
}

static uint32 zk_alarm_current_threshold(uint32 percent)
{
    if (SET_OUTCUR == 0)
    {
        return 0;
    }
    return ((uint32)SET_OUTCUR * percent) / 100U;
}

static uint32 zk_alarm_temperature_value(void)
{
    signed short temp;

    temp = Ntctemp.Ntctemp;
    if (temp <= 0)
    {
        return 0;
    }
    return (uint32)((temp + 5) / 10);
}

static uint32 zk_alarm_temperature_threshold(void)
{
    if (INNRE_TEMP_PRO <= 0)
    {
        return 0;
    }
    return (uint32)INNRE_TEMP_PRO;
}

static void zk_alarm_update_level(zk_alarm_state_t *alarm, u8 active, uint32 value, uint32 threshold)
{
    if (alarm == NULL)
    {
        return;
    }

    alarm->value = value;
    alarm->threshold = threshold;

    if (active)
    {
        if (alarm->pending && alarm->pending_status == 0)
        {
            alarm->pending = 0;
        }
        if (alarm->reported == 0)
        {
            alarm->pending = 1;
            alarm->pending_status = 1;
        }
    }
    else
    {
        if (alarm->pending && alarm->pending_status != 0)
        {
            alarm->pending = 0;
        }
        if (alarm->reported != 0)
        {
            alarm->pending = 1;
            alarm->pending_status = 0;
        }
    }
}

static void zk_alarm_update_power_down(void)
{
    zk_alarm_state_t *alarm;

    alarm = &zk_alarm_states[ZK_ALARM_POWER_DOWN_INDEX];
    alarm->value = ac_voltage_8209;
    alarm->threshold = 70;
    if (power_down_flag != 0 && alarm->pending == 0)
    {
        alarm->pending = 1;
        alarm->pending_status = 1;
    }
}

static void zk_alarm_collect_sources(void)
{
    zk_alarm_update_level(&zk_alarm_states[0], Error_3_OV ? 1 : 0, ac_voltage_8209, 3200);
    zk_alarm_update_level(&zk_alarm_states[1], Error_4_LV ? 1 : 0, ac_voltage_8209, 800);
    zk_alarm_update_level(&zk_alarm_states[2], Error_1_OL ? 1 : 0, Io_value, zk_alarm_current_threshold(150));
    zk_alarm_update_level(&zk_alarm_states[3], Error_Out_LV ? 1 : 0, Io_value, zk_alarm_current_threshold(80));
    zk_alarm_update_level(&zk_alarm_states[4], Error_0_linght ? 1 : 0, Po_value, 0);
    zk_alarm_update_level(&zk_alarm_states[5], danger_current_warn ? 1 : 0, dangeo_out, 30);
    zk_alarm_update_level(&zk_alarm_states[6], driver_temperarure_warn ? 1 : 0,
                          zk_alarm_temperature_value(),
                          zk_alarm_temperature_threshold());
    zk_alarm_update_power_down();
}

static int zk_publish_alarm_report(uint16 alarm_id, u8 status, uint32 value, uint32 threshold)
{
    zk_message_header_t header;
    cJSON *root;
    cJSON *dt;
    cJSON *alm;
    char message_id[8];
    char alarm_time[20];
    int len;

    if (zk_mqtt_get_config() == NULL || pubsend_state_idle() == BOOL_FALSE)
    {
        return -1;
    }

    zk_cjson_prepare_tx();
    snprintf(message_id, sizeof(message_id), "%06lu", (unsigned long)zk_mqtt_next_json_id());
    zk_fill_message_header(&header, ZK_SV_REPT, message_id, ZK_CT_ALARM);
    root = zk_create_root_from_header(&header, 0, 0);
    if (root == NULL)
    {
        return -1;
    }

    dt = zk_cjson_create_tx_object("DT");
    alm = zk_cjson_create_tx_object("DT.alm");
    if (dt == NULL || alm == NULL)
    {
        cJSON_Delete(root);
        return -1;
    }

    zk_get_time_text(alarm_time, sizeof(alarm_time));
    cJSON_AddNumberToObject(alm, "almId", (double)alarm_id);
    cJSON_AddStringToObject(alm, "time", alarm_time);
    cJSON_AddNumberToObject(alm, "status", status ? 1 : 0);
    cJSON_AddNumberToObject(alm, "cns", ZK_CNCTRL_SUPPORTED_CNS);
    cJSON_AddNumberToObject(alm, "value", (double)value);
    cJSON_AddNumberToObject(alm, "threshold", (double)threshold);
    cJSON_AddItemToObject(dt, "alm", alm);
    cJSON_AddItemToObject(root, "DT", dt);

    memset(zk_tx_buf, 0, sizeof(zk_tx_buf));
    if (cJSON_PrintPreallocated(root, zk_tx_buf, (int)sizeof(zk_tx_buf), 0) == 0)
    {
        cJSON_Delete(root);
        return -1;
    }
    len = (int)strlen(zk_tx_buf);
    if (len <= 0 || nbSendTcpData((uint8 *)zk_tx_buf, (uint16)len) != NB_ERROR_NONE)
    {
        cJSON_Delete(root);
        return -1;
    }

    cJSON_Delete(root);
    return 0;
}

static boolean_en zk_alarm_publish_pending(void)
{
    uint32 i;
    u8 status;

    for (i = 0; i < ZK_ALARM_STATE_COUNT; ++i)
    {
        if (zk_alarm_states[i].pending == 0)
        {
            continue;
        }

        status = zk_alarm_states[i].pending_status ? 1 : 0;
        if (zk_publish_alarm_report(zk_alarm_states[i].alm_id,
                                    status,
                                    zk_alarm_states[i].value,
                                    zk_alarm_states[i].threshold) == 0)
        {
            zk_alarm_states[i].pending = 0;
            if (zk_alarm_states[i].one_shot && status != 0)
            {
                power_down_flag = 0;
                zk_alarm_states[i].reported = 0;
            }
            else
            {
                zk_alarm_states[i].reported = status;
            }
        }
        return BOOL_TRUE;
    }

    return BOOL_FALSE;
}

static boolean_en zk_alarm_process(void)
{
    zk_alarm_collect_sources();
    return zk_alarm_publish_pending();
}

static int zk_publish_ota_progress_now(uint32 progress)
{
    zk_message_header_t header;
    cJSON *root;
    cJSON *dt;
    const char *topic;

    if (zk_last_ota_id[0] == '\0')
    {
        return -1;
    }
    topic = zk_mqtt_get_config() != NULL ? zk_mqtt_cfg.pub_upgrade_topic : NULL;
    if (topic == NULL || topic[0] == '\0')
    {
        return -1;
    }

    zk_fill_message_header(&header, ZK_SV_OTA, zk_last_ota_id, ZK_CT_PROGRESS);
    root = zk_create_root_from_header(&header, 0, 0);
    if (root == NULL)
    {
        return -1;
    }
    dt = zk_cjson_create_tx_object("DT");
    if (dt == NULL)
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_AddNumberToObject(dt, "progress", (double)progress);
    cJSON_AddItemToObject(root, "DT", dt);
    if (zk_send_json_root(root, topic) != 0)
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_Delete(root);
    return 0;
}

int zk_publish_ota_progress(uint32 progress)
{
    if (zk_last_ota_id[0] == '\0' ||
        zk_mqtt_get_config() == NULL ||
        zk_mqtt_cfg.pub_upgrade_topic[0] == '\0')
    {
        return -1;
    }
    if (zk_publish_ota_progress_now(progress) == 0)
    {
        return 0;
    }
    zk_ota_progress_value = progress;
    zk_ota_progress_pending = BOOL_TRUE;
    return -1;
}

static int zk_publish_ota_error_now(int err_code)
{
    zk_message_header_t header;
    cJSON *root;
    cJSON *dt;
    const char *topic;

    if (zk_last_ota_id[0] == '\0')
    {
        return -1;
    }
    topic = zk_mqtt_get_config() != NULL ? zk_mqtt_cfg.pub_upgrade_topic : NULL;
    if (topic == NULL || topic[0] == '\0')
    {
        return -1;
    }

    zk_fill_message_header(&header, ZK_SV_OTA, zk_last_ota_id, ZK_CT_ERROR);
    root = zk_create_root_from_header(&header, 0, 0);
    if (root == NULL)
    {
        return -1;
    }
    dt = zk_cjson_create_tx_object("DT");
    if (dt == NULL)
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_AddNumberToObject(dt, "error", err_code);
    cJSON_AddItemToObject(root, "DT", dt);
    if (zk_send_json_root(root, topic) != 0)
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_Delete(root);
    return 0;
}

int zk_publish_ota_error(int err_code)
{
    if (zk_last_ota_id[0] == '\0' ||
        zk_mqtt_get_config() == NULL ||
        zk_mqtt_cfg.pub_upgrade_topic[0] == '\0')
    {
        return -1;
    }
    if (zk_publish_ota_error_now(err_code) == 0)
    {
        return 0;
    }
    zk_ota_error_code = err_code;
    zk_ota_error_pending = BOOL_TRUE;
    return -1;
}

void zk_notify_state_changed(void)
{
    zk_change_report_pending = BOOL_TRUE;
    zk_change_report_tick = Timer_GetTickCount();
}

static void zk_cancel_control_restore(void)
{
    zk_control_restore_pending = BOOL_FALSE;
    zk_control_restore_tick = 0;
    zk_control_restore_delay_ms = 0;
}

static void zk_schedule_control_restore(int restore_brightness, int last_sec)
{
    if (last_sec <= 0)
    {
        zk_cancel_control_restore();
        return;
    }

    zk_control_restore_brightness = restore_brightness;
    zk_control_restore_delay_ms = (uint32)last_sec * 1000UL;
    zk_control_restore_tick = Timer_GetTickCount();
    zk_control_restore_pending = BOOL_TRUE;
}

static void zk_control_restore_process(void)
{
    if (zk_control_restore_pending == BOOL_TRUE &&
        Timer_PassedDelay(zk_control_restore_tick, zk_control_restore_delay_ms))
    {
        zk_control_restore_pending = BOOL_FALSE;
        zk_apply_brightness(zk_control_restore_brightness);
    }
}

void zk_mqtt_session_process(void)
{
    uint32 now;

    now = Timer_GetTickCount();
    zk_control_restore_process();

    if (zk_reboot_pending == BOOL_TRUE && Timer_PassedDelay(zk_reboot_tick, 500))
    {
        NVIC_SystemReset();
    }

    if (zk_login_state == ZK_LOGIN_STATE_WAIT_ACK)
    {
        if (Timer_PassedDelay(zk_login_tick, ZK_LOGIN_ACK_TIMEOUT_MS) == BOOL_FALSE)
        {
            return;
        }
        zk_login_state = ZK_LOGIN_STATE_IDLE;
    }

    if (zk_login_state == ZK_LOGIN_STATE_IDLE)
    {
        (void)zk_publish_login_packet();
        return;
    }

    if (zk_login_state == ZK_LOGIN_STATE_ONLINE)
    {
        if (zk_alarm_process() == BOOL_TRUE)
        {
            return;
        }

        if (zk_ota_error_pending == BOOL_TRUE)
        {
            if (zk_publish_ota_error_now(zk_ota_error_code) == 0)
            {
                zk_ota_error_pending = BOOL_FALSE;
                zk_ota_progress_pending = BOOL_FALSE;
            }
            return;
        }

        if (zk_ota_progress_pending == BOOL_TRUE)
        {
            if (zk_publish_ota_progress_now(zk_ota_progress_value) == 0)
            {
                zk_ota_progress_pending = BOOL_FALSE;
            }
            return;
        }

        if (zk_response_pending == BOOL_TRUE)
        {
            if (zk_publish_simple_response_now(&zk_response_pending_header,
                                               zk_response_pending_err_code) == 0)
            {
                zk_response_pending = BOOL_FALSE;
            }
            return;
        }

        if (zk_change_report_pending == BOOL_TRUE &&
            Timer_PassedDelay(zk_change_report_tick, ZK_CHANGE_REPORT_SETTLE_MS) == BOOL_FALSE)
        {
            return;
        }

        if (zk_patrol_report_pending == BOOL_TRUE)
        {
            if (pubsend_state_idle() == BOOL_TRUE &&
                zk_publish_runtime_report(ZK_CT_CYCLIC) == 0)
            {
                zk_patrol_report_pending = BOOL_FALSE;
                zk_report_tick = now;
            }
            return;
        }

        if (zk_change_report_pending == BOOL_TRUE)
        {
            if (zk_publish_runtime_report(ZK_CT_CHANGE) == 0)
            {
                zk_change_report_pending = BOOL_FALSE;
            }
            return;
        }

        if (zk_report_tick == 0 ||
            Timer_PassedDelay(zk_report_tick, zk_get_effective_period_sec(zk_dev_cfg.uPeriod, ZK_UPLOAD_INTERVAL_SEC) * 1000UL))
        {
            if (zk_publish_runtime_report(ZK_CT_CYCLIC) == 0)
            {
                zk_report_tick = now;
            }
            return;
        }

        if (zk_time_request_tick == 0 ||
            Timer_PassedDelay(zk_time_request_tick, zk_get_effective_period_sec(zk_dev_cfg.tPeriod, ZK_TIME_REQUEST_INTERVAL_SEC) * 1000UL))
        {
            if (zk_publish_time_request() == 0)
            {
                zk_time_request_tick = now;
            }
            return;
        }

        if (Timer_PassedDelay(zk_heartbeat_tick, zk_get_effective_period_sec(zk_dev_cfg.hPeriod, ZK_HEARTBEAT_INTERVAL_SEC) * 1000UL))
        {
            (void)zk_publish_heartbeat_packet();
            return;
        }

        (void)zk_signal_query_process(now);
    }
}

static void zk_copy_json_string(cJSON *root, const char *key, char *out, int out_size)
{
    cJSON *node;

    node = cJSON_GetObjectItem(root, key);
    if (node != NULL && cJSON_IsString(node) && node->valuestring != NULL)
    {
        strncpy(out, node->valuestring, out_size - 1);
        out[out_size - 1] = '\0';
    }
}

int zk_parse_message_header(const char *json_str, zk_message_header_t *header)
{
    cJSON *root;

    if (json_str == NULL || header == NULL)
    {
        return -1;
    }

    if (strlen(json_str) > ZK_JSON_RX_MAX)
    {
        return -1;
    }

    zk_cjson_prepare_parse();
    root = cJSON_Parse(json_str);
    if (root == NULL)
    {
        return -1;
    }
    if (zk_parse_message_header_from_root(root, header) != 0)
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_Delete(root);
    return 0;
}

int zk_parse_message_header_from_root(cJSON *root, zk_message_header_t *header)
{
    if (root == NULL || header == NULL)
    {
        return -1;
    }
    memset(header, 0, sizeof(*header));
    zk_copy_json_string(root, "SN", header->sn, sizeof(header->sn));
    zk_copy_json_string(root, "TM", header->tm, sizeof(header->tm));
    zk_copy_json_string(root, "SV", header->sv, sizeof(header->sv));
    zk_copy_json_string(root, "ID", header->id, sizeof(header->id));
    zk_copy_json_string(root, "CT", header->ct, sizeof(header->ct));

    if (header->sn[0] == '\0' ||
        header->sv[0] == '\0' ||
        header->id[0] == '\0' ||
        header->ct[0] == '\0')
    {
        return -1;
    }
    return 0;
}

boolean_en zk_message_header_matches_device(const zk_message_header_t *header)
{
    const zk_mqtt_config_t *cfg;

    if (header == NULL)
    {
        return BOOL_FALSE;
    }
    cfg = zk_mqtt_get_config();
    if (cfg == NULL)
    {
        return BOOL_FALSE;
    }
    return (strcmp(header->sn, cfg->imei) == 0) ? BOOL_TRUE : BOOL_FALSE;
}

int zk_parse_login_response(const char *json_str, zk_login_response_t *response)
{
    if (zk_parse_message_header(json_str, response) != 0)
    {
        return -1;
    }
    return (strcmp(response->ct, ZK_CT_LOGIN) == 0) ? 0 : -1;
}

boolean_en zk_mqtt_accept_login_ack(const zk_message_header_t *header)
{
    const zk_mqtt_config_t *cfg;

    if (header == NULL)
    {
        return BOOL_FALSE;
    }
    if (zk_login_state != ZK_LOGIN_STATE_WAIT_ACK)
    {
        return BOOL_FALSE;
    }
    cfg = zk_mqtt_get_config();
    if (cfg == NULL)
    {
        return BOOL_FALSE;
    }
    if (strcmp(header->ct, ZK_CT_LOGIN) == 0 &&
        strcmp(header->sn, cfg->imei) == 0 &&
        (header->id[0] == '\0' || strcmp(header->id, ZK_LOGIN_REQUEST_ID) == 0))
    {
        zk_login_state = ZK_LOGIN_STATE_ONLINE;
        zk_heartbeat_tick = Timer_GetTickCount();
        zk_report_tick = 0;
        zk_time_request_tick = 0;
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

boolean_en zk_mqtt_accept_heartbeat_ack(const zk_message_header_t *header)
{
    const zk_mqtt_config_t *cfg;

    if (header == NULL)
    {
        return BOOL_FALSE;
    }
    if (zk_login_state != ZK_LOGIN_STATE_ONLINE)
    {
        return BOOL_FALSE;
    }
    cfg = zk_mqtt_get_config();
    if (cfg == NULL)
    {
        return BOOL_FALSE;
    }
    if (header->id[0] != '\0' &&
        zk_last_heartbeat_id[0] != '\0' &&
        strcmp(header->id, zk_last_heartbeat_id) != 0)
    {
        return BOOL_FALSE;
    }
    if (strcmp(header->ct, ZK_CT_HEARTBEAT) == 0 &&
        strcmp(header->sn, cfg->imei) == 0)
    {
        zk_heartbeat_tick = Timer_GetTickCount();
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static boolean_en zk_json_get_number(cJSON *object, const char *key, int *value)
{
    cJSON *node;

    if (object == NULL)
    {
        return BOOL_FALSE;
    }
    node = cJSON_GetObjectItem(object, key);
    if (node != NULL && cJSON_IsNumber(node))
    {
        *value = node->valueint;
        return BOOL_TRUE;
    }
    if (node != NULL && cJSON_IsBool(node))
    {
        *value = cJSON_IsTrue(node) ? 1 : 0;
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static boolean_en zk_json_get_string(cJSON *object, const char *key, char *value, int value_size)
{
    cJSON *node;

    if (object == NULL || value == NULL || value_size <= 1)
    {
        return BOOL_FALSE;
    }
    node = cJSON_GetObjectItem(object, key);
    if (node == NULL || !cJSON_IsString(node) || node->valuestring == NULL)
    {
        return BOOL_FALSE;
    }
    strncpy(value, node->valuestring, value_size - 1);
    value[value_size - 1] = '\0';
    return BOOL_TRUE;
}

static void zk_add_dev_info_prop(cJSON *dt_root)
{
    cJSON *item;

    item = zk_cjson_create_tx_object("DevInfo");
    if (item == NULL)
    {
        return;
    }
    cJSON_AddNumberToObject(item, "protId", zk_dev_cfg.protId);
    cJSON_AddStringToObject(item, "SN", zk_mqtt_cfg.imei);
    cJSON_AddStringToObject(item, "clas", zk_dev_cfg.clas);
    cJSON_AddStringToObject(item, "prodtp", zk_dev_cfg.prottp); /* JSON字段按协议使用prodtp */
    cJSON_AddNumberToObject(item, "hver", zk_dev_cfg.hver);
    cJSON_AddNumberToObject(item, "sver", zk_dev_cfg.sver);
    cJSON_AddItemToObject(dt_root, "DevInfo", item);
}

static void zk_add_mdl_info_prop(cJSON *dt_root)
{
    cJSON *item;

    item = zk_cjson_create_tx_object("MdlInfo");
    if (item == NULL)
    {
        return;
    }
    cJSON_AddStringToObject(item, "mver", zk_dev_cfg.mver);
    cJSON_AddStringToObject(item, "iccid", zk_dev_cfg.iccid);
    cJSON_AddItemToObject(dt_root, "MdlInfo", item);
}

static void zk_add_gis_prop(cJSON *dt_root)
{
    cJSON *item;

    item = zk_cjson_create_tx_object("Gis");
    if (item == NULL)
    {
        return;
    }
    cJSON_AddNumberToObject(item, "lng", zk_dev_cfg.lng);
    cJSON_AddNumberToObject(item, "lat", zk_dev_cfg.lat);
    cJSON_AddNumberToObject(item, "zone", zk_dev_cfg.zone);
    cJSON_AddItemToObject(dt_root, "Gis", item);
}

static void zk_add_dim_prop(cJSON *dt_root)
{
    cJSON *array;
    cJSON *item;

    array = zk_cjson_create_tx_array("Dim");
    item = zk_cjson_create_tx_object("Dim.item");
    if (array == NULL || item == NULL)
    {
        return;
    }
    cJSON_AddNumberToObject(item, "cns", zk_dev_cfg.cns);
    cJSON_AddNumberToObject(item, "dimTp", zk_dev_cfg.dimTp);
    cJSON_AddNumberToObject(item, "polar", zk_dev_cfg.polar);
    cJSON_AddNumberToObject(item, "dlmt", zk_dev_cfg.dlmt);
    cJSON_AddNumberToObject(item, "ulmt", zk_dev_cfg.ulmt);
    cJSON_AddNumberToObject(item, "rti", zk_dev_cfg.rti);
    cJSON_AddNumberToObject(item, "rtPwr", zk_dev_cfg.rtPwr);
    cJSON_AddItemToArray(array, item);
    cJSON_AddItemToObject(dt_root, "Dim", array);
}

static void zk_add_sense_prop(cJSON *dt_root)
{
    cJSON *item;

    item = zk_cjson_create_tx_object("Sense");
    if (item == NULL)
    {
        return;
    }
    cJSON_AddNumberToObject(item, "di", zk_dev_cfg.di);
    cJSON_AddNumberToObject(item, "sBri", zk_dev_cfg.sBri);
    cJSON_AddNumberToObject(item, "sBriTm", zk_dev_cfg.sBriTm);
    cJSON_AddItemToObject(dt_root, "Sense", item);
}

static void zk_add_spread_prop(cJSON *dt_root)
{
    cJSON *item;

    item = zk_cjson_create_tx_object("Spread");
    if (item == NULL)
    {
        return;
    }
    cJSON_AddNumberToObject(item, "offset", zk_dev_cfg.spreadOffset);
    cJSON_AddNumberToObject(item, "window", zk_dev_cfg.spreadWindow);
    cJSON_AddNumberToObject(item, "interval", zk_dev_cfg.spreadInterval);
    cJSON_AddItemToObject(dt_root, "Spread", item);
}

static void zk_add_comm_prop(cJSON *dt_root)
{
    cJSON *item;

    item = zk_cjson_create_tx_object("Comm");
    if (item == NULL)
    {
        return;
    }
    cJSON_AddNumberToObject(item, "main", zk_dev_cfg.commMain);
    cJSON_AddNumberToObject(item, "sub", zk_dev_cfg.commSub);
    cJSON_AddNumberToObject(item, "sAuto", zk_dev_cfg.commSAuto);
    cJSON_AddItemToObject(dt_root, "Comm", item);
}

static void zk_add_svr_prop(cJSON *dt_root)
{
    cJSON *item;

    item = zk_cjson_create_tx_object("Svr");
    if (item == NULL)
    {
        return;
    }
    cJSON_AddStringToObject(item, "svrIp", zk_dev_cfg.svrIp);
    cJSON_AddNumberToObject(item, "svrPort", zk_dev_cfg.svrPort);
    cJSON_AddNumberToObject(item, "uPeriod", zk_dev_cfg.uPeriod);
    cJSON_AddNumberToObject(item, "hPeriod", zk_dev_cfg.hPeriod);
    cJSON_AddNumberToObject(item, "tPeriod", zk_dev_cfg.tPeriod);
    cJSON_AddItemToObject(dt_root, "Svr", item);
}

static void zk_add_rtc_prop(cJSON *dt_root)
{
    char text[20];

    zk_get_time_text(text, sizeof(text));
    cJSON_AddStringToObject(dt_root, "RTC", text);
}

static int zk_add_property_to_dt(cJSON *dt_root, const char *name)
{
    if (strcmp(name, "DevInfo") == 0)
    {
        zk_add_dev_info_prop(dt_root);
    }
    else if (strcmp(name, "MdlInfo") == 0)
    {
        zk_add_mdl_info_prop(dt_root);
    }
    else if (strcmp(name, "Gis") == 0)
    {
        zk_add_gis_prop(dt_root);
    }
    else if (strcmp(name, "Dim") == 0)
    {
        zk_add_dim_prop(dt_root);
    }
    else if (strcmp(name, "Sense") == 0)
    {
        zk_add_sense_prop(dt_root);
    }
    else if (strcmp(name, "RunSts") == 0)
    {
        zk_add_run_status_group(dt_root);
    }
    else if (strcmp(name, "EleInfo") == 0)
    {
        zk_add_ele_info_group(dt_root);
    }
    else if (strcmp(name, "PerSts") == 0)
    {
        zk_add_per_sts_group(dt_root);
    }
    else if (strcmp(name, "Signal") == 0)
    {
        zk_add_signal_group(dt_root);
    }
    else if (strcmp(name, "RTC") == 0)
    {
        zk_add_rtc_prop(dt_root);
    }
    else if (strcmp(name, "Svr") == 0)
    {
        zk_add_svr_prop(dt_root);
    }
    else if (strcmp(name, "Comm") == 0)
    {
        zk_add_comm_prop(dt_root);
    }
    else if (strcmp(name, "Spread") == 0)
    {
        zk_add_spread_prop(dt_root);
    }
    else
    {
        return 1;
    }
    return 0;
}

static boolean_en zk_json_pick_config_number(cJSON *object,
                                             const char *key,
                                             int *value,
                                             int *err)
{
    cJSON *node;

    node = cJSON_GetObjectItem(object, key);
    if (node == NULL)
    {
        return BOOL_FALSE;
    }
    if (cJSON_IsNumber(node))
    {
        *value = node->valueint;
        *err = 0;
        return BOOL_TRUE;
    }
    if (cJSON_IsBool(node))
    {
        *value = cJSON_IsTrue(node) ? 1 : 0;
        *err = 0;
        return BOOL_TRUE;
    }
    *err = 2;
    return BOOL_TRUE;
}

static boolean_en zk_json_pick_config_string(cJSON *object,
                                             const char *key,
                                             char *value,
                                             int value_size,
                                             int *err)
{
    cJSON *node;

    node = cJSON_GetObjectItem(object, key);
    if (node == NULL)
    {
        return BOOL_FALSE;
    }
    if (!cJSON_IsString(node) || node->valuestring == NULL)
    {
        *err = 2;
        return BOOL_TRUE;
    }
    strncpy(value, node->valuestring, value_size - 1);
    value[value_size - 1] = '\0';
    *err = 0;
    return BOOL_TRUE;
}

static void zk_reset_config_period_timers(void)
{
    zk_report_tick = Timer_GetTickCount();
    zk_time_request_tick = Timer_GetTickCount();
    zk_heartbeat_tick = Timer_GetTickCount();
}

static int zk_apply_gis_config(cJSON *gis, zk_device_config_t *config)
{
    int value;
    int err;

    if (gis == NULL || !cJSON_IsObject(gis))
    {
        return 4;
    }
    if (zk_json_pick_config_number(gis, "lng", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value < -180000000 || value > 180000000)
        {
            return 3;
        }
        config->lng = value;
    }
    if (zk_json_pick_config_number(gis, "lat", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value < -90000000 || value > 90000000)
        {
            return 3;
        }
        config->lat = value;
    }
    if (zk_json_pick_config_number(gis, "zone", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value < -12 || value > 12)
        {
            return 3;
        }
        config->zone = value;
    }
    return 0;
}

static int zk_apply_dim_config(cJSON *dim, zk_device_config_t *config)
{
    cJSON *item;
    int index;
    int value;
    int err;

    if (dim == NULL || !cJSON_IsArray(dim) || cJSON_GetArraySize(dim) <= 0)
    {
        return 7;
    }
    for (index = 0; index < cJSON_GetArraySize(dim); ++index)
    {
        item = cJSON_GetArrayItem(dim, index);
        if (item == NULL || !cJSON_IsObject(item))
        {
            return 2;
        }
        if (zk_json_pick_config_number(item, "cns", &value, &err) == BOOL_TRUE)
        {
            if (err != 0)
            {
                return err;
            }
            if (value != 1)
            {
                return 6;
            }
            config->cns = value;
        }
        if (zk_json_pick_config_number(item, "dimTp", &value, &err) == BOOL_TRUE)
        {
            if (err != 0)
            {
                return err;
            }
            if (value < 0 || value > 1)
            {
                return 3;
            }
            config->dimTp = value;
        }
        if (zk_json_pick_config_number(item, "polar", &value, &err) == BOOL_TRUE)
        {
            if (err != 0)
            {
                return err;
            }
            if (value < 0 || value > 1)
            {
                return 3;
            }
            config->polar = value;
        }
        if (zk_json_pick_config_number(item, "dlmt", &value, &err) == BOOL_TRUE)
        {
            if (err != 0)
            {
                return err;
            }
            if (value < 500 || value > 2000)
            {
                return 3;
            }
            config->dlmt = value;
        }
        if (zk_json_pick_config_number(item, "ulmt", &value, &err) == BOOL_TRUE)
        {
            if (err != 0)
            {
                return err;
            }
            if (value < 7000 || value > 10000)
            {
                return 3;
            }
            config->ulmt = value;
        }
        if (zk_json_pick_config_number(item, "rti", &value, &err) == BOOL_TRUE)
        {
            if (err != 0)
            {
                return err;
            }
            if (value < 0 || value > 1)
            {
                return 3;
            }
            config->rti = value;
        }
        if (zk_json_pick_config_number(item, "rtPwr", &value, &err) == BOOL_TRUE)
        {
            if (err != 0)
            {
                return err;
            }
            if (value < 0 || value > 1000)
            {
                return 3;
            }
            config->rtPwr = value;
        }
    }
    return 0;
}

static int zk_apply_sense_config(cJSON *sense, zk_device_config_t *config)
{
    int value;
    int err;

    if (sense == NULL || !cJSON_IsObject(sense))
    {
        return 4;
    }
    if (zk_json_pick_config_number(sense, "di", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value < 0 || value > 1)
        {
            return 3;
        }
        config->di = value;
    }
    if (zk_json_pick_config_number(sense, "sBri", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value != 0 && (value < 10 || value > 100))
        {
            return 3;
        }
        config->sBri = value;
    }
    if (zk_json_pick_config_number(sense, "sBriTm", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value < 1 || value > 30)
        {
            return 3;
        }
        config->sBriTm = value;
    }
    return 0;
}

static int zk_apply_svr_config(cJSON *svr, zk_device_config_t *config)
{
    int value;
    int err;
    char ip[32];

    if (svr == NULL || !cJSON_IsObject(svr))
    {
        return 4;
    }
    if (zk_json_pick_config_string(svr, "svrIp", ip, sizeof(ip), &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        strncpy(config->svrIp, ip, sizeof(config->svrIp) - 1);
        config->svrIp[sizeof(config->svrIp) - 1] = '\0';
    }
    if (zk_json_pick_config_number(svr, "svrPort", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value <= 0 || value > 65535)
        {
            return 3;
        }
        config->svrPort = value;
    }
    if (zk_json_pick_config_number(svr, "uPeriod", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value < 0 || value > 1800)
        {
            return 3;
        }
        config->uPeriod = value;
    }
    if (zk_json_pick_config_number(svr, "hPeriod", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value < 0 || value > 1800)
        {
            return 3;
        }
        config->hPeriod = value;
    }
    if (zk_json_pick_config_number(svr, "tPeriod", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value < 0 || value > 7200)
        {
            return 3;
        }
        config->tPeriod = value;
    }
    return 0;
}

static int zk_validate_rtc_config(cJSON *rtc, RtcTime_t *rtc_value)
{
    const char *time_text;

    if (rtc == NULL || rtc_value == NULL)
    {
        return 2;
    }
    time_text = zk_json_get_rtc_time_text(rtc);
    if (time_text == NULL)
    {
        return 2;
    }
    if (zk_parse_rtc_text(time_text, rtc_value) == BOOL_FALSE)
    {
        return 3;
    }
    return 0;
}

boolean_en zk_handle_property_read(cJSON *root, const zk_message_header_t *header)
{
    cJSON *dt_in;
    cJSON *props;
    cJSON *root_out;
    cJSON *dt_out;
    cJSON *item;
    int index;

    if (root == NULL || header == NULL)
    {
        return BOOL_FALSE;
    }
    if (strcmp(header->sv, ZK_SV_PROP) != 0 || strcmp(header->ct, ZK_CT_READ) != 0)
    {
        return BOOL_FALSE;
    }

    dt_in = cJSON_GetObjectItem(root, "DT");
    props = (dt_in != NULL) ? cJSON_GetObjectItem(dt_in, "props") : NULL;
    if (props == NULL || !cJSON_IsArray(props))
    {
        zk_publish_simple_response(header, 5);
        return BOOL_TRUE;
    }
    if (cJSON_GetArraySize(props) <= 0)
    {
        zk_publish_simple_response(header, 7);
        return BOOL_TRUE;
    }

    root_out = zk_create_root_from_header(header, 1, 0);
    if (root_out == NULL)
    {
        zk_publish_simple_response(header, 12);
        return BOOL_TRUE;
    }
    dt_out = zk_cjson_create_tx_object("DT");
    if (dt_out == NULL)
    {
        cJSON_Delete(root_out);
        zk_publish_simple_response(header, 12);
        return BOOL_TRUE;
    }
    cJSON_AddItemToObject(root_out, "DT", dt_out);

    for (index = 0; index < cJSON_GetArraySize(props); ++index)
    {
        item = cJSON_GetArrayItem(props, index);
        if (item == NULL || !cJSON_IsString(item) || item->valuestring == NULL)
        {
            cJSON_Delete(root_out);
            zk_publish_simple_response(header, 2);
            return BOOL_TRUE;
        }
        if (zk_add_property_to_dt(dt_out, item->valuestring) != 0)
        {
            cJSON_Delete(root_out);
            zk_publish_simple_response(header, 1);
            return BOOL_TRUE;
        }
    }

    if (zk_send_json_root(root_out, NULL) != 0)
    {
        zk_schedule_simple_response(header, 12);
    }
    cJSON_Delete(root_out);
    return BOOL_TRUE;
}

boolean_en zk_handle_property_write(cJSON *root, const zk_message_header_t *header)
{
    cJSON *dt;
    cJSON *gis;
    cJSON *dim;
    cJSON *sense;
    cJSON *svr;
    cJSON *rtc;
    zk_device_config_t candidate;
    RtcTime_t rtc_value;
    int err;
    int handled;
    int persist_needed;
    int reset_period_timers;
    int update_rtc;

    if (root == NULL || header == NULL)
    {
        return BOOL_FALSE;
    }
    if (strcmp(header->sv, ZK_SV_PROP) != 0 || strcmp(header->ct, ZK_CT_WRITE) != 0)
    {
        return BOOL_FALSE;
    }

    dt = cJSON_GetObjectItem(root, "DT");
    if (dt == NULL || !cJSON_IsObject(dt))
    {
        zk_publish_simple_response(header, 5);
        return BOOL_TRUE;
    }

    gis = cJSON_GetObjectItem(dt, "Gis");
    dim = cJSON_GetObjectItem(dt, "Dim");
    sense = cJSON_GetObjectItem(dt, "Sense");
    svr = cJSON_GetObjectItem(dt, "Svr");
    rtc = cJSON_GetObjectItem(dt, "RTC");

    handled = 0;
    if (gis != NULL)
    {
        handled = 1;
    }
    if (dim != NULL)
    {
        handled = 1;
    }
    if (sense != NULL)
    {
        handled = 1;
    }
    if (svr != NULL)
    {
        handled = 1;
    }
    if (rtc != NULL)
    {
        handled = 1;
    }
    if (handled == 0)
    {
        zk_publish_simple_response(header, 1);
        return BOOL_TRUE;
    }

    candidate = zk_dev_cfg;
    persist_needed = (gis != NULL || dim != NULL || sense != NULL || svr != NULL) ? 1 : 0;
    reset_period_timers = (svr != NULL) ? 1 : 0;
    update_rtc = (rtc != NULL) ? 1 : 0;

    if (gis != NULL && (err = zk_apply_gis_config(gis, &candidate)) != 0)
    {
        zk_publish_simple_response(header, err);
        return BOOL_TRUE;
    }
    if (dim != NULL && (err = zk_apply_dim_config(dim, &candidate)) != 0)
    {
        zk_publish_simple_response(header, err);
        return BOOL_TRUE;
    }
    if (sense != NULL && (err = zk_apply_sense_config(sense, &candidate)) != 0)
    {
        zk_publish_simple_response(header, err);
        return BOOL_TRUE;
    }
    if (svr != NULL && (err = zk_apply_svr_config(svr, &candidate)) != 0)
    {
        zk_publish_simple_response(header, err);
        return BOOL_TRUE;
    }
    if (rtc != NULL && (err = zk_validate_rtc_config(rtc, &rtc_value)) != 0)
    {
        zk_publish_simple_response(header, err);
        return BOOL_TRUE;
    }

    if (persist_needed != 0)
    {
        if (zk_property_flash_store_config(&candidate) == BOOL_FALSE)
        {
            zk_publish_simple_response(header, ZK_FLASH_SAVE_ERROR);
            return BOOL_TRUE;
        }
        zk_dev_cfg = candidate;
        if (reset_period_timers != 0)
        {
            zk_reset_config_period_timers();
        }
    }
    if (update_rtc != 0)
    {
        zk_set_local_rtc(&rtc_value);
    }

    zk_publish_simple_response(header, 0);
    return BOOL_TRUE;
}

static boolean_en zk_json_pick_number_field(cJSON *object, const char *key, int *value, int *err)
{
    cJSON *node;

    if (object == NULL || key == NULL)
    {
        return BOOL_FALSE;
    }
    node = cJSON_GetObjectItem(object, key);
    if (node == NULL)
    {
        return BOOL_FALSE;
    }
    if (!cJSON_IsNumber(node))
    {
        *err = 2;
        return BOOL_TRUE;
    }
    *value = node->valueint;
    *err = 0;
    return BOOL_TRUE;
}

static boolean_en zk_pick_brightness_checked(cJSON *object, int *value, int *err)
{
    static const char *keys[] = {"bri", "Bri", "brightness", "level", "bl", "dim"};
    cJSON *dim;
    cJSON *first;
    int index;

    *err = 0;
    for (index = 0; index < (int)(sizeof(keys) / sizeof(keys[0])); ++index)
    {
        if (zk_json_pick_number_field(object, keys[index], value, err) == BOOL_TRUE)
        {
            return BOOL_TRUE;
        }
    }

    dim = cJSON_GetObjectItem(object, "Dim");
    if (dim != NULL)
    {
        if (!cJSON_IsArray(dim))
        {
            *err = 2;
            return BOOL_TRUE;
        }
        first = cJSON_GetArrayItem(dim, 0);
        if (first != NULL)
        {
            if (!cJSON_IsObject(first))
            {
                *err = 2;
                return BOOL_TRUE;
            }
            return zk_pick_brightness_checked(first, value, err);
        }
    }
    return BOOL_FALSE;
}

static boolean_en zk_pick_switch_checked(cJSON *object, int *value, int *err)
{
    static const char *keys[] = {"sw", "switch", "onoff", "power", "on", "light"};
    cJSON *node;
    cJSON *state;
    int index;

    *err = 0;
    for (index = 0; index < (int)(sizeof(keys) / sizeof(keys[0])); ++index)
    {
        node = cJSON_GetObjectItem(object, keys[index]);
        if (node == NULL)
        {
            continue;
        }
        if (cJSON_IsBool(node))
        {
            *value = cJSON_IsTrue(node) ? 1 : 0;
            return BOOL_TRUE;
        }
        if (!cJSON_IsNumber(node))
        {
            *err = 2;
            return BOOL_TRUE;
        }
        *value = node->valueint;
        return BOOL_TRUE;
    }

    state = cJSON_GetObjectItem(object, "state");
    if (state != NULL)
    {
        if (!cJSON_IsString(state) || state->valuestring == NULL)
        {
            *err = 2;
            return BOOL_TRUE;
        }
        if (strcmp(state->valuestring, "on") == 0 || strcmp(state->valuestring, "ON") == 0)
        {
            *value = 1;
            return BOOL_TRUE;
        }
        if (strcmp(state->valuestring, "off") == 0 || strcmp(state->valuestring, "OFF") == 0)
        {
            *value = 0;
            return BOOL_TRUE;
        }
        *err = 3;
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static int zk_pick_control_target(cJSON *control, int *target)
{
    int brightness;
    int switch_value;
    int err;
    boolean_en has_brightness;
    boolean_en has_switch;

    has_brightness = zk_pick_brightness_checked(control, &brightness, &err);
    if (err != 0)
    {
        return err;
    }
    has_switch = zk_pick_switch_checked(control, &switch_value, &err);
    if (err != 0)
    {
        return err;
    }
    if (has_switch == BOOL_TRUE && (switch_value < 0 || switch_value > 1))
    {
        return 3;
    }
    if (has_brightness == BOOL_TRUE && (brightness < 0 || brightness > 100))
    {
        return 3;
    }

    if (has_switch == BOOL_TRUE && switch_value == 0)
    {
        *target = 0;
        return 0;
    }
    if (has_brightness == BOOL_TRUE)
    {
        *target = brightness;
        return 0;
    }
    if (has_switch == BOOL_TRUE)
    {
        *target = (zk_last_brightness == 0) ? 100 : zk_last_brightness;
        return 0;
    }
    return 5;
}

static int zk_validate_cnctrl_item(cJSON *item, int *target, int *last_sec)
{
    cJSON *node;
    int err;
    int cns;

    if (item == NULL || !cJSON_IsObject(item))
    {
        return 2;
    }

    node = cJSON_GetObjectItem(item, "cns");
    if (node == NULL)
    {
        return 5;
    }
    if (!cJSON_IsNumber(node))
    {
        return 2;
    }
    cns = node->valueint;
    if (cns != ZK_CNCTRL_SUPPORTED_CNS)
    {
        return 6;
    }

    err = zk_pick_control_target(item, target);
    if (err != 0)
    {
        return err;
    }

    *last_sec = 0;
    node = cJSON_GetObjectItem(item, "last");
    if (node != NULL)
    {
        if (!cJSON_IsNumber(node))
        {
            return 2;
        }
        if (node->valueint < 0 ||
            (uint32)node->valueint > (ZK_CNCTRL_LAST_MAX_SEC / 60UL))
        {
            return 3;
        }
        *last_sec = node->valueint * 60;
    }
    return 0;
}

static void zk_apply_brightness(int brightness)
{
    if (brightness < 0)
    {
        brightness = 0;
    }
    if (brightness > 100)
    {
        brightness = 100;
    }
    if (brightness > 0)
    {
        zk_last_brightness = (u8)brightness;
    }
    dim_level = (u32)brightness;
    dim_ready();
    zk_notify_state_changed();
}

void zk_apply_plan_brightness(int brightness)
{
    zk_apply_brightness(brightness);
}

boolean_en zk_handle_control_message(cJSON *root, const zk_message_header_t *header)
{
    cJSON *dt;
    cJSON *cn_ctrl;
    cJSON *item;
    cJSON *restore;
    cJSON *do_node;
    int target;
    int last_sec;
    int restore_type;
    int index;
    int item_count;
    int restore_brightness;
    int err;
    zk_device_config_t restore_config;

    if (root == NULL || header == NULL)
    {
        return BOOL_FALSE;
    }
    if (strcmp(header->sv, ZK_SV_CTRL) != 0 || strcmp(header->ct, ZK_CT_WRITE) != 0)
    {
        return BOOL_FALSE;
    }

    dt = cJSON_GetObjectItem(root, "DT");
    if (dt == NULL || !cJSON_IsObject(dt))
    {
        zk_publish_simple_response(header, 5);
        return BOOL_TRUE;
    }

    do_node = cJSON_GetObjectItem(dt, "DO");
    if (do_node != NULL && cJSON_IsString(do_node) && do_node->valuestring != NULL)
    {
        if (strcmp(do_node->valuestring, "patrol") == 0)
        {
            zk_publish_simple_response(header, 0);
            zk_patrol_report_pending = BOOL_TRUE;
            return BOOL_TRUE;
        }
        if (strcmp(do_node->valuestring, "reboot") == 0)
        {
            zk_publish_simple_response(header, 0);
            zk_reboot_pending = BOOL_TRUE;
            zk_reboot_tick = Timer_GetTickCount();
            return BOOL_TRUE;
        }
        zk_publish_simple_response(header, 1);
        return BOOL_TRUE;
    }

    restore = cJSON_GetObjectItem(dt, "restore");
    if (restore != NULL)
    {
        if (!cJSON_IsNumber(restore))
        {
            zk_publish_simple_response(header, 2);
            return BOOL_TRUE;
        }
        restore_type = restore->valueint;
        if (restore_type < 0 || restore_type > 6)
        {
            zk_publish_simple_response(header, 3);
            return BOOL_TRUE;
        }
        if (restore_type == 2 || restore_type == 3 || restore_type == 4)
        {
            zk_publish_simple_response(header, 1);
            return BOOL_TRUE;
        }
        if (restore_type == 0 || restore_type == 1 || restore_type == 6)
        {
            zk_device_config_set_defaults(&restore_config);
            if (zk_property_flash_store_config(&restore_config) == BOOL_FALSE)
            {
                zk_publish_simple_response(header, ZK_FLASH_SAVE_ERROR);
                return BOOL_TRUE;
            }
            zk_cancel_control_restore();
            zk_dev_cfg = restore_config;
            zk_reset_config_period_timers();
        }
        if (restore_type == 5)
        {
            sys_data.ac_EnergyP = 0;
            sys_data.today_Energy = 0;
        }
        zk_publish_simple_response(header, 0);
        return BOOL_TRUE;
    }

    cn_ctrl = cJSON_GetObjectItem(dt, "cnCtrl");
    if (cn_ctrl != NULL)
    {
        if (!cJSON_IsArray(cn_ctrl))
        {
            zk_publish_simple_response(header, 2);
            return BOOL_TRUE;
        }
        item_count = cJSON_GetArraySize(cn_ctrl);
        if (item_count <= 0)
        {
            zk_publish_simple_response(header, 7);
            return BOOL_TRUE;
        }

        for (index = 0; index < item_count; ++index)
        {
            item = cJSON_GetArrayItem(cn_ctrl, index);
            err = zk_validate_cnctrl_item(item, &target, &last_sec);
            if (err != 0)
            {
                zk_publish_simple_response(header, err);
                return BOOL_TRUE;
            }
        }

        restore_brightness = zk_get_current_brightness();
        for (index = 0; index < item_count; ++index)
        {
            item = cJSON_GetArrayItem(cn_ctrl, index);
            err = zk_validate_cnctrl_item(item, &target, &last_sec);
            if (err != 0)
            {
                zk_publish_simple_response(header, err);
                return BOOL_TRUE;
            }
            zk_apply_brightness(target);
            zk_schedule_control_restore(restore_brightness, last_sec);
        }
        zk_publish_simple_response(header, 0);
        return BOOL_TRUE;
    }

    err = zk_pick_control_target(dt, &target);
    if (err == 0)
    {
        zk_cancel_control_restore();
        zk_apply_brightness(target);
        zk_publish_simple_response(header, 0);
        return BOOL_TRUE;
    }

    zk_publish_simple_response(header, err == 5 ? 1 : err);
    return BOOL_TRUE;
}

boolean_en zk_handle_request_message(cJSON *root, const zk_message_header_t *header)
{
    cJSON *dt;
    cJSON *tm_cali;

    if (root == NULL || header == NULL)
    {
        return BOOL_FALSE;
    }
    if (strcmp(header->sv, ZK_SV_RQST) != 0 || strcmp(header->ct, ZK_CT_READ) != 0)
    {
        return BOOL_FALSE;
    }

    dt = cJSON_GetObjectItem(root, "DT");
    tm_cali = (dt != NULL) ? cJSON_GetObjectItem(dt, "TmCali") : NULL;
    if (tm_cali != NULL)
    {
        (void)zk_apply_server_time_text(zk_json_get_rtc_time_text(tm_cali));
        zk_time_request_tick = Timer_GetTickCount();
        return BOOL_TRUE;
    }
    zk_publish_simple_response(header, 1);
    return BOOL_TRUE;
}

boolean_en zk_handle_ota_message(cJSON *root, const zk_message_header_t *header)
{
    cJSON *dt;
    cJSON *url;
    char file_name[64];

    if (root == NULL || header == NULL)
    {
        return BOOL_FALSE;
    }
    if (strcmp(header->sv, ZK_SV_OTA) != 0 || strcmp(header->ct, ZK_CT_WRITE) != 0)
    {
        return BOOL_FALSE;
    }

    dt = cJSON_GetObjectItem(root, "DT");
    url = (dt != NULL) ? cJSON_GetObjectItem(dt, "url") : NULL;
    if (url == NULL || !cJSON_IsString(url) || url->valuestring == NULL)
    {
        zk_publish_simple_response(header, 5);
        return BOOL_TRUE;
    }
    if (strncmp(url->valuestring, "http://", 7) != 0)
    {
        zk_publish_simple_response(header, 91);
        return BOOL_TRUE;
    }
    if (strlen(url->valuestring) >= sizeof(zk_ota_url))
    {
        zk_publish_simple_response(header, 91);
        return BOOL_TRUE;
    }
    if (zk_ota_is_busy() == BOOL_TRUE)
    {
        zk_publish_simple_response(header, 12);
        return BOOL_TRUE;
    }
    if (zk_ota_extract_filename(url->valuestring, file_name, sizeof(file_name)) == BOOL_FALSE)
    {
        zk_publish_simple_response(header, 91);
        return BOOL_TRUE;
    }

    strncpy(zk_last_ota_id, header->id, sizeof(zk_last_ota_id) - 1);
    zk_last_ota_id[sizeof(zk_last_ota_id) - 1] = '\0';
    zk_ota_set_url(url->valuestring);
    memset(firm_name_buffer, 0, 256);
    strncpy(firm_name_buffer, file_name, 255);
    set_OTA_ENABLE();
    zk_publish_simple_response(header, 0);
    return BOOL_TRUE;
}
