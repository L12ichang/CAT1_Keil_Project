#include "mqtt_zk_protocol.h"
#include "zk_runtime_stats.h"
#include "zk_alarm.h"
#include "zk_property.h"
#include "zk_protocol_internal.h"
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
#include "flash_address_assignment.h"
#include "ntc.h"
#include "main.h"
#include "sys_calibration_mqtt.h"
#include <stdio.h>
#include <string.h>

extern uint8 IMEI[18];
extern uint8 simCardICCID[22];
extern u8 online;
extern char firm_name_buffer[256];


static zk_mqtt_config_t zk_mqtt_cfg;
static zk_login_state_en zk_login_state = ZK_LOGIN_STATE_IDLE;
static uint32 zk_login_tick = 0;
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
static boolean_en zk_ota_progress_pending = BOOL_FALSE;
static uint32 zk_ota_progress_value = 0;
static boolean_en zk_ota_error_pending = BOOL_FALSE;
static int zk_ota_error_code = 0;
static boolean_en zk_reboot_pending = BOOL_FALSE;
static uint32 zk_reboot_tick = 0;

/* ===================== 信号强度查询(AT+QENG运行时刷新) ===================== */
typedef enum
{
    ZK_SIGNAL_QUERY_IDLE = 0,        /* 空闲,等待周期或命令触发 */
    ZK_SIGNAL_QUERY_WAIT_FINISH,     /* QENG已发送,等待AT命令机完成 */
} zk_signal_query_state_en;

static zk_signal_query_state_en zk_signal_query_state = ZK_SIGNAL_QUERY_IDLE;
static uint32 zk_signal_query_tick = 0;
/* 按需 SignalQuery 命令的延迟响应(保存请求头深拷贝) */
static boolean_en zk_signal_query_cmd_pending = BOOL_FALSE;
static zk_message_header_t zk_signal_query_cmd_header;
static boolean_en zk_login_time_sync_pending = BOOL_FALSE;
static boolean_en zk_startup_time_force_pending = BOOL_FALSE;
static boolean_en zk_startup_time_sync_done = BOOL_FALSE;
static boolean_en zk_control_restore_pending = BOOL_FALSE;
static uint32 zk_control_restore_tick = 0;
static uint32 zk_control_restore_delay_ms = 0;
static int zk_control_restore_brightness = 0;

/* ===================== 心跳健康监督（QoS1 Broker发布闭环） ===================== */
typedef enum
{
    ZK_HEARTBEAT_MONITOR_IDLE = 0,
    ZK_HEARTBEAT_MONITOR_WAIT_SUBMIT,
    ZK_HEARTBEAT_MONITOR_WAIT_ACK
} zk_heartbeat_monitor_state_en;

#define ZK_HEARTBEAT_MONITOR_PERIOD_MS       (60UL * 1000UL)
#define ZK_HEARTBEAT_SUBMIT_GRACE_MS         (15UL * 1000UL)
#define ZK_HEARTBEAT_ACK_WAIT_MS             (45UL * 1000UL)
#define ZK_HEARTBEAT_FAIL_LIMIT              3U
#define ZK_CYCLIC_REPORT_RETRY_DELAY_MS      (5UL * 1000UL)

static zk_heartbeat_monitor_state_en zk_hb_monitor_state = ZK_HEARTBEAT_MONITOR_IDLE;
static u8 zk_hb_consecutive_fail_count = 0;
static u32 zk_hb_period_tick = 0;
static u32 zk_hb_state_tick = 0;
static u32 zk_hb_pub_success_snapshot = 0;
static u32 zk_hb_pub_fail_snapshot = 0;
static u32 zk_hb_pub_timeout_snapshot = 0;
static u32 zk_broker_ack_snapshot = 0;
static u32 zk_hb_send_count = 0;
static u32 zk_hb_success_count = 0;
static u32 zk_hb_fail_count = 0;

/* 周期上报失败重试控制：首次失败后进入短重试窗口，到点最多重试一次，
   仍失败则放弃本次周期上报并清理TX池，推进周期计时，避免主循环每圈卡在此处饿死后续校时 */
static boolean_en zk_cyclic_report_retry_pending = BOOL_FALSE;
static uint32 zk_cyclic_report_retry_tick = 0;

/* 校时请求失败重试控制：与周期上报对称，失败最多重试一次，仍失败则放弃并推进周期 */
static boolean_en zk_time_request_retry_pending = BOOL_FALSE;
static uint32 zk_time_request_retry_tick = 0;

#define ZK_CNCTRL_SUPPORTED_CNS 1
#define ZK_CNCTRL_LAST_MAX_SEC  (24UL * 60UL * 60UL)
#define ZK_CHANGE_REPORT_SETTLE_MS 3000UL
#define ZK_RESPONSE_QUEUE_SIZE 2U
#define ZK_LOGIN_ACK_RECONNECT_THRESHOLD 2U
#define ZK_LOGIN_PUBLISH_TIMEOUT_MS (45UL * 1000UL)
#define ZK_OTA_ACK_PUBLISH_TIMEOUT_MS (45UL * 1000UL)
#define ZK_OTA_ACK_RETRY_LIMIT 3U
#define ZK_OTA_REPORT_FLASH_MAGIC 0x5A4B4F54UL
#define ZK_OTA_REPORT_FLASH_VERSION 1U
#define ZK_OTA_REPORT_FLASH_OFFSET 0x300UL
#define ZK_OTA_REPORT_FLASH_MAIN_ADDR CAT1_FLASH_OTA_REPORT_MAIN_START
#define ZK_OTA_REPORT_FLASH_BACKUP_ADDR CAT1_FLASH_OTA_REPORT_BACKUP_START
#define ZK_OTA_REPORT_SUCCESS_PROGRESS 100U
#define ZK_OTA_REPORT_RETRY_DELAY_MS (10UL * 1000UL)
#define ZK_OTA_REPORT_PUBLISH_TIMEOUT_MS (45UL * 1000UL)
#define ZK_DEFAULT_TIMEZONE_HOURS 8
#define ZK_SECONDS_PER_HOUR 3600L
#define ZK_SECONDS_PER_DAY 86400L
#define ZK_TIME_SYNC_THRESHOLD_SECONDS 30L
#define ZK_OTA_STATIC_ASSERT_CONCAT_(a, b) a##b
#define ZK_OTA_STATIC_ASSERT_CONCAT(a, b) ZK_OTA_STATIC_ASSERT_CONCAT_(a, b)
#define ZK_OTA_STATIC_ASSERT(cond) typedef char ZK_OTA_STATIC_ASSERT_CONCAT(zk_ota_static_assert_, __LINE__)[(cond) ? 1 : -1]
static void zk_apply_brightness(int brightness);
cJSON *zk_cjson_create_tx_object(const char *context);
cJSON *zk_create_root_from_header(const zk_message_header_t *header, int with_er, int er_code);
int zk_send_json_root(cJSON *root, const char *topic);

typedef struct
{
    zk_message_header_t header;
    int err_code;
} zk_response_pending_item_t;

typedef enum
{
    ZK_OTA_ACK_STATE_IDLE = 0,
    ZK_OTA_ACK_STATE_SEND,
    ZK_OTA_ACK_STATE_WAIT_PUBLISH,
} zk_ota_ack_state_en;

typedef enum
{
    ZK_OTA_REPORT_STATE_EMPTY = 0,
    ZK_OTA_REPORT_STATE_PENDING = 1,
    ZK_OTA_REPORT_STATE_VERIFIED = 2,
    ZK_OTA_REPORT_STATE_REPORTED = 3,
} zk_ota_report_state_en;

typedef enum
{
    ZK_OTA_SUCCESS_REPORT_IDLE = 0,
    ZK_OTA_SUCCESS_REPORT_WAIT_PUBLISH,
} zk_ota_success_report_state_en;

typedef struct
{
    u32 magic;
    u16 version;
    u16 size;
    u32 seq;
    u32 state;
    char ota_id[8];
    u32 url_hash;
    u32 image_checksum;
    u32 image_size;
    u16 device_type;
    u16 reserved;
    u32 retry_count;
    u32 checksum;
} zk_ota_report_flash_record_t;

ZK_OTA_STATIC_ASSERT((ZK_OTA_REPORT_FLASH_OFFSET + sizeof(zk_ota_report_flash_record_t)) <= FLASH_PAGE_SIZE);

static zk_response_pending_item_t zk_response_queue[ZK_RESPONSE_QUEUE_SIZE];
static u8 zk_response_queue_head = 0;
static u8 zk_response_queue_count = 0;
static u32 zk_response_queue_drop_count = 0;
static u8 zk_login_ack_timeout_count = 0;
static u32 zk_login_wait_pub_success_count = 0;
static u32 zk_login_wait_pub_fail_count = 0;
static u32 zk_login_wait_pub_timeout_count = 0;
static zk_ota_ack_state_en zk_ota_ack_state = ZK_OTA_ACK_STATE_IDLE;
static zk_message_header_t zk_ota_ack_header;
static u32 zk_ota_ack_pub_success_count = 0;
static u32 zk_ota_ack_pub_fail_count = 0;
static u32 zk_ota_ack_pub_timeout_count = 0;
static u32 zk_ota_ack_tick = 0;
static u8 zk_ota_ack_retry_count = 0;
static zk_ota_success_report_state_en zk_ota_success_report_state = ZK_OTA_SUCCESS_REPORT_IDLE;
static zk_ota_report_flash_record_t zk_ota_success_report_record;
static u32 zk_ota_success_pub_success_count = 0;
static u32 zk_ota_success_pub_fail_count = 0;
static u32 zk_ota_success_pub_timeout_count = 0;
static u32 zk_ota_success_report_tick = 0;
static u32 zk_ota_success_retry_tick = 0;

static u16 zk_ota_report_checksum(zk_ota_report_flash_record_t *record)
{
    return crc16_modbus_get((unsigned char *)record,
                            sizeof(*record) - sizeof(record->checksum));
}

static u32 zk_ota_report_url_hash(const char *text)
{
    u32 hash;

    hash = 2166136261UL;
    if (text == NULL)
    {
        return hash;
    }
    while (*text != '\0')
    {
        hash ^= (u8)(*text);
        hash *= 16777619UL;
        ++text;
    }
    return hash;
}

static boolean_en zk_ota_report_record_valid(zk_ota_report_flash_record_t *record)
{
    if (record == NULL)
    {
        return BOOL_FALSE;
    }
    if (record->magic != ZK_OTA_REPORT_FLASH_MAGIC ||
        record->version != ZK_OTA_REPORT_FLASH_VERSION ||
        record->size != sizeof(*record))
    {
        return BOOL_FALSE;
    }
    return ((u16)record->checksum == zk_ota_report_checksum(record)) ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en zk_ota_report_read_record(u32 addr,
                                            zk_ota_report_flash_record_t *record)
{
    hw_flash_read_bytes(addr, (u8 *)record, sizeof(*record));
    return zk_ota_report_record_valid(record);
}

static boolean_en zk_ota_report_read_latest(zk_ota_report_flash_record_t *record)
{
    zk_ota_report_flash_record_t main_record;
    zk_ota_report_flash_record_t backup_record;
    zk_ota_report_flash_record_t *selected;
    boolean_en main_ok;
    boolean_en backup_ok;

    main_ok = zk_ota_report_read_record(ZK_OTA_REPORT_FLASH_MAIN_ADDR, &main_record);
    backup_ok = zk_ota_report_read_record(ZK_OTA_REPORT_FLASH_BACKUP_ADDR, &backup_record);
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
        return BOOL_FALSE;
    }
    if (record != NULL)
    {
        memcpy(record, selected, sizeof(*record));
    }
    return BOOL_TRUE;
}

static boolean_en zk_ota_report_write_record(u32 addr,
                                             zk_ota_report_flash_record_t *record)
{
    hw_flash_write_bytes(addr, (u8 *)record, sizeof(*record));
    return user_flash_check(addr, (u8 *)record, sizeof(*record));
}

static boolean_en zk_ota_report_store(zk_ota_report_flash_record_t *record)
{
    zk_ota_report_flash_record_t latest;
    u32 next_seq;
    boolean_en main_ok;
    boolean_en backup_ok;

    if (record == NULL)
    {
        return BOOL_FALSE;
    }
    next_seq = 1U;
    if (zk_ota_report_read_latest(&latest) == BOOL_TRUE)
    {
        next_seq = latest.seq + 1U;
        if (next_seq == 0U)
        {
            next_seq = 1U;
        }
    }
    record->magic = ZK_OTA_REPORT_FLASH_MAGIC;
    record->version = ZK_OTA_REPORT_FLASH_VERSION;
    record->size = (u16)sizeof(*record);
    record->seq = next_seq;
    record->checksum = zk_ota_report_checksum(record);
    main_ok = zk_ota_report_write_record(ZK_OTA_REPORT_FLASH_MAIN_ADDR, record);
    backup_ok = zk_ota_report_write_record(ZK_OTA_REPORT_FLASH_BACKUP_ADDR, record);
    return (main_ok == BOOL_TRUE || backup_ok == BOOL_TRUE) ? BOOL_TRUE : BOOL_FALSE;
}

void zk_ota_report_mark_pending(const char *ota_id, const char *url)
{
    zk_ota_report_flash_record_t record;

    zk_ota_success_report_state = ZK_OTA_SUCCESS_REPORT_IDLE;
    zk_ota_success_retry_tick = Timer_GetTickCount();
    memset(&record, 0, sizeof(record));
    record.state = ZK_OTA_REPORT_STATE_PENDING;
    record.url_hash = zk_ota_report_url_hash(url);
    if (ota_id != NULL)
    {
        strncpy(record.ota_id, ota_id, sizeof(record.ota_id) - 1);
    }
    if (zk_ota_report_store(&record) == BOOL_TRUE)
    {
        OTA_LOGI("ota report pending stored id=%s url_hash=0x%08x\r\n",
                 record.ota_id,
                 (unsigned int)record.url_hash);
    }
    else
    {
        OTA_LOGW("ota report pending store failed id=%s\r\n", record.ota_id);
    }
}

void zk_ota_report_mark_verified(u32 checksum, u32 size, u16 device_type)
{
    zk_ota_report_flash_record_t record;

    if (zk_ota_report_read_latest(&record) != BOOL_TRUE ||
        (record.state != ZK_OTA_REPORT_STATE_PENDING &&
         record.state != ZK_OTA_REPORT_STATE_VERIFIED))
    {
        memset(&record, 0, sizeof(record));
        record.state = ZK_OTA_REPORT_STATE_PENDING;
        strncpy(record.ota_id, zk_last_ota_id, sizeof(record.ota_id) - 1);
    }
    record.state = ZK_OTA_REPORT_STATE_VERIFIED;
    record.image_checksum = checksum;
    record.image_size = size;
    record.device_type = device_type;
    if (zk_ota_report_store(&record) == BOOL_TRUE)
    {
        OTA_LOGI("ota report verified stored id=%s checksum=0x%08x size=%u type=0x%04x\r\n",
                 record.ota_id,
                 (unsigned int)checksum,
                 (unsigned int)size,
                 (unsigned int)device_type);
    }
    else
    {
        OTA_LOGW("ota report verified store failed id=%s\r\n", record.ota_id);
    }
}

static boolean_en zk_ota_report_app_matches(zk_ota_report_flash_record_t *record)
{
    u32 app_checksum;
    u32 app_size;
    u16 app_device_type;

    if (record == NULL || record->state != ZK_OTA_REPORT_STATE_VERIFIED)
    {
        return BOOL_FALSE;
    }
    app_checksum = *((__IO u32 *)(APROM_STARTADDR + ADDR_CHECKSUM_OFFSET));
    app_size = *((__IO u32 *)(APROM_STARTADDR + ADDR_SIZE_OFFSET)) & 0x00FFFFFFU;
    app_device_type = *((__IO u16 *)(APROM_STARTADDR + ADDR_TYPE_OFFSET));
    if (app_checksum == record->image_checksum &&
        app_size == record->image_size &&
        app_device_type == record->device_type)
    {
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static int zk_publish_ota_success_now(const char *ota_id)
{
    zk_message_header_t header;
    cJSON *root;
    cJSON *dt;
    const char *topic;

    if (ota_id == NULL || ota_id[0] == '\0')
    {
        return -1;
    }
    topic = zk_mqtt_get_config() != NULL ? zk_mqtt_cfg.pub_topic : NULL;
    if (topic == NULL || topic[0] == '\0')
    {
        return -1;
    }
    zk_fill_message_header(&header, ZK_SV_OTA, ota_id, ZK_CT_PROGRESS);
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
    cJSON_AddNumberToObject(dt, "progress", (double)ZK_OTA_REPORT_SUCCESS_PROGRESS);
    cJSON_AddItemToObject(root, "DT", dt);
    if (zk_send_json_root(root, topic) != 0)
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_Delete(root);
    return 0;
}

static void zk_ota_success_report_retry_later(uint32 now)
{
    zk_ota_success_retry_tick = now;
    zk_ota_success_report_state = ZK_OTA_SUCCESS_REPORT_IDLE;
    if (zk_ota_success_report_record.retry_count < 0xFFFFFFFFUL)
    {
        zk_ota_success_report_record.retry_count++;
        (void)zk_ota_report_store(&zk_ota_success_report_record);
    }
}

static boolean_en zk_ota_success_report_process(uint32 now)
{
    zk_ota_report_flash_record_t record;

    if (zk_ota_success_report_state == ZK_OTA_SUCCESS_REPORT_WAIT_PUBLISH)
    {
        if (nb_mqtt_get_publish_success_count() != zk_ota_success_pub_success_count)
        {
            zk_ota_success_report_record.state = ZK_OTA_REPORT_STATE_REPORTED;
            if (zk_ota_report_store(&zk_ota_success_report_record) == BOOL_TRUE)
            {
                OTA_LOGI("ota success report published id=%s\r\n",
                         zk_ota_success_report_record.ota_id);
            }
            else
            {
                OTA_LOGW("ota success report published but clear failed id=%s\r\n",
                         zk_ota_success_report_record.ota_id);
            }
            zk_ota_success_report_state = ZK_OTA_SUCCESS_REPORT_IDLE;
            return BOOL_TRUE;
        }
        if (nb_mqtt_get_publish_fail_count() != zk_ota_success_pub_fail_count ||
            nb_mqtt_get_publish_timeout_count() != zk_ota_success_pub_timeout_count ||
            Timer_PassedDelay(zk_ota_success_report_tick, ZK_OTA_REPORT_PUBLISH_TIMEOUT_MS))
        {
            OTA_LOGW("ota success report publish retry id=%s\r\n",
                     zk_ota_success_report_record.ota_id);
            zk_ota_success_report_retry_later(now);
            return BOOL_TRUE;
        }
        return BOOL_TRUE;
    }

    if (Timer_PassedDelay(zk_ota_success_retry_tick, ZK_OTA_REPORT_RETRY_DELAY_MS) == BOOL_FALSE)
    {
        return BOOL_FALSE;
    }
    if (zk_ota_report_read_latest(&record) != BOOL_TRUE ||
        record.state != ZK_OTA_REPORT_STATE_VERIFIED)
    {
        return BOOL_FALSE;
    }
    if (zk_ota_report_app_matches(&record) != BOOL_TRUE)
    {
        OTA_LOGW("ota success report waiting app match id=%s checksum=0x%08x size=%u type=0x%04x\r\n",
                 record.ota_id,
                 (unsigned int)record.image_checksum,
                 (unsigned int)record.image_size,
                 (unsigned int)record.device_type);
        zk_ota_success_retry_tick = now;
        return BOOL_FALSE;
    }
    if (pubsend_state_idle() == BOOL_FALSE)
    {
        return BOOL_TRUE;
    }
    if (zk_publish_ota_success_now(record.ota_id) == 0)
    {
        memcpy(&zk_ota_success_report_record, &record, sizeof(record));
        zk_ota_success_pub_success_count = nb_mqtt_get_publish_success_count();
        zk_ota_success_pub_fail_count = nb_mqtt_get_publish_fail_count();
        zk_ota_success_pub_timeout_count = nb_mqtt_get_publish_timeout_count();
        zk_ota_success_report_tick = now;
        zk_ota_success_report_state = ZK_OTA_SUCCESS_REPORT_WAIT_PUBLISH;
        OTA_LOGI("ota success report started id=%s\r\n", record.ota_id);
        return BOOL_TRUE;
    }
    memcpy(&zk_ota_success_report_record, &record, sizeof(record));
    zk_ota_success_report_retry_later(now);
    return BOOL_TRUE;
}

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

cJSON *zk_cjson_create_tx_object(const char *context)
{
    cJSON *object;

    object = cJSON_CreateObject();
    if (object == NULL)
    {
        zk_cjson_log_tx_pool_exhausted(context);
    }
    return object;
}

cJSON *zk_cjson_create_tx_array(const char *context)
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
void zk_get_time_text(char *buf, int buf_size)
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

static int zk_days_in_month(int year, int month)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (month < 1 || month > 12)
    {
        return 0;
    }
    if (month == 2 && zk_is_leap_year(year))
    {
        return 29;
    }
    return days[month - 1];
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

static boolean_en zk_unix_seconds_to_rtc(long seconds, RtcTime_t *rtc)
{
    long days;
    long second_of_day;
    int year;
    int month;
    int year_days;
    int month_days;

    if (rtc == NULL || seconds < 0)
    {
        return BOOL_FALSE;
    }

    days = seconds / ZK_SECONDS_PER_DAY;
    second_of_day = seconds % ZK_SECONDS_PER_DAY;

    year = 1970;
    while (1)
    {
        year_days = zk_is_leap_year(year) ? 366 : 365;
        if (days < year_days)
        {
            break;
        }
        days -= year_days;
        ++year;
        if (year > 2099)
        {
            return BOOL_FALSE;
        }
    }

    month = 1;
    while (month <= 12)
    {
        month_days = zk_days_in_month(year, month);
        if (days < month_days)
        {
            break;
        }
        days -= month_days;
        ++month;
    }
    if (month > 12)
    {
        return BOOL_FALSE;
    }

    memset(rtc, 0, sizeof(*rtc));
    rtc->year = (u16)year;
    rtc->mon = (u8)month;
    rtc->day = (u8)(days + 1L);
    rtc->hour = (u8)(second_of_day / ZK_SECONDS_PER_HOUR);
    second_of_day %= ZK_SECONDS_PER_HOUR;
    rtc->min = (u8)(second_of_day / 60L);
    rtc->sec = (u8)(second_of_day % 60L);
    rtc->week = (u8)(GetWeek(rtc->year, rtc->mon, rtc->day) + 1U);
    rtc->ready = BOOL_TRUE;
    return BOOL_TRUE;
}

static long zk_local_timezone_offset_seconds(void)
{
    const zk_device_config_t *cfg;
    int zone;

    cfg = zk_device_config_get();
    zone = ZK_DEFAULT_TIMEZONE_HOURS;
    if (cfg != NULL && cfg->zone >= -12 && cfg->zone <= 12)
    {
        zone = cfg->zone;
    }
    return (long)zone * ZK_SECONDS_PER_HOUR;
}

static boolean_en zk_utc_rtc_to_local_rtc(const RtcTime_t *utc_time, RtcTime_t *local_time)
{
    long utc_seconds;
    long local_seconds;

    if (utc_time == NULL || local_time == NULL)
    {
        return BOOL_FALSE;
    }

    utc_seconds = zk_rtc_to_unix_seconds(utc_time);
    local_seconds = utc_seconds + zk_local_timezone_offset_seconds();
    return zk_unix_seconds_to_rtc(local_seconds, local_time);
}

boolean_en zk_parse_rtc_text(const char *text, RtcTime_t *rtc)
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
    if (mo < 1U ||
        mo > 12U ||
        d < 1U ||
        d > (unsigned int)zk_days_in_month((int)y, (int)mo) ||
        h > 23U ||
        mi > 59U ||
        s > 59U)
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

const char *zk_json_get_rtc_time_text(cJSON *node)
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

void zk_set_local_rtc(const RtcTime_t *rtc)
{
    if (rtc == NULL)
    {
        return;
    }
    apprtc_RtcTime = *rtc;
    SetTime();
}

static boolean_en zk_apply_server_time_text_ex(const char *time_text, boolean_en force_apply)
{
    RtcTime_t server_utc_time;
    RtcTime_t server_local_time;
    long server_local_seconds;
    long local_seconds;
    long diff_seconds;

    if (time_text == NULL || time_text[0] == '\0')
    {
        printf("[RTC] server time text empty, skip sync\r\n");
        return BOOL_FALSE;
    }
    printf("[RTC] server UTC TM: %s\r\n", time_text);
    if (zk_parse_rtc_text(time_text, &server_utc_time) == BOOL_FALSE)
    {
        printf("[RTC] parse server time failed, format mismatch\r\n");
        return BOOL_FALSE;
    }
    if (zk_utc_rtc_to_local_rtc(&server_utc_time, &server_local_time) == BOOL_FALSE)
    {
        printf("[RTC] convert server UTC time failed\r\n");
        return BOOL_FALSE;
    }
    printf("[RTC] server UTC parsed: %04d-%02d-%02d %02d:%02d:%02d\r\n",
           server_utc_time.year, server_utc_time.mon, server_utc_time.day,
           server_utc_time.hour, server_utc_time.min, server_utc_time.sec);
    printf("[RTC] server local time: %04d-%02d-%02d %02d:%02d:%02d\r\n",
           server_local_time.year, server_local_time.mon, server_local_time.day,
           server_local_time.hour, server_local_time.min, server_local_time.sec);
    if (apprtc_RtcTime.ready == BOOL_TRUE)
    {
        printf("[RTC] local time: %04d-%02d-%02d %02d:%02d:%02d\r\n",
               apprtc_RtcTime.year, apprtc_RtcTime.mon, apprtc_RtcTime.day,
               apprtc_RtcTime.hour, apprtc_RtcTime.min, apprtc_RtcTime.sec);
        server_local_seconds = zk_rtc_to_unix_seconds(&server_local_time);
        local_seconds = zk_rtc_to_unix_seconds(&apprtc_RtcTime);
        diff_seconds = server_local_seconds - local_seconds;
        if (diff_seconds < 0)
        {
            diff_seconds = -diff_seconds;
        }
        printf("[RTC] time diff = %ld seconds\r\n", diff_seconds);
    }
    else
    {
        printf("[RTC] local RTC not ready\r\n");
    }

    if (force_apply == BOOL_TRUE)
    {
        printf("[RTC] startup force sync, apply server local time\r\n");
        zk_set_local_rtc(&server_local_time);
        return BOOL_TRUE;
    }

    if (apprtc_RtcTime.ready != BOOL_TRUE)
    {
        printf("[RTC] apply server local time directly\r\n");
        zk_set_local_rtc(&server_local_time);
        return BOOL_TRUE;
    }

    if (diff_seconds > ZK_TIME_SYNC_THRESHOLD_SECONDS)
    {
        printf("[RTC] diff > %lds, apply server local time\r\n", ZK_TIME_SYNC_THRESHOLD_SECONDS);
        zk_set_local_rtc(&server_local_time);
    }
    else
    {
        printf("[RTC] diff <= %lds, skip sync\r\n", ZK_TIME_SYNC_THRESHOLD_SECONDS);
    }
    return BOOL_TRUE;
}

static boolean_en zk_apply_server_time_text(const char *time_text)
{
    return zk_apply_server_time_text_ex(time_text, BOOL_FALSE);
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

static void zk_sync_online_period_timers(uint32 now)
{
    zk_report_tick = now;
    zk_time_request_tick = now;
    /* 信号查询与上报周期对齐:同一基准起算,保证每次上报前信号已刷新 */
    zk_signal_query_tick = now;
    zk_signal_query_state = ZK_SIGNAL_QUERY_IDLE;
    zk_signal_query_cmd_pending = BOOL_FALSE;
    /* 心跳监督独立固定60s周期，登录成功/配置恢复时重新起算 */
    zk_hb_monitor_state = ZK_HEARTBEAT_MONITOR_IDLE;
    zk_hb_period_tick = now;
    zk_broker_ack_snapshot = nb_mqtt_get_publish_success_count();
}

void zk_reset_config_period_timers(void)
{
    zk_sync_online_period_timers(Timer_GetTickCount());
}

static boolean_en zk_is_over_temperature_protecting(void)
{
    int current_temp;

    if (INNRE_TEMP_PRO_EN == 0U || INNRE_TEMP_PRO <= 0)
    {
        return BOOL_FALSE;
    }
    if (driver_temperarure_warn != 0U ||
        sys_temp_over_protect_state == SYS_TEMP_OVER_PROTECT_STATE_OVER)
    {
        return BOOL_TRUE;
    }
    if (Ntctemp.Ntctemp <= 0)
    {
        return BOOL_FALSE;
    }

    current_temp = (Ntctemp.Ntctemp + 5) / 10;
    return (current_temp >= (int)INNRE_TEMP_PRO) ? BOOL_TRUE : BOOL_FALSE;
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

static void zk_ota_ack_clear(void)
{
    zk_ota_ack_state = ZK_OTA_ACK_STATE_IDLE;
    memset(&zk_ota_ack_header, 0, sizeof(zk_ota_ack_header));
    zk_ota_ack_pub_success_count = 0;
    zk_ota_ack_pub_fail_count = 0;
    zk_ota_ack_pub_timeout_count = 0;
    zk_ota_ack_tick = 0;
    zk_ota_ack_retry_count = 0;
}

static void zk_ota_ack_defer_start(const zk_message_header_t *header)
{
    if (header == NULL)
    {
        return;
    }
    memcpy(&zk_ota_ack_header, header, sizeof(zk_ota_ack_header));
    zk_ota_ack_pub_success_count = nb_mqtt_get_publish_success_count();
    zk_ota_ack_pub_fail_count = nb_mqtt_get_publish_fail_count();
    zk_ota_ack_pub_timeout_count = nb_mqtt_get_publish_timeout_count();
    zk_ota_ack_tick = Timer_GetTickCount();
    zk_ota_ack_retry_count = 0;
    zk_ota_ack_state = ZK_OTA_ACK_STATE_SEND;
}

static boolean_en zk_ota_is_busy(void)
{
    if (zk_ota_ack_state != ZK_OTA_ACK_STATE_IDLE)
    {
        return BOOL_TRUE;
    }
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

/** 根据IMEI计算MQTT登录密码：对IMEI分三段做CRC16取反后拼接为12位HEX */
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

/** 初始化MQTT连接配置：加载IMEI、生成ClientId/Username/Password、订阅和发布Topic */
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
    zk_report_tick = 0;
    zk_time_request_tick = 0;
    zk_last_heartbeat_id[0] = '\0';
    zk_last_ota_id[0] = '\0';
    zk_change_report_pending = BOOL_FALSE;
    zk_change_report_tick = 0;
    zk_patrol_report_pending = BOOL_FALSE;
    zk_response_pending = BOOL_FALSE;
    zk_response_queue_head = 0;
    zk_response_queue_count = 0;
    zk_response_queue_drop_count = 0;
    zk_login_ack_timeout_count = 0;
    zk_login_wait_pub_success_count = 0;
    zk_login_wait_pub_fail_count = 0;
    zk_login_wait_pub_timeout_count = 0;
    zk_ota_progress_pending = BOOL_FALSE;
    zk_ota_error_pending = BOOL_FALSE;
    zk_reboot_pending = BOOL_FALSE;
    /* 会话级复位心跳监督：重新从60s后开始第一次健康心跳 */
    zk_hb_monitor_state = ZK_HEARTBEAT_MONITOR_IDLE;
    zk_hb_period_tick = 0;
    zk_hb_state_tick = 0;
    zk_hb_pub_success_snapshot = 0;
    zk_hb_pub_fail_snapshot = 0;
    zk_hb_pub_timeout_snapshot = 0;
    zk_hb_consecutive_fail_count = 0;
    zk_broker_ack_snapshot = nb_mqtt_get_publish_success_count();
    zk_cyclic_report_retry_pending = BOOL_FALSE;
    zk_cyclic_report_retry_tick = 0;
    zk_time_request_retry_pending = BOOL_FALSE;
    zk_time_request_retry_tick = 0;
    zk_login_time_sync_pending = BOOL_FALSE;
    zk_startup_time_force_pending = BOOL_FALSE;
    zk_ota_ack_clear();
    zk_alarm_reset_states();
    /* 会话级复位信号查询:丢弃进行中的查询与延迟响应 */
    zk_signal_query_state = ZK_SIGNAL_QUERY_IDLE;
    zk_signal_query_cmd_pending = BOOL_FALSE;
    zk_signal_query_tick = 0;
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
    const zk_device_config_t *dev_cfg;

    dev_cfg = zk_device_config_get();
    if (dev_cfg == NULL)
    {
        return -1;
    }
    return snprintf(buf, buf_size, "AT+QMTOPEN=%d,\"%s\",%d\r\n",
                    ZK_MQTT_CLIENT_IDX,
                    dev_cfg->svrIp[0] != '\0' ? dev_cfg->svrIp : ZK_MQTT_SERVER_IP,
                    dev_cfg->svrPort > 0 ? dev_cfg->svrPort : ZK_MQTT_SERVER_PORT);
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

cJSON *zk_create_root_from_header(const zk_message_header_t *header, int with_er, int er_code)
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
    /* 移除"连续Busy强制pubsend_state_set_idle"：底层WAIT_PROMPT(5s)/WAIT_ACK(30s)已有超时自动回IDLE，
       发布状态机能自愈；强制清空会打断在途PUBACK事务，导致归属丢失与统计混乱。
       恢复路径统一由心跳监督连续3次失败触发的完整4G重连处理。 */
    (void)result;
}

static void zk_log_periodic_send_failure(const char *task_name)
{
    if (task_name == NULL)
    {
        return;
    }
    printf("ZK %s send failed, retry later\r\n", task_name);
}

static void zk_mqtt_force_reconnect(const char *reason)
{
    printf("ZK MQTT force reconnect: %s\r\n", reason == NULL ? "unknown" : reason);
    pubsend_state_set_idle();
    onNBEvent(NB_EVENT_LOST_CONNECTION, 0, 0);
    _4G_configModule_machine_star();
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

int zk_send_json_root(cJSON *root, const char *topic)
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

static zk_response_pending_item_t *zk_response_queue_front(void)
{
    if (zk_response_queue_count == 0)
    {
        return NULL;
    }
    return &zk_response_queue[zk_response_queue_head];
}

static void zk_response_queue_pop(void)
{
    if (zk_response_queue_count == 0)
    {
        zk_response_pending = BOOL_FALSE;
        return;
    }
    zk_response_queue_head++;
    if (zk_response_queue_head >= ZK_RESPONSE_QUEUE_SIZE)
    {
        zk_response_queue_head = 0;
    }
    zk_response_queue_count--;
    zk_response_pending = (zk_response_queue_count > 0) ? BOOL_TRUE : BOOL_FALSE;
}

void zk_schedule_simple_response(const zk_message_header_t *request, int err_code)
{
    u8 write_index;

    if (request == NULL)
    {
        return;
    }
    if (zk_response_queue_count >= ZK_RESPONSE_QUEUE_SIZE)
    {
        zk_response_queue_pop();
        zk_response_queue_drop_count++;
    }

    write_index = zk_response_queue_head + zk_response_queue_count;
    if (write_index >= ZK_RESPONSE_QUEUE_SIZE)
    {
        write_index -= ZK_RESPONSE_QUEUE_SIZE;
    }
    memcpy(&zk_response_queue[write_index].header, request, sizeof(zk_response_queue[write_index].header));
    zk_response_queue[write_index].err_code = err_code;
    zk_response_queue_count++;
    zk_response_pending = BOOL_TRUE;
}

int zk_publish_simple_response_now(const zk_message_header_t *request, int err_code)
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

int zk_publish_simple_response(const zk_message_header_t *request, int err_code)
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
    const zk_device_config_t *dev_cfg;

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
    dev_cfg = zk_device_config_get();
    if (dev_cfg == NULL)
    {
        return -1;
    }
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
                    dev_cfg->protId,
                    dev_cfg->clas,
                    dev_cfg->prottp,
                    dev_cfg->hver,
                    dev_cfg->sver,
                    dev_cfg->mver,
                    dev_cfg->iccid,
                    dev_cfg->lng,
                    dev_cfg->lat,
                    dev_cfg->zone,
                    dev_cfg->cns,
                    dev_cfg->dimTp,
                    dev_cfg->polar,
                    dev_cfg->dlmt,
                    dev_cfg->ulmt,
                    dev_cfg->rti,
                    dev_cfg->rtPwr,
                    dev_cfg->di,
                    dev_cfg->sBri,
                    dev_cfg->sBriTm);
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
    zk_login_wait_pub_success_count = nb_mqtt_get_publish_success_count();
    zk_login_wait_pub_fail_count = nb_mqtt_get_publish_fail_count();
    zk_login_wait_pub_timeout_count = nb_mqtt_get_publish_timeout_count();
    /* 不再每次发送清零业务ACK超时计数：仅新会话(zk_mqtt_reset_session)或业务登录成功
       (zk_mqtt_accept_login_ack)时清零，否则业务ACK持续缺失时无法累积到重连阈值 */
    zk_login_state = ZK_LOGIN_STATE_WAIT_PUBLISH;
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
    /* 提交成功只代表交给发布状态机，不代表Broker已确认；成功与否由心跳监督按PUBACK判定 */
    return 0;
}

/* ===================== 心跳健康监督（QoS1 Broker发布闭环） ===================== */

static void zk_mqtt_start_modem_recovery(const char *reason)
{
    if (OTA_ENABLE_IS_SET() == BOOL_TRUE || nb_modem_locked_by_ota() == BOOL_TRUE)
    {
        return;
    }
    printf("[MQTT][RECOVERY] start reason=%s\r\n",
           (reason != NULL) ? reason : "unknown");
    zk_hb_monitor_state = ZK_HEARTBEAT_MONITOR_IDLE;
    pubsend_state_set_idle();
    onNBEvent(NB_EVENT_LOST_CONNECTION, 0, 0);
    nb_mqtt_recovery_start(reason);
}

static void zk_heartbeat_mark_success(uint32 now)
{
    zk_hb_monitor_state = ZK_HEARTBEAT_MONITOR_IDLE;
    zk_hb_consecutive_fail_count = 0;
    zk_hb_period_tick = now;
    zk_broker_ack_snapshot = nb_mqtt_get_publish_success_count();
    zk_hb_success_count++;
    printf("[HBMON] ack success\r\n");
}

static void zk_heartbeat_mark_fail(uint32 now, const char *reason)
{
    zk_hb_monitor_state = ZK_HEARTBEAT_MONITOR_IDLE;
    zk_hb_period_tick = now;
    if (zk_hb_consecutive_fail_count < 0xFFU)
    {
        zk_hb_consecutive_fail_count++;
    }
    zk_hb_fail_count++;
    printf("[HBMON][W] %s fail=%u/%u\r\n",
           (reason != NULL) ? reason : "heartbeat",
           (unsigned int)zk_hb_consecutive_fail_count,
           (unsigned int)ZK_HEARTBEAT_FAIL_LIMIT);
    if (zk_hb_consecutive_fail_count >= ZK_HEARTBEAT_FAIL_LIMIT)
    {
        zk_hb_consecutive_fail_count = 0;
        printf("[HBMON][E] heartbeat failed %u times\r\n", (unsigned int)ZK_HEARTBEAT_FAIL_LIMIT);
        zk_mqtt_start_modem_recovery("heartbeat_no_ack");
    }
}

static void zk_heartbeat_submit(uint32 now)
{
    if (zk_publish_heartbeat_packet() == 0)
    {
        zk_hb_pub_success_snapshot = nb_mqtt_get_publish_success_count();
        zk_hb_pub_fail_snapshot = nb_mqtt_get_publish_fail_count();
        zk_hb_pub_timeout_snapshot = nb_mqtt_get_publish_timeout_count();
        zk_hb_monitor_state = ZK_HEARTBEAT_MONITOR_WAIT_ACK;
        zk_hb_state_tick = now;
        zk_hb_send_count++;
        printf("[HBMON] submit ok\r\n");
    }
    else
    {
        zk_heartbeat_mark_fail(now, "submit failed");
    }
}

/* 心跳监督只在业务ONLINE阶段运行，OTA期间暂停，固定60s周期 */
static void zk_heartbeat_monitor_process(uint32 now)
{
    if (zk_login_state != ZK_LOGIN_STATE_ONLINE)
    {
        return;
    }
    if (OTA_ENABLE_IS_SET() == BOOL_TRUE || nb_modem_locked_by_ota() == BOOL_TRUE)
    {
        return;
    }
    if (zk_hb_period_tick == 0)
    {
        zk_hb_period_tick = now;
        return;
    }

    switch (zk_hb_monitor_state)
    {
        case ZK_HEARTBEAT_MONITOR_IDLE:
            if (Timer_PassedDelay(zk_hb_period_tick, ZK_HEARTBEAT_MONITOR_PERIOD_MS) != BOOL_TRUE)
            {
                break;
            }
            if (pubsend_state_idle() == BOOL_TRUE)
            {
                zk_heartbeat_submit(now);
            }
            else
            {
                /* 发布器被正常事务占用，进入15s宽限等待，不强制打断当前事务 */
                zk_hb_monitor_state = ZK_HEARTBEAT_MONITOR_WAIT_SUBMIT;
                zk_hb_state_tick = now;
                printf("[HBMON] wait submit\r\n");
            }
            break;

        case ZK_HEARTBEAT_MONITOR_WAIT_SUBMIT:
            if (pubsend_state_idle() == BOOL_TRUE)
            {
                zk_heartbeat_submit(now);
            }
            else if (Timer_PassedDelay(zk_hb_state_tick, ZK_HEARTBEAT_SUBMIT_GRACE_MS) == BOOL_TRUE)
            {
                zk_heartbeat_mark_fail(now, "submit busy timeout");
            }
            break;

        case ZK_HEARTBEAT_MONITOR_WAIT_ACK:
            if (nb_mqtt_get_publish_success_count() != zk_hb_pub_success_snapshot)
            {
                zk_heartbeat_mark_success(now);
            }
            else if (nb_mqtt_get_publish_fail_count() != zk_hb_pub_fail_snapshot ||
                     nb_mqtt_get_publish_timeout_count() != zk_hb_pub_timeout_snapshot)
            {
                zk_heartbeat_mark_fail(now, "publish failed");
            }
            else if (Timer_PassedDelay(zk_hb_state_tick, ZK_HEARTBEAT_ACK_WAIT_MS) == BOOL_TRUE)
            {
                zk_heartbeat_mark_fail(now, "ack timeout");
            }
            break;

        default:
            break;
    }
}

/* 任意其他QoS1消息成功到达Broker，证明公共发布链路正常，清连续失败计数 */
static void zk_broker_ack_detect(uint32 now)
{
    uint32 success;

    if (zk_login_state != ZK_LOGIN_STATE_ONLINE)
    {
        return;
    }
    success = nb_mqtt_get_publish_success_count();
    if (success != zk_broker_ack_snapshot)
    {
        zk_broker_ack_snapshot = success;
        if (zk_hb_consecutive_fail_count != 0)
        {
            zk_hb_consecutive_fail_count = 0;
            printf("[HBMON] other qos1 broker ack ok, fail reset\r\n");
        }
    }
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
    if (strcmp(header->sv, ZK_SV_CAL) == 0)
    {
        return sys_calibration_mqtt_handle(root, header);
    }
    zk_apply_server_time_from_header(header);
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
    if (zk_handle_alam_message(root, header))
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


/**
*@brief   构建并发送信号查询的延迟响应(在 QENG 完成后调用)
*@param   request：保存的请求消息头
*@return  0：发送成功；-1：发送失败
*@note    复用 zk_add_signal_group 构建 Signal 数据组
*/
static int zk_publish_signal_query_response(const zk_message_header_t *request)
{
    cJSON *root;
    cJSON *dt;

    root = zk_create_root_from_header(request, 1, 0);
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
    zk_add_signal_group(dt);

    if (zk_send_json_root(root, NULL) != 0)
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_Delete(root);
    return 0;
}

/**
*@brief   信号强度查询状态机(周期刷新 + 命令延迟响应)
*@param   now：当前系统tick
*@return  1：已消费本tick(阻塞后续业务)；0：未触发
*@note    周期与上报周期(dev_cfg->uPeriod)对齐；UART忙时不推进周期,下个tick重试；
*         QENG失败不重试,直接推进周期等待下个周期
*/
static boolean_en zk_signal_query_process(uint32 now)
{
    const zk_device_config_t *dev_cfg;
    uint32 period_ms;
    boolean_en qeng_ok;

    switch (zk_signal_query_state)
    {
    case ZK_SIGNAL_QUERY_IDLE:
        dev_cfg = zk_device_config_get();
        if (dev_cfg == NULL)
        {
            return BOOL_FALSE;
        }
        period_ms = zk_get_effective_period_sec(dev_cfg->uPeriod,
                                                 ZK_UPLOAD_INTERVAL_SEC) * 1000UL;
        if (Timer_PassedDelay(zk_signal_query_tick, period_ms) == BOOL_FALSE)
        {
            return BOOL_FALSE;   /* 周期未到 */
        }
        if (nb_qeng_trigger_runtime() == BOOL_FALSE)
        {
            return BOOL_FALSE;   /* UART忙,下个tick重试,不推进周期 */
        }
        zk_signal_query_state = ZK_SIGNAL_QUERY_WAIT_FINISH;
        printf("[SIG] periodic query started\r\n");
        return BOOL_TRUE;

    case ZK_SIGNAL_QUERY_WAIT_FINISH:
        if (send_AT_Command_machine_finish() == BOOL_FALSE)
        {
            return BOOL_TRUE;    /* 仍在等待 QENG 完成 */
        }

        /* QENG 完成 — 无论成功失败都重置 AT 机并推进周期 */
        send_AT_Command_machine_idle();
        zk_signal_query_tick = now;
        zk_signal_query_state = ZK_SIGNAL_QUERY_IDLE;

        /* 成功 = AT命令执行成功 且 本次解析到有效RSRP，二者缺一不可；
           仅收到OK但无新RSRP(SEARCH/格式异常)按失败处理，避免用陈旧值回成功 */
        qeng_ok = ((nb_at_command_is_failed() == BOOL_FALSE) &&
                   (nb_qeng_last_capture_valid() == BOOL_TRUE))
                      ? BOOL_TRUE
                      : BOOL_FALSE;

        if (qeng_ok == BOOL_FALSE)
        {
            printf("[SIG] qeng failed or no valid rsrp\r\n");
        }
        else
        {
            printf("[SIG] qeng ok\r\n");
        }

        /* 命令触发的查询:发送延迟响应 */
        if (zk_signal_query_cmd_pending == BOOL_TRUE)
        {
            zk_signal_query_cmd_pending = BOOL_FALSE;
            if (qeng_ok == BOOL_FALSE)
            {
                zk_publish_simple_response(&zk_signal_query_cmd_header,
                                           ZK_SIGNAL_ERR_QENG_FAIL);
            }
            else
            {
                (void)zk_publish_signal_query_response(&zk_signal_query_cmd_header);
            }
        }
        return BOOL_TRUE;

    default:
        zk_signal_query_state = ZK_SIGNAL_QUERY_IDLE;
        return BOOL_FALSE;
    }
}

/** 添加运行时统计时间组到DT（调用前确保 zk_runtime_counter_process 已周期性执行） */
void zk_add_runtime_time_groups(cJSON *dt_root)
{
    cJSON *run_tm;
    cJSON *light_tm;
    cJSON *light_total;
    cJSON *light_run;
    uint32 run_minutes;
    uint32 light_run_minutes;
    uint32 total_run;
    uint32 total_light;
    uint32 boot_run;
    uint32 boot_light;

    /* 通过访问函数获取最新统计值（zk_runtime_counter_process 周期更新RAM） */
    boot_run = zk_runtime_get_boot_run_seconds();
    boot_light = zk_runtime_get_boot_light_seconds();
    total_run = zk_runtime_get_total_run_seconds();
    total_light = zk_runtime_get_total_light_seconds();

    run_minutes = boot_run / 60U;
    light_run_minutes = boot_light / 60U;

    run_tm = zk_cjson_create_tx_object("RunTm");
    light_tm = zk_cjson_create_tx_object("LightTm");
    light_total = zk_cjson_create_tx_array("LightTm.tLtTime");
    light_run = zk_cjson_create_tx_array("LightTm.rLtTime");
    if (run_tm == NULL || light_tm == NULL || light_total == NULL || light_run == NULL)
    {
        return;
    }

    /* RunTm.tTime = 历史累计 + 本次上电累计，单位分钟 */
    cJSON_AddNumberToObject(run_tm, "tTime", (double)(total_run / 60U));
    /* RunTm.rTime = 本次上电运行分钟 */
    cJSON_AddNumberToObject(run_tm, "rTime", (double)run_minutes);
    cJSON_AddItemToObject(dt_root, "RunTm", run_tm);

    /* LightTm.tLtTime = 历史亮灯累计 + 本次亮灯累计，单位分钟 */
    cJSON_AddItemToArray(light_total, cJSON_CreateNumber((double)(total_light / 60U)));
    /* LightTm.rLtTime = 本次上电亮灯分钟（关灯后保持历史值不归零） */
    cJSON_AddItemToArray(light_run, cJSON_CreateNumber((double)light_run_minutes));
    cJSON_AddItemToObject(light_tm, "tLtTime", light_total);
    cJSON_AddItemToObject(light_tm, "rLtTime", light_run);
    cJSON_AddItemToObject(dt_root, "LightTm", light_tm);
}

void zk_add_run_status_group(cJSON *dt_root)
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
    if (zk_is_over_temperature_protecting() == BOOL_TRUE)
    {
        cJSON_AddItemToArray(sts, cJSON_CreateNumber(51));
    }
    cJSON_AddItemToArray(bri, cJSON_CreateNumber(zk_get_current_brightness()));
    cJSON_AddItemToObject(run_status, "sts", sts);
    cJSON_AddItemToObject(run_status, "bri", bri);
    cJSON_AddItemToObject(dt_root, "RunSts", run_status);
}

void zk_add_ele_info_group(cJSON *dt_root)
{
    cJSON *ele_info;
    cJSON *e;
    cJSON *c;
    cJSON *v;
    cJSON *f;
    cJSON *p;
    cJSON *r_ec;
    cJSON *t_ec;
    cJSON *oc;
    cJSON *ov;
    cJSON *op;

    ele_info = zk_cjson_create_tx_object("EleInfo");
    e = zk_cjson_create_tx_array("EleInfo.e");
    c = zk_cjson_create_tx_array("EleInfo.c");
    v = zk_cjson_create_tx_array("EleInfo.v");
    f = zk_cjson_create_tx_array("EleInfo.f");
    p = zk_cjson_create_tx_array("EleInfo.p");
    r_ec = zk_cjson_create_tx_array("EleInfo.rEc");
    t_ec = zk_cjson_create_tx_array("EleInfo.tEc");
    oc = zk_cjson_create_tx_array("EleInfo.oc");
    ov = zk_cjson_create_tx_array("EleInfo.ov");
    op = zk_cjson_create_tx_array("EleInfo.op");
    if (ele_info == NULL || e == NULL || c == NULL || v == NULL ||
        f == NULL || p == NULL || r_ec == NULL || t_ec == NULL ||
        oc == NULL || ov == NULL || op == NULL)
    {
        return;
    }

    /* EleInfo.e[0]: 0表示无故障；有故障时上报当前电源故障位图 */
    cJSON_AddItemToArray(e, cJSON_CreateNumber((double)error_flag_byte));
    cJSON_AddItemToArray(c, cJSON_CreateNumber((double)Z_ac_current));
    cJSON_AddItemToArray(v, cJSON_CreateNumber((double)ac_voltage_8209));
    cJSON_AddItemToArray(f, cJSON_CreateNumber((double)((u32)ac_pf * 10U)));
    /* EleInfo.p: BL0942有功功率ac_powerpa原始单位0.01W，平台按原始数字显示W，故/100转为整数W（四舍五入） */
    cJSON_AddItemToArray(p, cJSON_CreateNumber((double)((ac_powerpa + 50U) / 100U)));
    /* EleInfo.rEc/tEc: 原始单位0.01Wh，协议要求W·h，故/100转为整数W·h（四舍五入） */
    /* tEc = flash历史累积 + 本周期RAM累积 = 设备启用至今总能耗 */
    cJSON_AddItemToArray(r_ec, cJSON_CreateNumber((double)((energy_this_time + 50U) / 100U)));
    cJSON_AddItemToArray(t_ec, cJSON_CreateNumber((double)((sys_data.ac_EnergyP + total_power_this_time + 50U) / 100U)));
    /* EleInfo.oc: 输出电流，Io_value原始单位mA，协议单位mA，直接使用 */
    cJSON_AddItemToArray(oc, cJSON_CreateNumber((double)Io_value));
    /* EleInfo.ov: 输出电压，Vo_value原始单位0.1V，协议单位0.1V，直接使用 */
    cJSON_AddItemToArray(ov, cJSON_CreateNumber((double)Vo_value));
    /* EleInfo.op: 输出功率，Po_value原始单位0.1W，协议单位W，/10四舍五入 */
    cJSON_AddItemToArray(op, cJSON_CreateNumber((double)((Po_value + 5U) / 10U)));
    cJSON_AddItemToObject(ele_info, "e", e);
    cJSON_AddItemToObject(ele_info, "c", c);
    cJSON_AddItemToObject(ele_info, "v", v);
    cJSON_AddItemToObject(ele_info, "f", f);
    cJSON_AddItemToObject(ele_info, "p", p);
    cJSON_AddItemToObject(ele_info, "rEc", r_ec);
    cJSON_AddItemToObject(ele_info, "tEc", t_ec);
    cJSON_AddItemToObject(ele_info, "oc", oc);
    cJSON_AddItemToObject(ele_info, "ov", ov);
    cJSON_AddItemToObject(ele_info, "op", op);
    cJSON_AddNumberToObject(ele_info, "pwr", power_down_flag ? 1 : 0);
    cJSON_AddNumberToObject(ele_info, "lc", (double)dangeo_out);
    cJSON_AddItemToObject(dt_root, "EleInfo", ele_info);
}

void zk_add_per_sts_group(cJSON *dt_root)
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

void zk_add_signal_group(cJSON *dt_root)
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

void zk_add_angle_group(cJSON *dt_root)
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

int zk_publish_alarm_report(uint16 alarm_id, u8 status, uint32 value, uint32 threshold)
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
    topic = zk_mqtt_get_config() != NULL ? zk_mqtt_cfg.pub_topic : NULL;
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
        zk_mqtt_cfg.pub_topic[0] == '\0')
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
    topic = zk_mqtt_get_config() != NULL ? zk_mqtt_cfg.pub_topic : NULL;
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
        zk_mqtt_cfg.pub_topic[0] == '\0')
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

static boolean_en zk_ota_ack_process(uint32 now)
{
    if (zk_ota_ack_state == ZK_OTA_ACK_STATE_IDLE)
    {
        return BOOL_FALSE;
    }

    if (zk_ota_ack_state == ZK_OTA_ACK_STATE_SEND)
    {
        if (pubsend_state_idle() == BOOL_FALSE)
        {
            return BOOL_TRUE;
        }
        if (zk_publish_simple_response_now(&zk_ota_ack_header, 0) == 0)
        {
            zk_ota_ack_pub_success_count = nb_mqtt_get_publish_success_count();
            zk_ota_ack_pub_fail_count = nb_mqtt_get_publish_fail_count();
            zk_ota_ack_pub_timeout_count = nb_mqtt_get_publish_timeout_count();
            zk_ota_ack_tick = now;
            zk_ota_ack_state = ZK_OTA_ACK_STATE_WAIT_PUBLISH;
            OTA_LOGI("cmd ack started id=%s\r\n", zk_ota_ack_header.id);
            return BOOL_TRUE;
        }
        ++zk_ota_ack_retry_count;
        if (zk_ota_ack_retry_count >= ZK_OTA_ACK_RETRY_LIMIT)
        {
            OTA_LOGE("cmd ack start failed id=%s retry=%u, skip download\r\n",
                     zk_ota_ack_header.id,
                     (unsigned int)zk_ota_ack_retry_count);
            zk_ota_ack_clear();
            return BOOL_TRUE;
        }
        zk_ota_ack_tick = now;
        OTA_LOGW("cmd ack start retry id=%s retry=%u\r\n",
                 zk_ota_ack_header.id,
                 (unsigned int)zk_ota_ack_retry_count);
        return BOOL_TRUE;
    }

    if (zk_ota_ack_state == ZK_OTA_ACK_STATE_WAIT_PUBLISH &&
        nb_mqtt_get_publish_success_count() != zk_ota_ack_pub_success_count)
    {
        OTA_LOGI("cmd ack published id=%s, start download local=%s\r\n",
                 zk_ota_ack_header.id,
                 firm_name_buffer);
        zk_ota_ack_clear();
        set_OTA_ENABLE();
        return BOOL_TRUE;
    }

    if (zk_ota_ack_state == ZK_OTA_ACK_STATE_WAIT_PUBLISH &&
        (nb_mqtt_get_publish_fail_count() != zk_ota_ack_pub_fail_count ||
         nb_mqtt_get_publish_timeout_count() != zk_ota_ack_pub_timeout_count ||
         Timer_PassedDelay(zk_ota_ack_tick, ZK_OTA_ACK_PUBLISH_TIMEOUT_MS)))
    {
        ++zk_ota_ack_retry_count;
        if (zk_ota_ack_retry_count >= ZK_OTA_ACK_RETRY_LIMIT)
        {
            OTA_LOGE("cmd ack publish failed id=%s retry=%u, skip download\r\n",
                     zk_ota_ack_header.id,
                     (unsigned int)zk_ota_ack_retry_count);
            zk_ota_ack_clear();
            return BOOL_TRUE;
        }
        OTA_LOGW("cmd ack publish retry id=%s retry=%u\r\n",
                 zk_ota_ack_header.id,
                 (unsigned int)zk_ota_ack_retry_count);
        zk_ota_ack_state = ZK_OTA_ACK_STATE_SEND;
        zk_ota_ack_tick = now;
    }

    return BOOL_TRUE;
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
    uint32 report_period_ms;
    uint32 time_request_period_ms;
    const zk_device_config_t *dev_cfg;

    now = Timer_GetTickCount();
    dev_cfg = zk_device_config_get();
    if (dev_cfg == NULL)
    {
        return;
    }
    zk_control_restore_process();
    report_period_ms = zk_get_effective_period_sec(dev_cfg->uPeriod, ZK_UPLOAD_INTERVAL_SEC) * 1000UL;
    time_request_period_ms = zk_get_effective_period_sec(dev_cfg->tPeriod, ZK_TIME_REQUEST_INTERVAL_SEC) * 1000UL;

    if (zk_reboot_pending == BOOL_TRUE && Timer_PassedDelay(zk_reboot_tick, 500))
    {
        NVIC_SystemReset();
    }

    if (zk_login_state == ZK_LOGIN_STATE_WAIT_PUBLISH)
    {
        if (nb_mqtt_get_publish_success_count() != zk_login_wait_pub_success_count)
        {
            /* 登录消息取得Broker发布确认：若正处于4G恢复流程，则标记传输恢复成功
               （业务登录ACK缺失不算传输失败，由login阶段轻量重连继续重试） */
            if (nb_mqtt_recovery_is_active() == BOOL_TRUE)
            {
                nb_mqtt_recovery_mark_transport_success();
                printf("[MQTT][LOGIN] publish broker ack ok, transport restored\r\n");
            }
            zk_login_state = ZK_LOGIN_STATE_WAIT_ACK;
            /* 业务ACK超时计数不在"发布成功"时清零，否则登录重发会让计数永远累积不到重连阈值；
               仅新会话(zk_mqtt_reset_session)或业务登录成功(zk_mqtt_accept_login_ack)时清零 */
            zk_login_tick = now;
            return;
        }
        if (nb_mqtt_get_publish_timeout_count() != zk_login_wait_pub_timeout_count)
        {
            zk_mqtt_force_reconnect("login_publish_timeout");
            return;
        }
        if (nb_mqtt_get_publish_fail_count() != zk_login_wait_pub_fail_count)
        {
            zk_mqtt_force_reconnect("login_publish_fail");
            return;
        }
        if (Timer_PassedDelay(zk_login_tick, ZK_LOGIN_PUBLISH_TIMEOUT_MS) == BOOL_TRUE)
        {
            zk_mqtt_force_reconnect("login_publish_stall");
            return;
        }
        return;
    }

    if (zk_login_state == ZK_LOGIN_STATE_WAIT_ACK)
    {
        if (Timer_PassedDelay(zk_login_tick, ZK_LOGIN_ACK_TIMEOUT_MS) == BOOL_FALSE)
        {
            return;
        }
        if (zk_login_ack_timeout_count < 0xFFU)
        {
            zk_login_ack_timeout_count++;
        }
        if (zk_login_ack_timeout_count >= ZK_LOGIN_ACK_RECONNECT_THRESHOLD)
        {
            zk_mqtt_force_reconnect("login_ack_timeout");
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
        /* 心跳监督优先执行，避免被下方告警/OTA/上报等提前return饿死；
           固定60s周期，成功以Broker PUBACK为准，连续3次失败触发4G恢复 */
        zk_broker_ack_detect(now);
        zk_heartbeat_monitor_process(now);

        if (zk_alarm_process() == BOOL_TRUE)
        {
            return;
        }

        if (zk_ota_ack_process(now) == BOOL_TRUE)
        {
            return;
        }

        if (zk_ota_success_report_process(now) == BOOL_TRUE)
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
            zk_response_pending_item_t *item;

            item = zk_response_queue_front();
            if (item == NULL)
            {
                zk_response_pending = BOOL_FALSE;
                return;
            }
            if (zk_publish_simple_response_now(&item->header,
                                               item->err_code) == 0)
            {
                zk_response_queue_pop();
            }
            return;
        }

        if (zk_login_time_sync_pending == BOOL_TRUE)
        {
            if (zk_publish_time_request() == 0)
            {
                zk_login_time_sync_pending = BOOL_FALSE;
                zk_startup_time_force_pending = BOOL_TRUE;
                zk_time_request_tick = now;
                printf("[RTC] startup TmCali request sent, next ack will force sync\r\n");
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

        /* QENG信号刷新:先于周期上报,保证上报使用刚解析的RSRP;
           查询进行中(WAIT_FINISH)或刚完成时返回TRUE,阻塞本tick后续上报/校时,
           上报顺延到QENG完成后一tick,携带最新信号值 */
        if (zk_signal_query_process(now) == BOOL_TRUE)
        {
            return;
        }

        /* 周期上报重试窗口：首次失败后5s到点最多重试一次；窗口期内不阻塞后续业务 */
        if (zk_cyclic_report_retry_pending == BOOL_TRUE &&
            Timer_PassedDelay(zk_cyclic_report_retry_tick, ZK_CYCLIC_REPORT_RETRY_DELAY_MS) == BOOL_TRUE)
        {
            zk_cyclic_report_retry_pending = BOOL_FALSE;
            if (zk_publish_runtime_report(ZK_CT_CYCLIC) == 0)
            {
                zk_report_tick = now;
            }
            else
            {
                /* 重试仍失败：放弃本次周期上报，清理TX池残留，推进周期计时 */
                zk_report_tick = now;
                zk_cjson_prepare_tx();
            }
            return;
        }

        if (Timer_PassedDelay(zk_report_tick, report_period_ms))
        {
            if (zk_publish_runtime_report(ZK_CT_CYCLIC) == 0)
            {
                zk_report_tick = now;
            }
            else
            {
                zk_log_periodic_send_failure("cyclic report");
                /* 最多重试一次：记录重试窗口，不再无限占用主循环 */
                zk_cyclic_report_retry_pending = BOOL_TRUE;
                zk_cyclic_report_retry_tick = now;
            }
            return;
        }

        /* 校时重试窗口：与周期上报对称，失败最多重试一次，仍失败则放弃并推进周期 */
        if (zk_time_request_retry_pending == BOOL_TRUE &&
            Timer_PassedDelay(zk_time_request_retry_tick, ZK_CYCLIC_REPORT_RETRY_DELAY_MS) == BOOL_TRUE)
        {
            zk_time_request_retry_pending = BOOL_FALSE;
            if (zk_publish_time_request() == 0)
            {
                zk_time_request_tick = now;
            }
            else
            {
                /* 重试仍失败：放弃本次校时，清理TX池，推进周期计时 */
                zk_time_request_tick = now;
                zk_cjson_prepare_tx();
            }
            return;
        }

        if (Timer_PassedDelay(zk_time_request_tick, time_request_period_ms))
        {
            if (zk_publish_time_request() == 0)
            {
                zk_time_request_tick = now;
            }
            else
            {
                zk_log_periodic_send_failure("time request");
                /* 最多重试一次：记录重试窗口，不再无限占用主循环 */
                zk_time_request_retry_pending = BOOL_TRUE;
                zk_time_request_retry_tick = now;
            }
            return;
        }
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
    uint32 now;

    if (header == NULL)
    {
        return BOOL_FALSE;
    }
    if (zk_login_state != ZK_LOGIN_STATE_WAIT_ACK &&
        zk_login_state != ZK_LOGIN_STATE_WAIT_PUBLISH)
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
        now = Timer_GetTickCount();
        if (nb_mqtt_recovery_is_active() == BOOL_TRUE)
        {
            nb_mqtt_recovery_mark_transport_success();
        }
        zk_login_state = ZK_LOGIN_STATE_ONLINE;
        zk_login_ack_timeout_count = 0;
        zk_sync_online_period_timers(now);
        zk_login_time_sync_pending = (zk_startup_time_sync_done == BOOL_TRUE) ? BOOL_FALSE : BOOL_TRUE;
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
        /* 业务平台心跳ACK仅确认业务在线；Broker链路健康以QoS1发布确认为准（心跳监督），不在此更新周期 */
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
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
    /*
     * 平台会在调光/开关灯后下发巡检，巡检的 CT:C 已包含相同的
     * RunSts。此处不再排队 CT:B，避免同一状态重复上报。
     */
}

void zk_apply_plan_brightness(int brightness)
{
    /* A newer plan action supersedes any temporary control restore snapshot. */
    zk_cancel_control_restore();
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
        if (strcmp(do_node->valuestring, "SignalQuery") == 0)
        {
            /* 检查信号查询状态机是否空闲(避免与周期刷新冲突) */
            if (zk_signal_query_state != ZK_SIGNAL_QUERY_IDLE)
            {
                zk_publish_simple_response(header, ZK_SIGNAL_ERR_BUSY);
                return BOOL_TRUE;
            }
            /* 尝试发送 QENG,若 UART 忙则立即返回失败 */
            if (nb_qeng_trigger_runtime() == BOOL_FALSE)
            {
                zk_publish_simple_response(header, ZK_SIGNAL_ERR_BUSY);
                return BOOL_TRUE;
            }
            /* 保存请求头,QENG 完成后由 zk_signal_query_process 延迟响应 */
            memcpy(&zk_signal_query_cmd_header, header, sizeof(zk_message_header_t));
            zk_signal_query_cmd_pending = BOOL_TRUE;
            zk_signal_query_state = ZK_SIGNAL_QUERY_WAIT_FINISH;
            printf("[SIG] cmd query triggered by server\r\n");
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
            if (zk_device_config_restore_defaults() == BOOL_FALSE)
            {
                zk_publish_simple_response(header, ZK_FLASH_SAVE_ERROR);
                return BOOL_TRUE;
            }
            zk_cancel_control_restore();
            zk_reset_config_period_timers();
        }
        if (restore_type == 5)
        {
            boolean_en runtime_ok;
            boolean_en energy_main_ok;
            boolean_en energy_backup_ok;

            sys_bl0942_energy_stats_clear();
            sys_data_store();
            energy_main_ok = user_flash_check(DATAROM_STARTADDR,
                                              (u8 *)&sys_data,
                                              (u16)sizeof(sys_data));
            energy_backup_ok = user_flash_check(BAKDATAROM_STARTADDR,
                                                (u8 *)&sys_data,
                                                (u16)sizeof(sys_data));
            runtime_ok = zk_runtime_stats_clear();
            if ((energy_main_ok != BOOL_TRUE && energy_backup_ok != BOOL_TRUE) ||
                runtime_ok != BOOL_TRUE)
            {
                zk_publish_simple_response(header, ZK_FLASH_SAVE_ERROR);
                return BOOL_TRUE;
            }
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
    const char *time_text;
    boolean_en force_apply;
    boolean_en applied;

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
        force_apply = zk_startup_time_force_pending;
        applied = BOOL_FALSE;
        time_text = zk_json_get_rtc_time_text(tm_cali);
        if (time_text != NULL)
        {
            applied = zk_apply_server_time_text_ex(time_text, force_apply);
        }
        else
        {
            printf("[RTC] TmCali ack=%d, server UTC is header TM: %s\r\n",
                   cJSON_IsNumber(tm_cali) ? tm_cali->valueint : 0,
                   header->tm);
            if (force_apply == BOOL_TRUE)
            {
                applied = zk_apply_server_time_text_ex(header->tm, BOOL_TRUE);
            }
        }
        if (force_apply == BOOL_TRUE && applied == BOOL_TRUE)
        {
            zk_startup_time_force_pending = BOOL_FALSE;
            zk_startup_time_sync_done = BOOL_TRUE;
            printf("[RTC] startup force sync complete\r\n");
        }
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
        OTA_LOGW("cmd rejected: missing url id=%s\r\n", header->id);
        zk_publish_simple_response(header, 5);
        return BOOL_TRUE;
    }
    if (strncmp(url->valuestring, "http://", 7) != 0)
    {
        OTA_LOGW("cmd rejected: unsupported url id=%s\r\n", header->id);
        zk_publish_simple_response(header, 91);
        return BOOL_TRUE;
    }
    if (strlen(url->valuestring) >= sizeof(zk_ota_url))
    {
        OTA_LOGW("cmd rejected: url too long id=%s len=%u\r\n", header->id, (unsigned int)strlen(url->valuestring));
        zk_publish_simple_response(header, 91);
        return BOOL_TRUE;
    }
    if (strchr(url->valuestring, '\r') != NULL || strchr(url->valuestring, '\n') != NULL)
    {
        OTA_LOGW("cmd rejected: url has newline id=%s\r\n", header->id);
        zk_publish_simple_response(header, 91);
        return BOOL_TRUE;
    }
    if (zk_ota_is_busy() == BOOL_TRUE)
    {
        OTA_LOGW("cmd rejected: ota busy id=%s\r\n", header->id);
        zk_publish_simple_response(header, 12);
        return BOOL_TRUE;
    }
    strncpy(zk_last_ota_id, header->id, sizeof(zk_last_ota_id) - 1);
    zk_last_ota_id[sizeof(zk_last_ota_id) - 1] = '\0';
    zk_ota_set_url(url->valuestring);
    zk_ota_report_mark_pending(zk_last_ota_id, url->valuestring);
    memset(firm_name_buffer, 0, 256);
    strncpy(firm_name_buffer, OTA_LOCAL_FIRMWARE_NAME, 255);
    OTA_LOGI("cmd received id=%s url=%s local=%s\r\n", zk_last_ota_id, url->valuestring, firm_name_buffer);
    zk_ota_ack_defer_start(header);
    OTA_LOGI("cmd accepted id=%s wait ack before download\r\n", zk_last_ota_id);
    return BOOL_TRUE;
}

boolean_en zk_handle_alam_message(cJSON *root, const zk_message_header_t *header)
{
    cJSON *dt;
    cJSON *alms;
    cJSON *alm_item;
    cJSON *out_alms;
    cJSON *out_alm;
    cJSON *root_out;
    cJSON *dt_out;
    int index;
    int count;
    int alm_id;
    int err;
    int value;
    int rec_value;
    int alm_en;
    const zk_device_config_t *cfg;
    zk_device_config_t candidate;

    if (root == NULL || header == NULL)
    {
        return BOOL_FALSE;
    }
    if (strcmp(header->sv, ZK_SV_ALAM) != 0)
    {
        return BOOL_FALSE;
    }

    dt = cJSON_GetObjectItem(root, "DT");

    /* ---------- CT="R" 读取告警阈值配置 ---------- */
    if (strcmp(header->ct, ZK_CT_READ) == 0)
    {
        alms = (dt != NULL) ? cJSON_GetObjectItem(dt, "alms") : NULL;
        if (alms == NULL || !cJSON_IsArray(alms) || cJSON_GetArraySize(alms) <= 0)
        {
            zk_publish_simple_response(header, 5);
            return BOOL_TRUE;
        }

        cfg = zk_device_config_get();
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

        out_alms = cJSON_CreateArray();
        if (out_alms == NULL)
        {
            cJSON_Delete(root_out);
            zk_publish_simple_response(header, 12);
            return BOOL_TRUE;
        }
        cJSON_AddItemToObject(dt_out, "alms", out_alms);

        count = cJSON_GetArraySize(alms);
        for (index = 0; index < count; ++index)
        {
            alm_item = cJSON_GetArrayItem(alms, index);
            if (alm_item == NULL || !cJSON_IsNumber(alm_item))
            {
                continue;
            }
            alm_id = cJSON_GetNumberValue(alm_item);
            if (alm_id < 10000 || alm_id > 10009)
            {
                continue;
            }
            alm_id -= 10000; /* 转为config数组索引 */
            out_alm = cJSON_CreateObject();
            if (out_alm == NULL)
            {
                continue;
            }
            cJSON_AddNumberToObject(out_alm, "almId", alm_id + 10000);
            cJSON_AddNumberToObject(out_alm, "almValue", cfg->almValue[alm_id]);
            cJSON_AddNumberToObject(out_alm, "recValue", cfg->almRecValue[alm_id]);
            cJSON_AddNumberToObject(out_alm, "almEn", cfg->almEn[alm_id]);
            cJSON_AddItemToArray(out_alms, out_alm);
        }

        if (cJSON_GetArraySize(out_alms) <= 0)
        {
            cJSON_Delete(root_out);
            zk_publish_simple_response(header, 4);
            return BOOL_TRUE;
        }

        if (zk_send_json_root(root_out, NULL) != 0)
        {
            cJSON_Delete(root_out);
        }
        return BOOL_TRUE;
    }

    /* ---------- CT="W" 写入告警阈值配置 ---------- */
    if (strcmp(header->ct, ZK_CT_WRITE) == 0)
    {
        alms = (dt != NULL) ? cJSON_GetObjectItem(dt, "alms") : NULL;
        if (alms == NULL || !cJSON_IsArray(alms) || cJSON_GetArraySize(alms) <= 0)
        {
            zk_publish_simple_response(header, 5);
            return BOOL_TRUE;
        }

        /* 先取当前配置副本，在RAM中完成全部校验后再一次性写Flash，
         * 避免逐条写入导致部分生效和Flash擦写次数放大。 */
        cfg = zk_device_config_get();
        candidate = *cfg;
        err = 0;
        count = cJSON_GetArraySize(alms);
        for (index = 0; index < count; ++index)
        {
            cJSON *alm_id_node;

            alm_item = cJSON_GetArrayItem(alms, index);
            if (alm_item == NULL || !cJSON_IsObject(alm_item))
            {
                err = 2;
                break;
            }

            alm_id_node = cJSON_GetObjectItem(alm_item, "almId");
            if (alm_id_node == NULL || !cJSON_IsNumber(alm_id_node))
            {
                err = 2;
                break;
            }
            alm_id = (int)cJSON_GetNumberValue(alm_id_node);
            if (alm_id < 10000 || alm_id > 10009)
            {
                err = 4;
                break;
            }
            alm_id -= 10000;
            /* almValue/recValue/almEn 均为可选字段，不出现则保持原有值 */
            value = candidate.almValue[alm_id];
            rec_value = candidate.almRecValue[alm_id];
            alm_en = candidate.almEn[alm_id];

            {
                cJSON *node = cJSON_GetObjectItem(alm_item, "almValue");
                if (node != NULL) { if (!cJSON_IsNumber(node)) { err = 2; break; } value = (int)cJSON_GetNumberValue(node); }
            }
            {
                cJSON *node = cJSON_GetObjectItem(alm_item, "recValue");
                if (node != NULL) { if (!cJSON_IsNumber(node)) { err = 2; break; } rec_value = (int)cJSON_GetNumberValue(node); }
            }
            {
                cJSON *node = cJSON_GetObjectItem(alm_item, "almEn");
                if (node != NULL) { if (!cJSON_IsNumber(node) && !cJSON_IsBool(node)) { err = 2; break; } alm_en = cJSON_IsTrue(node) ? 1 : (node->valueint != 0 ? 1 : 0); }
            }

            candidate.almValue[alm_id] = value;
            candidate.almRecValue[alm_id] = rec_value;
            candidate.almEn[alm_id] = alm_en;
        }

        if (err != 0)
        {
            zk_publish_simple_response(header, err);
            return BOOL_TRUE;
        }

        if (zk_device_config_commit(&candidate) == BOOL_FALSE)
        {
            zk_publish_simple_response(header, 99);
            return BOOL_TRUE;
        }

        zk_publish_simple_response(header, 0);
        return BOOL_TRUE;
    }

    return BOOL_FALSE;
}
