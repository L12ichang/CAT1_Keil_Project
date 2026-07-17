#include "zk_runtime_stats.h"
#include "common.h"
#include "Portable.h"
#include "hw_flash.h"
#include "flash_address_assignment.h"
#include "crc16_modbus.h"
#include "sys_data.h"
#include "sys_pow_drop_check.h"
#include "net_dim.h"

/* ========== Flash 存储布局 ========== */
#define ZK_RUNTIME_FLASH_MAGIC        0x5A4B5254UL
#define ZK_RUNTIME_FLASH_VERSION      1U
#define ZK_RUNTIME_FLASH_OFFSET       0x200UL
/* 运行时统计存储在属性配置之后 0x200 字节偏移处 */
#define ZK_RUNTIME_FLASH_MAIN_ADDR    (DATAROM_STARTADDR + FLASH_PAGE_SIZE + ZK_RUNTIME_FLASH_OFFSET)
#define ZK_RUNTIME_FLASH_BACKUP_ADDR  (BAKDATAROM_STARTADDR + FLASH_PAGE_SIZE + ZK_RUNTIME_FLASH_OFFSET)
#define ZK_RUNTIME_SAVE_INTERVAL_MS   (6UL * 60UL * 60UL * 1000UL)  /* 每6小时持久化一次 */

#define ZK_FLASH_SAVE_ERROR           99

#define ZK_STATIC_ASSERT_CONCAT_(a, b) a##b
#define ZK_STATIC_ASSERT_CONCAT(a, b) ZK_STATIC_ASSERT_CONCAT_(a, b)
#define ZK_STATIC_ASSERT(cond) typedef char ZK_STATIC_ASSERT_CONCAT(zk_static_assert_, __LINE__)[(cond) ? 1 : -1]

/* ========== Flash 记录结构 ========== */
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

ZK_STATIC_ASSERT((ZK_RUNTIME_FLASH_OFFSET + sizeof(zk_runtime_flash_record_t)) <= FLASH_PAGE_SIZE);
ZK_STATIC_ASSERT((ZK_RUNTIME_FLASH_OFFSET + sizeof(zk_runtime_flash_record_t)) <= CURRENT_CAL_FLASH_SLOT_OFFSET);

/* ========== 运行时统计状态 ========== */
static uint32 zk_boot_run_seconds;              /* 本次上电累计运行秒数 */
static uint32 zk_boot_light_seconds;            /* 本次上电累计亮灯秒数（仅dim_level>0时累加） */
static uint32 zk_total_run_base_seconds;        /* 历史总运行秒数（本次之前的累计） */
static uint32 zk_total_light_base_seconds;      /* 历史总亮灯秒数（本次之前的累计） */
static uint32 zk_runtime_last_tick;             /* 上次统计tick */
static uint32 zk_runtime_flash_seq;
static uint32 zk_runtime_last_save_tick;
static boolean_en zk_runtime_loaded = BOOL_FALSE;
static boolean_en zk_runtime_powerdown_saved = BOOL_FALSE;

/* ========== Flash 读写辅助 ========== */

/* 计算运行时记录CRC校验 */
static u16 zk_runtime_flash_checksum(zk_runtime_flash_record_t *record)
{
    return crc16_modbus_get((unsigned char *)record,
                            sizeof(*record) - sizeof(record->checksum));
}

/* 校验运行时记录是否合法（magic/version/size/checksum） */
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

/* 从Flash读取一条运行时记录并校验 */
static boolean_en zk_runtime_flash_read_record(u32 addr,
                                               zk_runtime_flash_record_t *record)
{
    hw_flash_read_bytes(addr, (u8 *)record, sizeof(*record));
    return zk_runtime_record_valid(record);
}

/* 将当前RAM统计值填入记录结构 */
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

/* 将一条记录写入Flash并校验 */
static boolean_en zk_runtime_flash_write_record(u32 addr,
                                                zk_runtime_flash_record_t *record)
{
    hw_flash_write_bytes(addr, (u8 *)record, sizeof(*record));
    return user_flash_check(addr, (u8 *)record, sizeof(*record));
}

/* 将当前统计值持久化到Flash（主/备双备份） */
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

/* 运行时持久化处理：周期保存 + 掉电保存 */
static void zk_runtime_save_process(uint32 now)
{
    /* 每6小时定时保存 */
    if (Timer_PassedDelay(zk_runtime_last_save_tick, ZK_RUNTIME_SAVE_INTERVAL_MS) == BOOL_TRUE)
    {
        (void)zk_runtime_flash_store_current();
        zk_runtime_last_save_tick = now;
    }

    /* 检测到掉电标志时立即保存 */
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

/* ========== 公共API ========== */

/* 初始化运行时统计：从Flash加载历史累计值，清空本次上电计数 */
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

/* 运行时统计非阻塞周期处理：秒级更新RAM，周期/掉电时持久化Flash */
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

/* ========== 访问函数（供协议层构建JSON使用） ========== */

boolean_en zk_runtime_stats_clear(void)
{
    uint32 old_boot_run;
    uint32 old_total_run_base;
    uint32 now;

    if (zk_runtime_loaded != BOOL_TRUE)
    {
        zk_runtime_stats_init();
    }

    zk_runtime_counter_process();
    old_boot_run = zk_boot_run_seconds;
    old_total_run_base = zk_total_run_base_seconds;

    zk_boot_run_seconds = 0U;
    zk_total_run_base_seconds = 0U;

    if (zk_runtime_flash_store_current() != BOOL_TRUE)
    {
        zk_boot_run_seconds = old_boot_run;
        zk_total_run_base_seconds = old_total_run_base;
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
