#include "zk_work_plan.h"
#if ZK_ENABLE_SUNRISE_PLAN
#include "zk_sunriset.h"
#endif
#include "sys_persistent_storage.h"
#include "sys_aip1302.h"
#include <stdio.h>
#include <string.h>

typedef struct __attribute__((packed))
{
    u16 year;
    u8 mon;
    u8 day;
    u8 hour;
    u8 min;
    u8 sec;
    u8 reserved;
} zk_plan_datetime_t;

typedef struct __attribute__((packed))
{
    u16 minute;
    u8 bri;
    u8 reserved;
} zk_plan_action_t;

typedef struct __attribute__((packed))
{
    u8 cns_mask;
    u8 timetp;
    u8 action_count;
    u8 reserved;
    zk_plan_action_t actions[ZK_PLAN_MAX_ACTIONS];
} zk_plan_job_t;

typedef struct __attribute__((packed))
{
    u8 valid;
    u8 en;
    u8 id;
    u8 type;
    u8 priority;
    u8 week_mask;
    u8 job_count;
    s8 setoffset;       /* 日落偏移量（分钟），负值=日落前，仅 type=2 有效 */
    s8 riseoffset;      /* 日出偏移量（分钟），负值=日出前，仅 type=2 有效 */
    zk_plan_datetime_t start;
    zk_plan_datetime_t end;
    zk_plan_job_t jobs[ZK_PLAN_MAX_JOBS];
} zk_plan_record_t;

typedef struct __attribute__((packed))
{
    zk_plan_record_t plans[ZK_PLAN_MAX_COUNT];
} zk_plan_store_t;

typedef struct
{
    u8 found;
    u8 id;
    u8 job_index;
    u8 action_index;
    u8 priority;
    u8 bri;
} zk_plan_match_t;

static zk_plan_store_t zk_plan_store;
static u8 zk_plan_persistent_payload[SYS_PERSISTENT_CONFIG_PLAN_LENGTH];
static boolean_en zk_plan_loaded = BOOL_FALSE;

static int zk_plan_delete(int id);

static u16 zk_last_exec_year = 0;
static u8 zk_last_exec_mon = 0;
static u8 zk_last_exec_day = 0;
static u16 zk_last_exec_minute = 0xffff;
static u8 zk_last_exec_plan = 0xff;
static u8 zk_last_exec_job = 0xff;
static u8 zk_last_exec_action = 0xff;

static int zk_plan_handle_read(cJSON *dt, const zk_message_header_t *header);
static int zk_plan_find_current_match(zk_plan_match_t *match);
static int zk_plan_find_current_match_for_cns(zk_plan_match_t *match, int cns);

static void zk_plan_clear_last_exec(void)
{
    zk_last_exec_year = 0;
    zk_last_exec_mon = 0;
    zk_last_exec_day = 0;
    zk_last_exec_minute = 0xffff;
    zk_last_exec_plan = 0xff;
    zk_last_exec_job = 0xff;
    zk_last_exec_action = 0xff;
}

static void zk_plan_reset_store(zk_plan_store_t *store)
{
    memset(store, 0, sizeof(*store));
}

static boolean_en zk_plan_payload_decode(
    const u8 payload[SYS_PERSISTENT_CONFIG_PLAN_LENGTH],
    zk_plan_store_t *store)
{
    u8 plan_index;
    u8 job_index;
    u8 action_index;
    u16 offset;
    const u8 *source;
    zk_plan_record_t *record;
    zk_plan_job_t *job;

    if (payload == NULL || store == NULL)
    {
        return BOOL_FALSE;
    }
    memset(store, 0, sizeof(*store));
    for (plan_index = 0U; plan_index < ZK_PLAN_MAX_COUNT; ++plan_index)
    {
        source = payload + (u16)plan_index * 81U;
        record = &store->plans[plan_index];
        if (source[0x00U] > 1U || source[0x01U] > 1U)
        {
            return BOOL_FALSE;
        }
        record->valid = source[0x00U];
        record->en = source[0x01U];
        record->id = source[0x02U];
        record->type = source[0x03U];
        record->priority = source[0x04U];
        record->week_mask = source[0x05U];
        record->job_count = source[0x06U];
        record->setoffset = (s8)source[0x07U];
        record->riseoffset = (s8)source[0x08U];
        if (record->valid == 1U &&
            (record->id == 0U || record->id > ZK_PLAN_MAX_COUNT ||
             record->job_count > ZK_PLAN_MAX_JOBS))
        {
            return BOOL_FALSE;
        }
        record->start.year = sys_persistent_get_u16_le(source + 0x09U);
        record->start.mon = source[0x0BU];
        record->start.day = source[0x0CU];
        record->start.hour = source[0x0DU];
        record->start.min = source[0x0EU];
        record->start.sec = source[0x0FU];
        record->start.reserved = source[0x10U];
        record->end.year = sys_persistent_get_u16_le(source + 0x11U);
        record->end.mon = source[0x13U];
        record->end.day = source[0x14U];
        record->end.hour = source[0x15U];
        record->end.min = source[0x16U];
        record->end.sec = source[0x17U];
        record->end.reserved = source[0x18U];
        if (record->start.reserved != 0U || record->end.reserved != 0U)
        {
            return BOOL_FALSE;
        }
        for (job_index = 0U; job_index < ZK_PLAN_MAX_JOBS; ++job_index)
        {
            offset = (u16)(0x19U + job_index * 28U);
            job = &record->jobs[job_index];
            job->cns_mask = source[offset + 0U];
            job->timetp = source[offset + 1U];
            job->action_count = source[offset + 2U];
            job->reserved = source[offset + 3U];
            if (job->action_count > ZK_PLAN_MAX_ACTIONS || job->reserved != 0U)
            {
                return BOOL_FALSE;
            }
            for (action_index = 0U; action_index < ZK_PLAN_MAX_ACTIONS;
                 ++action_index)
            {
                u16 action_offset =
                    (u16)(offset + 4U + action_index * 4U);
                job->actions[action_index].minute =
                    sys_persistent_get_u16_le(source + action_offset);
                job->actions[action_index].bri = source[action_offset + 2U];
                job->actions[action_index].reserved =
                    source[action_offset + 3U];
                if (job->actions[action_index].reserved != 0U)
                {
                    return BOOL_FALSE;
                }
            }
        }
    }
    return BOOL_TRUE;
}

static void zk_plan_payload_encode(
    const zk_plan_store_t *store,
    u8 payload[SYS_PERSISTENT_CONFIG_PLAN_LENGTH])
{
    u8 plan_index;
    u8 job_index;
    u8 action_index;
    u16 offset;
    u8 *target;
    const zk_plan_record_t *record;
    const zk_plan_job_t *job;

    memset(payload, 0, SYS_PERSISTENT_CONFIG_PLAN_LENGTH);
    for (plan_index = 0U; plan_index < ZK_PLAN_MAX_COUNT; ++plan_index)
    {
        target = payload + (u16)plan_index * 81U;
        record = &store->plans[plan_index];
        target[0x00U] = record->valid;
        target[0x01U] = record->en;
        target[0x02U] = record->id;
        target[0x03U] = record->type;
        target[0x04U] = record->priority;
        target[0x05U] = record->week_mask;
        target[0x06U] = record->job_count;
        target[0x07U] = (u8)record->setoffset;
        target[0x08U] = (u8)record->riseoffset;
        sys_persistent_put_u16_le(target + 0x09U, record->start.year);
        target[0x0BU] = record->start.mon;
        target[0x0CU] = record->start.day;
        target[0x0DU] = record->start.hour;
        target[0x0EU] = record->start.min;
        target[0x0FU] = record->start.sec;
        sys_persistent_put_u16_le(target + 0x11U, record->end.year);
        target[0x13U] = record->end.mon;
        target[0x14U] = record->end.day;
        target[0x15U] = record->end.hour;
        target[0x16U] = record->end.min;
        target[0x17U] = record->end.sec;
        for (job_index = 0U; job_index < ZK_PLAN_MAX_JOBS; ++job_index)
        {
            offset = (u16)(0x19U + job_index * 28U);
            job = &record->jobs[job_index];
            target[offset + 0U] = job->cns_mask;
            target[offset + 1U] = job->timetp;
            target[offset + 2U] = job->action_count;
            for (action_index = 0U; action_index < ZK_PLAN_MAX_ACTIONS;
                 ++action_index)
            {
                u16 action_offset =
                    (u16)(offset + 4U + action_index * 4U);
                sys_persistent_put_u16_le(
                    target + action_offset,
                    job->actions[action_index].minute);
                target[action_offset + 2U] =
                    job->actions[action_index].bri;
            }
        }
    }
}

static boolean_en zk_plan_save_store(const zk_plan_store_t *store)
{
    zk_plan_payload_encode(store, zk_plan_persistent_payload);
    if (sys_persistent_config_update_section(
            SYS_PERSISTENT_CONFIG_PLAN_OFFSET,
            zk_plan_persistent_payload,
            sizeof(zk_plan_persistent_payload),
            NULL) == BOOL_TRUE)
    {
        zk_plan_store = *store;
        zk_plan_loaded = BOOL_TRUE;
        zk_plan_clear_last_exec();
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

boolean_en zk_work_plan_persistent_defaults(u8 *payload, u16 length)
{
    if (payload == NULL || length != SYS_PERSISTENT_CONFIG_PLAN_LENGTH)
    {
        return BOOL_FALSE;
    }
    memset(payload, 0, SYS_PERSISTENT_CONFIG_PLAN_LENGTH);
    return BOOL_TRUE;
}

void zk_work_plan_init(void)
{
    if (sys_persistent_config_read_section(
            SYS_PERSISTENT_CONFIG_PLAN_OFFSET,
            zk_plan_persistent_payload,
            sizeof(zk_plan_persistent_payload),
            NULL) == BOOL_TRUE &&
        zk_plan_payload_decode(zk_plan_persistent_payload,
                               &zk_plan_store) == BOOL_TRUE)
    {
        zk_plan_loaded = BOOL_TRUE;
        zk_plan_clear_last_exec();
        return;
    }
    zk_plan_reset_store(&zk_plan_store);
    zk_plan_loaded = BOOL_TRUE;
    zk_plan_clear_last_exec();
}

static void zk_plan_ensure_loaded(void)
{
    if (zk_plan_loaded != BOOL_TRUE)
    {
        zk_work_plan_init();
    }
}

static int zk_plan_get_number(cJSON *object, const char *key, int *value)
{
    cJSON *node;

    node = cJSON_GetObjectItem(object, key);
    if (node == NULL)
    {
        return 5;
    }
    if (!cJSON_IsNumber(node))
    {
        return 2;
    }
    *value = node->valueint;
    return 0;
}

static int zk_plan_parse_two_digits(const char *text, u8 *value)
{
    if (text[0] < '0' || text[0] > '9' || text[1] < '0' || text[1] > '9')
    {
        return -1;
    }
    *value = (u8)((text[0] - '0') * 10 + (text[1] - '0'));
    return 0;
}

static int zk_plan_parse_year(const char *text, u16 *year)
{
    u16 value;
    int i;

    if (strncmp(text, "FFFF", 4) == 0)
    {
        *year = 0xffff;
        return 0;
    }
    value = 0;
    for (i = 0; i < 4; ++i)
    {
        if (text[i] < '0' || text[i] > '9')
        {
            return -1;
        }
        value = (u16)(value * 10 + (text[i] - '0'));
    }
    *year = value;
    return 0;
}

static int zk_plan_is_leap_year(u16 year)
{
    if (year == 0xffff)
    {
        return 0;
    }
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

static int zk_plan_days_in_month(u16 year, u8 mon)
{
    static const u8 days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (mon < 1 || mon > 12)
    {
        return 0;
    }
    if (mon == 2 && zk_plan_is_leap_year(year))
    {
        return 29;
    }
    return days[mon - 1];
}

static int zk_plan_datetime_valid(const zk_plan_datetime_t *dt)
{
    if (dt->year != 0xffff && (dt->year < 2020 || dt->year > 2099))
    {
        return 0;
    }
    if (dt->mon < 1 || dt->mon > 12)
    {
        return 0;
    }
    if (dt->day < 1 || dt->day > zk_plan_days_in_month(dt->year, dt->mon))
    {
        return 0;
    }
    if (dt->hour > 23 || dt->min > 59 || dt->sec > 59)
    {
        return 0;
    }
    return 1;
}

static int zk_plan_parse_datetime_text(const char *text, zk_plan_datetime_t *dt)
{
    u16 year;

    if (text == NULL || dt == NULL || strlen(text) != 19)
    {
        return 8;
    }
    if (text[4] != '-' || text[7] != '-' || text[10] != ' ' ||
        text[13] != ':' || text[16] != ':')
    {
        return 8;
    }
    memset(dt, 0, sizeof(*dt));
    if (zk_plan_parse_year(text, &year) != 0 ||
        zk_plan_parse_two_digits(text + 5, &dt->mon) != 0 ||
        zk_plan_parse_two_digits(text + 8, &dt->day) != 0 ||
        zk_plan_parse_two_digits(text + 11, &dt->hour) != 0 ||
        zk_plan_parse_two_digits(text + 14, &dt->min) != 0 ||
        zk_plan_parse_two_digits(text + 17, &dt->sec) != 0)
    {
        return 8;
    }
    dt->year = year;
    return zk_plan_datetime_valid(dt) ? 0 : 3;
}

static int zk_plan_compare_datetime(const zk_plan_datetime_t *left,
                                    const zk_plan_datetime_t *right)
{
    if (left->year != right->year)
    {
        return (left->year < right->year) ? -1 : 1;
    }
    if (left->mon != right->mon)
    {
        return (left->mon < right->mon) ? -1 : 1;
    }
    if (left->day != right->day)
    {
        return (left->day < right->day) ? -1 : 1;
    }
    if (left->hour != right->hour)
    {
        return (left->hour < right->hour) ? -1 : 1;
    }
    if (left->min != right->min)
    {
        return (left->min < right->min) ? -1 : 1;
    }
    if (left->sec != right->sec)
    {
        return (left->sec < right->sec) ? -1 : 1;
    }
    return 0;
}

static int zk_plan_compare_month_time(const zk_plan_datetime_t *left,
                                      const zk_plan_datetime_t *right)
{
    if (left->mon != right->mon)
    {
        return (left->mon < right->mon) ? -1 : 1;
    }
    if (left->day != right->day)
    {
        return (left->day < right->day) ? -1 : 1;
    }
    if (left->hour != right->hour)
    {
        return (left->hour < right->hour) ? -1 : 1;
    }
    if (left->min != right->min)
    {
        return (left->min < right->min) ? -1 : 1;
    }
    if (left->sec != right->sec)
    {
        return (left->sec < right->sec) ? -1 : 1;
    }
    return 0;
}

static int zk_plan_parse_week(const char *text, u8 *week_mask)
{
    int i;
    u8 mask;

    if (text == NULL || strlen(text) != 7)
    {
        return 8;
    }
    mask = 0;
    for (i = 0; i < 7; ++i)
    {
        if (text[i] == '1')
        {
            mask = (u8)(mask | (1U << i));
        }
        else if (text[i] != '0')
        {
            return 3;
        }
    }
    *week_mask = mask;
    return 0;
}

static int zk_plan_parse_time_text(const char *text, u16 *minute)
{
    u8 hour;
    u8 min;

    if (text == NULL || strlen(text) != 5 || text[2] != ':')
    {
        return 8;
    }
    if (zk_plan_parse_two_digits(text, &hour) != 0 ||
        zk_plan_parse_two_digits(text + 3, &min) != 0)
    {
        return 8;
    }
    if (hour > 23 || min > 59)
    {
        return 3;
    }
    *minute = (u16)(hour * 60U + min);
    return 0;
}

static int zk_plan_parse_duration(cJSON *node, u16 *minute)
{
    int value;

    if (node == NULL || !cJSON_IsNumber(node))
    {
        return 2;
    }
    value = node->valueint;
    if (value < 1 || value > 780)
    {
        return 3;
    }
    *minute = (u16)value;
    return 0;
}

static int zk_plan_parse_cns(cJSON *job, u8 *cns_mask)
{
    cJSON *cns;
    cJSON *item;
    int count;
    int index;
    int value;
    u8 mask;

    cns = cJSON_GetObjectItem(job, "cns");
    if (cns == NULL)
    {
        return 5;
    }
    if (!cJSON_IsArray(cns))
    {
        return 2;
    }
    count = cJSON_GetArraySize(cns);
    if (count <= 0)
    {
        return 7;
    }
    if (count > 2)
    {
        return 6;
    }
    mask = 0;
    for (index = 0; index < count; ++index)
    {
        item = cJSON_GetArrayItem(cns, index);
        if (item == NULL || !cJSON_IsNumber(item))
        {
            return 2;
        }
        value = item->valueint;
        if (value < 1 || value > 2)
        {
            return 3;
        }
        mask = (u8)(mask | (1U << (value - 1)));
    }
    *cns_mask = mask;
    return 0;
}

static int zk_plan_parse_job(cJSON *job, zk_plan_job_t *out, u8 plan_type)
{
    cJSON *schedule_array;
    cJSON *bri_array;
    cJSON *schedule_item;
    cJSON *bri_item;
    cJSON *timetp;
    int count;
    int bri_count;
    int index;
    int err;
    int bri;
    int use_duration;

    if (job == NULL || !cJSON_IsObject(job))
    {
        return 2;
    }
    memset(out, 0, sizeof(*out));
    err = zk_plan_parse_cns(job, &out->cns_mask);
    if (err != 0)
    {
        return err;
    }

    timetp = cJSON_GetObjectItem(job, "timetp");
    if (timetp != NULL)
    {
        if (!cJSON_IsNumber(timetp))
        {
            return 2;
        }
        if (timetp->valueint < 0 || timetp->valueint > 1)
        {
            return 1;
        }
    }
    out->timetp = (u8)((timetp != NULL) ? timetp->valueint : 0);
    if (plan_type == 1 && out->timetp != 0)
    {
        return 1;
    }

    use_duration = (out->timetp == 1) ? 1 : 0;
    schedule_array = cJSON_GetObjectItem(job, use_duration ? "dTime" : "time");
    bri_array = cJSON_GetObjectItem(job, "bri");
    if (schedule_array == NULL || bri_array == NULL)
    {
        return 5;
    }
    if (!cJSON_IsArray(schedule_array) || !cJSON_IsArray(bri_array))
    {
        return 2;
    }
    count = cJSON_GetArraySize(schedule_array);
    bri_count = cJSON_GetArraySize(bri_array);
    if (count <= 0 || bri_count <= 0)
    {
        return 7;
    }
    if (count > ZK_PLAN_MAX_ACTIONS || bri_count > ZK_PLAN_MAX_ACTIONS)
    {
        return 6;
    }
    if (count != bri_count)
    {
        return 9;
    }
    out->action_count = (u8)count;
    for (index = 0; index < count; ++index)
    {
        u16 minute;

        schedule_item = cJSON_GetArrayItem(schedule_array, index);
        bri_item = cJSON_GetArrayItem(bri_array, index);
        if (bri_item == NULL || !cJSON_IsNumber(bri_item))
        {
            return 2;
        }
        if (use_duration)
        {
            err = zk_plan_parse_duration(schedule_item, &minute);
        }
        else
        {
            if (schedule_item == NULL || !cJSON_IsString(schedule_item) ||
                schedule_item->valuestring == NULL)
            {
                return 2;
            }
            err = zk_plan_parse_time_text(schedule_item->valuestring,
                                          &minute);
        }
        if (err != 0)
        {
            return err;
        }
        out->actions[index].minute = minute;
        bri = bri_item->valueint;
        if (!(bri == 0 || (bri >= 10 && bri <= 100)))
        {
            return 3;
        }
        out->actions[index].bri = (u8)bri;
    }
    return 0;
}

static int zk_plan_parse_enabled(cJSON *plan, zk_plan_record_t *out)
{
    cJSON *node;
    cJSON *jobs;
    cJSON *job;
    int value;
    int err;
    int job_count;
    int index;

    memset(out, 0, sizeof(*out));
    out->valid = 1;
    err = zk_plan_get_number(plan, "id", &value);
    if (err != 0)
    {
        return err;
    }
    if (value < 1 || value > ZK_PLAN_MAX_COUNT)
    {
        return 3;
    }
    out->id = (u8)value;

    err = zk_plan_get_number(plan, "en", &value);
    if (err != 0)
    {
        return err;
    }
    if (value != 0 && value != 1)
    {
        return 3;
    }
    out->en = (u8)value;
    if (out->en == 0)
    {
        return 0;
    }

    err = zk_plan_get_number(plan, "type", &value);
    if (err != 0)
    {
        return err;
    }
    if (value != 1 && value != 2)
    {
        return 1;
    }
    out->type = (u8)value;

    err = zk_plan_get_number(plan, "priority", &value);
    if (err != 0)
    {
        return err;
    }
    if (value < 1 || value > 8)
    {
        return 3;
    }
    out->priority = (u8)value;

    node = cJSON_GetObjectItem(plan, "sDate");
    if (node == NULL)
    {
        return 5;
    }
    if (!cJSON_IsString(node) || node->valuestring == NULL)
    {
        return 2;
    }
    err = zk_plan_parse_datetime_text(node->valuestring, &out->start);
    if (err != 0)
    {
        return err;
    }

    node = cJSON_GetObjectItem(plan, "eDate");
    if (node == NULL)
    {
        return 5;
    }
    if (!cJSON_IsString(node) || node->valuestring == NULL)
    {
        return 2;
    }
    err = zk_plan_parse_datetime_text(node->valuestring, &out->end);
    if (err != 0)
    {
        return err;
    }
    if ((out->start.year == 0xffff) != (out->end.year == 0xffff))
    {
        return 9;
    }
    if (out->start.year != 0xffff &&
        zk_plan_compare_datetime(&out->start, &out->end) > 0)
    {
        return 9;
    }

    /* 解析 setoffset / riseoffset（仅 type=2 有效，缺省为0） */
    out->setoffset = 0;
    out->riseoffset = 0;
    if (out->type == 2)
    {
        node = cJSON_GetObjectItem(plan, "setoffset");
        if (node != NULL)
        {
            if (!cJSON_IsNumber(node)) return 2;
            if (node->valueint < -128 || node->valueint > 127) return 3;
            out->setoffset = (s8)node->valueint;
        }
        node = cJSON_GetObjectItem(plan, "riseoffset");
        if (node != NULL)
        {
            if (!cJSON_IsNumber(node)) return 2;
            if (node->valueint < -128 || node->valueint > 127) return 3;
            out->riseoffset = (s8)node->valueint;
        }
    }

    /* week：type=1 必填，type=2 可选（缺省=0表示全匹配） */
    node = cJSON_GetObjectItem(plan, "week");
    if (node == NULL)
    {
        if (out->type == 1)
        {
            return 5;
        }
        out->week_mask = 0;
    }
    else
    {
        if (!cJSON_IsString(node) || node->valuestring == NULL)
        {
            return 2;
        }
        err = zk_plan_parse_week(node->valuestring, &out->week_mask);
        if (err != 0)
        {
            return err;
        }
    }

    jobs = cJSON_GetObjectItem(plan, "jobs");
    if (jobs == NULL)
    {
        return 5;
    }
    if (!cJSON_IsArray(jobs))
    {
        return 2;
    }
    job_count = cJSON_GetArraySize(jobs);
    if (job_count <= 0)
    {
        return 7;
    }
    if (job_count > ZK_PLAN_MAX_JOBS)
    {
        return 6;
    }
    out->job_count = (u8)job_count;
    for (index = 0; index < job_count; ++index)
    {
        job = cJSON_GetArrayItem(jobs, index);
        err = zk_plan_parse_job(job, &out->jobs[index], out->type);
        if (err != 0)
        {
            return err;
        }
    }
    return 0;
}

static int zk_plan_upsert(cJSON *plan)
{
    zk_plan_record_t record;
    zk_plan_store_t next;
    int err;

    if (plan == NULL || !cJSON_IsObject(plan))
    {
        return 2;
    }
    err = zk_plan_parse_enabled(plan, &record);
    if (err != 0)
    {
        return err;
    }

    zk_plan_ensure_loaded();
    if (record.en == 0 && zk_plan_store.plans[record.id - 1].valid == 1)
    {
        record = zk_plan_store.plans[record.id - 1];
        record.en = 0;
    }
    next = zk_plan_store;
    next.plans[record.id - 1] = record;
    if (zk_plan_save_store(&next) != BOOL_TRUE)
    {
        return 10;
    }
    return 0;
}

boolean_en zk_handle_plan_message(cJSON *root, const zk_message_header_t *header)
{
    cJSON *dt;
    cJSON *plan;
    int err;

    if (root == NULL || header == NULL)
    {
        return BOOL_FALSE;
    }
    if (strcmp(header->sv, ZK_SV_PLAN) != 0)
    {
        return BOOL_FALSE;
    }
    if (strcmp(header->ct, ZK_CT_WRITE) != 0 &&
        strcmp(header->ct, ZK_CT_READ) != 0)
    {
        return BOOL_FALSE;
    }

    dt = cJSON_GetObjectItem(root, "DT");
    if (dt == NULL || !cJSON_IsObject(dt))
    {
        zk_publish_error_response(header, 5);
        return BOOL_TRUE;
    }
    if (strcmp(header->ct, ZK_CT_READ) == 0)
    {
        err = zk_plan_handle_read(dt, header);
        if (err != 0)
        {
            zk_publish_error_response(header, err);
        }
        return BOOL_TRUE;
    }

    {
        cJSON *del;
        int n;
        int i;

        del = cJSON_GetObjectItem(dt, "del");
        if (del != NULL)
        {
            int del_err;

            if (!cJSON_IsArray(del))
            {
                zk_publish_error_response(header, 2);
                return BOOL_TRUE;
            }
            n = cJSON_GetArraySize(del);
            if (n <= 0)
            {
                zk_publish_error_response(header, 7);
                return BOOL_TRUE;
            }
            del_err = 0;
            for (i = 0; i < n; i++)
            {
                cJSON *item = cJSON_GetArrayItem(del, i);
                if (item == NULL || !cJSON_IsNumber(item))
                {
                    del_err = 2;
                    break;
                }
                del_err = zk_plan_delete(item->valueint);
                if (del_err != 0)
                {
                    break;
                }
            }
            zk_publish_error_response(header, del_err);
            return BOOL_TRUE;
        }
    }

    plan = cJSON_GetObjectItem(dt, "plan");
    if (plan == NULL)
    {
        zk_publish_error_response(header, 5);
        return BOOL_TRUE;
    }
    err = zk_plan_upsert(plan);
    zk_publish_error_response(header, err);
    return BOOL_TRUE;
}

static void zk_plan_current_datetime(zk_plan_datetime_t *dt)
{
    dt->year = apprtc_RtcTime.year;
    dt->mon = apprtc_RtcTime.mon;
    dt->day = apprtc_RtcTime.day;
    dt->hour = apprtc_RtcTime.hour;
    dt->min = apprtc_RtcTime.min;
    dt->sec = apprtc_RtcTime.sec;
    dt->reserved = 0;
}

static int zk_plan_date_active(const zk_plan_record_t *plan,
                               const zk_plan_datetime_t *now)
{
    int start_cmp;
    int end_cmp;

    if (plan->start.year == 0xffff)
    {
        start_cmp = zk_plan_compare_month_time(now, &plan->start);
        end_cmp = zk_plan_compare_month_time(now, &plan->end);
        if (zk_plan_compare_month_time(&plan->start, &plan->end) <= 0)
        {
            return (start_cmp >= 0 && end_cmp <= 0);
        }
        return (start_cmp >= 0 || end_cmp <= 0);
    }
    return (zk_plan_compare_datetime(now, &plan->start) >= 0 &&
            zk_plan_compare_datetime(now, &plan->end) <= 0);
}

static int zk_plan_week_active(const zk_plan_record_t *plan,
                               const zk_plan_datetime_t *now)
{
    u8 idx;

    (void)now;
    idx = (apprtc_RtcTime.week + 5) % 7;
    return (plan->week_mask & (1U << idx)) != 0;
}

static int zk_plan_already_executed(u16 year, u8 mon, u8 day, u16 minute,
                                    u8 plan_index, u8 job_index, u8 action_index)
{
    return (zk_last_exec_year == year &&
            zk_last_exec_mon == mon &&
            zk_last_exec_day == day &&
            zk_last_exec_minute == minute &&
            zk_last_exec_plan == plan_index &&
            zk_last_exec_job == job_index &&
            zk_last_exec_action == action_index);
}

static int zk_plan_rtc_ready(void)
{
    return (apprtc_RtcTime.ready == BOOL_TRUE &&
            apprtc_RtcTime.year >= 2020 &&
            apprtc_RtcTime.mon >= 1 &&
            apprtc_RtcTime.mon <= 12 &&
            apprtc_RtcTime.day >= 1 &&
            apprtc_RtcTime.day <= 31 &&
            apprtc_RtcTime.hour <= 23 &&
            apprtc_RtcTime.min <= 59);
}

static int zk_plan_find_current_match_for_cns(zk_plan_match_t *match, int cns)
{
    zk_plan_datetime_t now;
    const zk_plan_record_t *plan;
    const zk_plan_job_t *job;
    const zk_plan_action_t *action;
    u16 minute;
    int plan_index;
    int job_index;
    int action_index;
    int effective_minute;   /* 实际执行分钟（考虑日出日落偏移后） */
#if ZK_ENABLE_SUNRISE_PLAN
    int sr_minute;          /* 日出分钟（当日） */
    int ss_minute;          /* 日落分钟（当日） */
    int have_sun;           /* 日出日落计算是否有效 */
    int offset_minute;
    int offset_index;
#endif

    if (match == NULL)
    {
        return 0;
    }
    memset(match, 0, sizeof(*match));
    if (!zk_plan_rtc_ready())
    {
        return 0;
    }

    /* 尝试获取日出日落（用于 type=2），失败时 type=2 跳过 */
#if ZK_ENABLE_SUNRISE_PLAN
    sr_minute = 0;
    ss_minute = 0;
    have_sun = (zk_sunriset_get(&sr_minute, &ss_minute) == 0) ? 1 : 0;
#endif

    zk_plan_current_datetime(&now);
    minute = (u16)(now.hour * 60U + now.min);
    for (plan_index = 0; plan_index < ZK_PLAN_MAX_COUNT; ++plan_index)
    {
        plan = &zk_plan_store.plans[plan_index];
        if (plan->valid != 1 || plan->en != 1)
        {
            continue;
        }
        if (plan->type != 1 && plan->type != 2)
        {
            continue;
        }
        if (!zk_plan_date_active(plan, &now))
        {
            continue;
        }
        /* type=1 校验星期掩码，type=2 不校验（week_mask=0 表示全匹配） */
        if (plan->type == 1 && !zk_plan_week_active(plan, &now))
        {
            continue;
        }
#if ZK_ENABLE_SUNRISE_PLAN
        /* type=2 且无法计算日出日落时跳过 */
        if (plan->type == 2 && have_sun == 0)
        {
            continue;
        }
#else
        if (plan->type == 2)
        {
            continue;
        }
#endif

        for (job_index = 0; job_index < plan->job_count && job_index < ZK_PLAN_MAX_JOBS; ++job_index)
        {
            job = &plan->jobs[job_index];
            if (cns >= 1 && cns <= 2 &&
                (job->cns_mask & (1U << (cns - 1))) == 0)
            {
                continue;
            }
            for (action_index = 0; action_index < job->action_count && action_index < ZK_PLAN_MAX_ACTIONS; ++action_index)
            {
                action = &job->actions[action_index];

                /* 计算绝对分钟数：timetp=0 为时间点，timetp=1 为日落后时间段。 */
#if ZK_ENABLE_SUNRISE_PLAN
                if (plan->type == 2 && job->timetp == 1)
                {
                    offset_minute = 0;
                    for (offset_index = 0; offset_index < action_index; ++offset_index)
                    {
                        offset_minute += (int)job->actions[offset_index].minute;
                    }
                    effective_minute = ss_minute + (int)plan->setoffset + offset_minute;
                }
                else
#endif
                {
                    effective_minute = action->minute;
                }

                /* 归一化到 [0, 1440) */
                while (effective_minute < 0)    effective_minute += 1440;
                while (effective_minute >= 1440) effective_minute -= 1440;

                if ((u16)effective_minute != minute)
                {
                    if (((u16)(effective_minute + 1) % 1440) != minute)
                    {
                        continue;
                    }
                }
                /* priority: 1(lowest)~8(highest), larger value wins */
                if (match->found == 0 || plan->priority > match->priority)
                {
                    match->found = 1;
                    match->id = plan->id;
                    match->priority = plan->priority;
                    match->bri = action->bri;
                    match->job_index = (u8)job_index;
                    match->action_index = (u8)action_index;
                }
            }
        }
    }
    return match->found != 0;
}

static int zk_plan_find_current_match(zk_plan_match_t *match)
{
    return zk_plan_find_current_match_for_cns(match, 0);
}

static void zk_plan_format_datetime(const zk_plan_datetime_t *dt, char *buf, int buf_size)
{
    if (dt->year == 0xffff)
    {
        snprintf(buf, buf_size, "FFFF-%02u-%02u %02u:%02u:%02u",
                 dt->mon, dt->day, dt->hour, dt->min, dt->sec);
        return;
    }
    snprintf(buf, buf_size, "%04u-%02u-%02u %02u:%02u:%02u",
             dt->year, dt->mon, dt->day, dt->hour, dt->min, dt->sec);
}

static void zk_plan_format_time(u16 minute, char *buf, int buf_size)
{
    snprintf(buf, buf_size, "%02u:%02u", minute / 60U, minute % 60U);
}

static void zk_plan_format_week(u8 week_mask, char *buf)
{
    int index;

    for (index = 0; index < 7; ++index)
    {
        buf[index] = (week_mask & (1U << index)) ? '1' : '0';
    }
    buf[7] = '\0';
}

static cJSON *zk_plan_record_to_json(const zk_plan_record_t *record)
{
    cJSON *plan;
    cJSON *jobs;
    cJSON *job_json;
    cJSON *cns;
    cJSON *schedule_array;
    cJSON *bri_array;
    char text[20];
    char week[8];
    const zk_plan_job_t *job;
    int job_index;
    int action_index;

    plan = cJSON_CreateObject();
    if (plan == NULL)
    {
        return NULL;
    }
    cJSON_AddNumberToObject(plan, "id", record->id);
    cJSON_AddNumberToObject(plan, "en", record->en);
    if (record->en == 0)
    {
        return plan;
    }

    cJSON_AddNumberToObject(plan, "type", record->type);
    cJSON_AddNumberToObject(plan, "priority", record->priority);
    zk_plan_format_datetime(&record->start, text, sizeof(text));
    cJSON_AddStringToObject(plan, "sDate", text);
    zk_plan_format_datetime(&record->end, text, sizeof(text));
    cJSON_AddStringToObject(plan, "eDate", text);
    zk_plan_format_week(record->week_mask, week);
    cJSON_AddStringToObject(plan, "week", week);
    if (record->type == 2)
    {
        cJSON_AddNumberToObject(plan, "setoffset", record->setoffset);
        cJSON_AddNumberToObject(plan, "riseoffset", record->riseoffset);
    }

    jobs = cJSON_CreateArray();
    if (jobs == NULL)
    {
        cJSON_Delete(plan);
        return NULL;
    }
    cJSON_AddItemToObject(plan, "jobs", jobs);
    for (job_index = 0; job_index < record->job_count && job_index < ZK_PLAN_MAX_JOBS; ++job_index)
    {
        job = &record->jobs[job_index];

        job_json = cJSON_CreateObject();
        cns = cJSON_CreateArray();
        schedule_array = cJSON_CreateArray();
        bri_array = cJSON_CreateArray();
        if (job_json == NULL || cns == NULL || schedule_array == NULL || bri_array == NULL)
        {
            if (job_json != NULL)
            {
                cJSON_Delete(job_json);
            }
            if (cns != NULL)
            {
                cJSON_Delete(cns);
            }
            if (schedule_array != NULL)
            {
                cJSON_Delete(schedule_array);
            }
            if (bri_array != NULL)
            {
                cJSON_Delete(bri_array);
            }
            cJSON_Delete(plan);
            return NULL;
        }
        if ((job->cns_mask & 0x01U) != 0)
        {
            cJSON_AddItemToArray(cns, cJSON_CreateNumber(1));
        }
        if ((job->cns_mask & 0x02U) != 0)
        {
            cJSON_AddItemToArray(cns, cJSON_CreateNumber(2));
        }
        cJSON_AddItemToObject(job_json, "cns", cns);
        cJSON_AddNumberToObject(job_json, "timetp", job->timetp);
        for (action_index = 0; action_index < job->action_count && action_index < ZK_PLAN_MAX_ACTIONS; ++action_index)
        {
            if (job->timetp == 1)
            {
                cJSON_AddItemToArray(schedule_array,
                                     cJSON_CreateNumber(job->actions[action_index].minute));
            }
            else
            {
                zk_plan_format_time(job->actions[action_index].minute, text, sizeof(text));
                cJSON_AddItemToArray(schedule_array, cJSON_CreateString(text));
            }
            cJSON_AddItemToArray(bri_array, cJSON_CreateNumber(job->actions[action_index].bri));
        }
        cJSON_AddItemToObject(job_json, (job->timetp == 1) ? "dTime" : "time",
                              schedule_array);
        cJSON_AddItemToObject(job_json, "bri", bri_array);
        cJSON_AddItemToArray(jobs, job_json);
    }
    return plan;
}

static int zk_plan_build_nid_dt(cJSON *dt, void *ctx)
{
    cJSON *array;
    int index;

    (void)ctx;
    array = cJSON_CreateArray();
    if (array == NULL)
    {
        return -1;
    }
    cJSON_AddItemToObject(dt, "nid", array);
    for (index = 0; index < ZK_PLAN_MAX_COUNT; ++index)
    {
        if (zk_plan_store.plans[index].valid == 1 &&
            zk_plan_store.plans[index].en == 1)
        {
            cJSON_AddItemToArray(array, cJSON_CreateNumber(zk_plan_store.plans[index].id));
        }
    }
    return 0;
}

static int zk_plan_build_sunriset_dt(cJSON *dt, void *ctx)
{
#if ZK_ENABLE_SUNRISE_PLAN
    cJSON *array;
    int sr_minute;
    int ss_minute;
    char text[6];

    (void)ctx;
    if (zk_sunriset_get(&sr_minute, &ss_minute) != 0)
    {
        return -1;
    }

    array = cJSON_CreateArray();
    if (array == NULL)
    {
        return -1;
    }
    snprintf(text, sizeof(text), "%02u:%02u", (u16)sr_minute / 60U, (u16)sr_minute % 60U);
    cJSON_AddItemToArray(array, cJSON_CreateString(text));
    snprintf(text, sizeof(text), "%02u:%02u", (u16)ss_minute / 60U, (u16)ss_minute % 60U);
    cJSON_AddItemToArray(array, cJSON_CreateString(text));
    cJSON_AddItemToObject(dt, "sr", array);
    return 0;
#else
    (void)dt;
    (void)ctx;
    return -1;
#endif
}

static int zk_plan_build_record_dt(cJSON *dt, void *ctx)
{
    cJSON *plan;

    plan = zk_plan_record_to_json((const zk_plan_record_t *)ctx);
    if (plan == NULL)
    {
        return -1;
    }
    cJSON_AddItemToObject(dt, "plan", plan);
    return 0;
}

static int zk_plan_delete(int id)
{
    zk_plan_store_t next;

    if (id < 1 || id > ZK_PLAN_MAX_COUNT)
    {
        return 3;
    }
    zk_plan_ensure_loaded();
    if (zk_plan_store.plans[id - 1].valid != 1)
    {
        return 1;
    }
    next = zk_plan_store;
    next.plans[id - 1].valid = 0;
    if (zk_plan_save_store(&next) != BOOL_TRUE)
    {
        return 10;
    }
    return 0;
}

static int zk_plan_publish_dt_response(const zk_message_header_t *header,
                                       zk_response_dt_builder_t builder,
                                       void *ctx)
{
    if (zk_publish_response_with_dt(header, 0, builder, ctx) != 0)
    {
        zk_publish_error_response(header, 12);
    }
    return 0;
}

static int zk_plan_handle_read(cJSON *dt, const zk_message_header_t *header)
{
    cJSON *do_node;
    cJSON *id_node;
    cJSON *now_node;
    int id;
    zk_plan_match_t match;

    zk_plan_ensure_loaded();

    now_node = cJSON_GetObjectItem(dt, "now");
    if (now_node != NULL)
    {
        if (!cJSON_IsNumber(now_node))
        {
            return 2;
        }
        if (now_node->valueint < 1 || now_node->valueint > 2)
        {
            return 3;
        }
        zk_plan_find_current_match_for_cns(&match, now_node->valueint);
        if (!match.found)
        {
            return zk_plan_publish_dt_response(header, NULL, NULL);
        }
        return zk_plan_publish_dt_response(header, zk_plan_build_record_dt,
                                           &zk_plan_store.plans[match.id - 1]);
    }

    do_node = cJSON_GetObjectItem(dt, "DO");
    if (do_node != NULL)
    {
        if (!cJSON_IsString(do_node) || do_node->valuestring == NULL)
        {
            return 2;
        }
        if (strcmp(do_node->valuestring, "nid") == 0)
        {
            return zk_plan_publish_dt_response(header, zk_plan_build_nid_dt, NULL);
        }
        if (strcmp(do_node->valuestring, "sr") == 0)
        {
            return zk_plan_publish_dt_response(header, zk_plan_build_sunriset_dt, NULL);
        }
        return 1;
    }

    id_node = cJSON_GetObjectItem(dt, "id");
    if (id_node == NULL)
    {
        return 5;
    }
    if (!cJSON_IsNumber(id_node))
    {
        return 2;
    }
    id = id_node->valueint;
    if (id < 1 || id > ZK_PLAN_MAX_COUNT)
    {
        return 3;
    }
    if (zk_plan_store.plans[id - 1].valid != 1)
    {
        return 1;
    }
    return zk_plan_publish_dt_response(header, zk_plan_build_record_dt,
                                       &zk_plan_store.plans[id - 1]);
}

void zk_work_plan_process(void)
{
    zk_plan_datetime_t now;
    u16 minute;
    zk_plan_match_t match;

    zk_plan_ensure_loaded();
    if (!zk_plan_find_current_match(&match))
    {
        return;
    }

    zk_plan_current_datetime(&now);
    minute = (u16)(now.hour * 60U + now.min);
    if (zk_plan_already_executed(now.year, now.mon, now.day, minute,
                                 (u8)(match.id - 1),
                                 match.job_index,
                                 match.action_index))
    {
        return;
    }

    printf("[plan] exec id=%d job=%d act=%d bri=%d\n",
           match.id, match.job_index, match.action_index, match.bri);
    zk_apply_plan_brightness(match.bri);
    zk_last_exec_year = now.year;
    zk_last_exec_mon = now.mon;
    zk_last_exec_day = now.day;
    zk_last_exec_minute = minute;
    zk_last_exec_plan = (u8)(match.id - 1);
    zk_last_exec_job = match.job_index;
    zk_last_exec_action = match.action_index;
}
