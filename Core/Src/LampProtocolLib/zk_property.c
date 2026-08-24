#include "zk_property.h"
#include "zk_protocol_internal.h"
#include "Portable.h"
#include "NbDriver.h"
#include "factory_user_data.h"
#include "sys_data.h"
#include "sys_pwm.h"
#include "sys_persistent_storage.h"
#include <string.h>

extern uint8 simCardICCID[22];

static zk_device_config_t zk_dev_cfg;

#define ZK_FACTORY_PROFILE_PROTECTED_ERROR 20
#define ZK_FACTORY_CURRENT_ZERO_ERROR      22
#define ZK_FACTORY_CURRENT_HWMAX_ERROR     25
#define ZK_FACTORY_PROFILE_MISMATCH_ERROR  27
#define ZK_FACTORY_CALIBRATION_ACTIVE_ERROR 28

/*
 * 属性模块持有 zk_dev_cfg：协议核心只通过 getter 读取，属性写入先写
 * Flash 成功后再提交到 RAM，避免平台下发异常值时污染运行配置。
 */

static void zk_property_copy_digits(char *dst, int dst_size, const char *src, int max_digits)
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

static void zk_property_put_s32(u8 *payload, u16 offset, s32 value)
{
    sys_persistent_put_u32_le(payload + offset, (u32)value);
}

static s32 zk_property_get_s32(const u8 *payload, u16 offset)
{
    return (s32)sys_persistent_get_u32_le(payload + offset);
}

static void zk_property_persistent_encode(const zk_device_config_t *config,
                                          u8 payload[SYS_PERSISTENT_CONFIG_PROPERTY_LENGTH])
{
    u8 index;

    memset(payload, 0, SYS_PERSISTENT_CONFIG_PROPERTY_LENGTH);
    zk_property_put_s32(payload, 0x000U, (s32)config->lng);
    zk_property_put_s32(payload, 0x004U, (s32)config->lat);
    zk_property_put_s32(payload, 0x008U, config->zone);
    zk_property_put_s32(payload, 0x00CU, config->cns);
    zk_property_put_s32(payload, 0x010U, config->dimTp);
    zk_property_put_s32(payload, 0x014U, config->polar);
    zk_property_put_s32(payload, 0x018U, config->dlmt);
    zk_property_put_s32(payload, 0x01CU, config->ulmt);
    zk_property_put_s32(payload, 0x020U, config->rti);
    zk_property_put_s32(payload, 0x024U, config->rtPwr);
    zk_property_put_s32(payload, 0x028U, config->di);
    zk_property_put_s32(payload, 0x02CU, config->sBri);
    zk_property_put_s32(payload, 0x030U, config->sBriTm);
    memcpy(payload + 0x034U, config->svrIp, 32U);
    zk_property_put_s32(payload, 0x054U, config->svrPort);
    zk_property_put_s32(payload, 0x058U, config->uPeriod);
    zk_property_put_s32(payload, 0x05CU, config->hPeriod);
    zk_property_put_s32(payload, 0x060U, config->tPeriod);
    for (index = 0U; index < 17U; ++index)
    {
        zk_property_put_s32(payload, (u16)(0x064U + index * 4U),
                            config->almValue[index]);
        zk_property_put_s32(payload, (u16)(0x0A8U + index * 4U),
                            config->almRecValue[index]);
        zk_property_put_s32(payload, (u16)(0x0ECU + index * 4U),
                            config->almEn[index]);
    }
}

static void zk_property_persistent_decode(
    zk_device_config_t *config,
    const u8 payload[SYS_PERSISTENT_CONFIG_PROPERTY_LENGTH])
{
    u8 index;

    config->lng = zk_property_get_s32(payload, 0x000U);
    config->lat = zk_property_get_s32(payload, 0x004U);
    config->zone = zk_property_get_s32(payload, 0x008U);
    config->cns = zk_property_get_s32(payload, 0x00CU);
    config->dimTp = zk_property_get_s32(payload, 0x010U);
    config->polar = zk_property_get_s32(payload, 0x014U);
    config->dlmt = zk_property_get_s32(payload, 0x018U);
    config->ulmt = zk_property_get_s32(payload, 0x01CU);
    config->rti = zk_property_get_s32(payload, 0x020U);
    config->rtPwr = zk_property_get_s32(payload, 0x024U);
    config->di = zk_property_get_s32(payload, 0x028U);
    config->sBri = zk_property_get_s32(payload, 0x02CU);
    config->sBriTm = zk_property_get_s32(payload, 0x030U);
    memcpy(config->svrIp, payload + 0x034U, 32U);
    config->svrIp[31] = '\0';
    config->svrPort = zk_property_get_s32(payload, 0x054U);
    config->uPeriod = zk_property_get_s32(payload, 0x058U);
    config->hPeriod = zk_property_get_s32(payload, 0x05CU);
    config->tPeriod = zk_property_get_s32(payload, 0x060U);
    for (index = 0U; index < 17U; ++index)
    {
        config->almValue[index] =
            zk_property_get_s32(payload, (u16)(0x064U + index * 4U));
        config->almRecValue[index] =
            zk_property_get_s32(payload, (u16)(0x0A8U + index * 4U));
        config->almEn[index] =
            zk_property_get_s32(payload, (u16)(0x0ECU + index * 4U));
    }
}

static boolean_en zk_property_flash_load(zk_device_config_t *config)
{
    u8 payload[SYS_PERSISTENT_CONFIG_PROPERTY_LENGTH];

    if (sys_persistent_config_read_section(
            SYS_PERSISTENT_CONFIG_PROPERTY_OFFSET,
            payload,
            sizeof(payload),
            NULL) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    zk_property_persistent_decode(config, payload);
    return BOOL_TRUE;
}

static boolean_en zk_property_flash_store_config(
    const zk_device_config_t *config)
{
    u8 payload[SYS_PERSISTENT_CONFIG_PROPERTY_LENGTH];

    zk_property_persistent_encode(config, payload);
    return sys_persistent_config_update_section(
        SYS_PERSISTENT_CONFIG_PROPERTY_OFFSET,
        payload,
        sizeof(payload),
        NULL);
}

static boolean_en zk_property_flash_store_transaction(
    const zk_device_config_t *config,
    boolean_en store_property,
    const u8 *factory_buf,
    boolean_en store_factory)
{
    u8 property_payload[SYS_PERSISTENT_CONFIG_PROPERTY_LENGTH];
    sys_persistent_section_update_st updates[2];
    u8 update_count = 0U;

    if (store_property == BOOL_TRUE)
    {
        if (config == NULL)
        {
            return BOOL_FALSE;
        }
        zk_property_persistent_encode(config, property_payload);
        updates[update_count].offset = SYS_PERSISTENT_CONFIG_PROPERTY_OFFSET;
        updates[update_count].data = property_payload;
        updates[update_count].length = sizeof(property_payload);
        ++update_count;
    }
    if (store_factory == BOOL_TRUE)
    {
        if (factory_buf == NULL)
        {
            return BOOL_FALSE;
        }
        updates[update_count].offset = SYS_PERSISTENT_CONFIG_FACTORY_OFFSET;
        updates[update_count].data = factory_buf;
        updates[update_count].length = SYS_PERSISTENT_CONFIG_FACTORY_LENGTH;
        ++update_count;
    }
    return (update_count != 0U) ?
           sys_persistent_config_update_sections(updates, update_count, NULL) :
           BOOL_TRUE;
}

static void zk_device_config_refresh_iccid_field(zk_device_config_t *config)
{
    if (config == NULL)
    {
        return;
    }
    zk_property_copy_digits(config->iccid, sizeof(config->iccid), (const char *)simCardICCID, 20);
    if (strlen(config->iccid) == 0)
    {
        strcpy(config->iccid, NB_ICCID_DEFAULT);
    }
}

static void zk_device_config_set_defaults(zk_device_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->protId = 100;
    strcpy(config->clas, "DL-MXG");
    strcpy(config->prottp, "DL-50Z-56T-MXG");
    config->hver = 1;
    config->sver = APP_VERSION;
    strcpy(config->mver, "EC801E");
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
    /* 设置告警阈值默认值："设备出厂配置"即默认阈值 */
    config->almValue[0] = 3200;  config->almRecValue[0] = 2640;  config->almEn[0] = 1;  /* 10000-输入过压 */
    config->almValue[1] = 800;   config->almRecValue[1] = 820;   config->almEn[1] = 1;  /* 10001-输入欠压 */
    config->almValue[2] = 0;     config->almRecValue[2] = 0;     config->almEn[2] = 1;  /* 10002-输入过流（动态按额定电流%计算） */
    config->almValue[3] = 0;     config->almRecValue[3] = 0;     config->almEn[3] = 1;  /* 10003-输入欠流（动态按额定电流%计算） */
    config->almValue[4] = 0;     config->almRecValue[4] = 0;     config->almEn[4] = 1;  /* 10004-开灯异常 */
    config->almValue[5] = 0;     config->almRecValue[5] = 0;     config->almEn[5] = 0;  /* 10005-关灯异常（未接入） */
    config->almValue[6] = 0;     config->almRecValue[6] = 0;     config->almEn[6] = 0;  /* 10006-灯杆倾斜（未接入） */
    config->almValue[7] = 30;    config->almRecValue[7] = 20;    config->almEn[7] = 1;  /* 10007-漏电 */
    config->almValue[8] = 0;     config->almRecValue[8] = 0;     config->almEn[8] = 1;  /* 10008-设备故障（动态按NTC温度） */
    config->almValue[9] = 70;    config->almRecValue[9] = 80;    config->almEn[9] = 1;  /* 10009-输入失电 */
    config->almValue[10] = 600;  config->almRecValue[10] = 550;  config->almEn[10] = 1; /* 10010-输出过压（0.1V） */
    config->almValue[11] = 100;  config->almRecValue[11] = 120;  config->almEn[11] = 1; /* 10011-输出欠压（0.1V） */
    config->almValue[12] = 0;    config->almRecValue[12] = 0;    config->almEn[12] = 1; /* 10012-输出过流（动态按SET_OUTCUR计算） */
    config->almValue[13] = 0;    config->almRecValue[13] = 0;    config->almEn[13] = 0; /* 10013-输出欠流（默认禁用） */
    config->almValue[14] = 0;    config->almRecValue[14] = 0;    config->almEn[14] = 1; /* 10014-过功率（动态按SET_OUTCUR×Vo计算） */
    config->almValue[15] = 0;    config->almRecValue[15] = 0;    config->almEn[15] = 1; /* 10015-TC过温（动态取INNRE_TEMP_PRO） */
    config->almValue[16] = 0;    config->almRecValue[16] = 0;    config->almEn[16] = 1; /* 10016-控制器过温（动态取INNRE_TEMP_PRO） */
}

boolean_en zk_device_config_persistent_defaults(u8 *payload, u16 length)
{
    zk_device_config_t defaults;

    if (payload == NULL || length != SYS_PERSISTENT_CONFIG_PROPERTY_LENGTH)
    {
        return BOOL_FALSE;
    }
    zk_device_config_set_defaults(&defaults);
    zk_property_persistent_encode(&defaults, payload);
    return BOOL_TRUE;
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

const zk_device_config_t *zk_device_config_get(void)
{
    return &zk_dev_cfg;
}

boolean_en zk_device_config_restore_defaults(void)
{
    zk_device_config_t restore_config;

    /* 恢复出厂参数也走属性 Flash 通道，避免只改 RAM 导致重启后回滚。 */
    zk_device_config_set_defaults(&restore_config);
    if (zk_property_flash_store_config(&restore_config) == BOOL_FALSE)
    {
        return BOOL_FALSE;
    }
    zk_dev_cfg = restore_config;
    return BOOL_TRUE;
}

boolean_en zk_device_config_commit(const zk_device_config_t *config)
{
    if (config == NULL)
    {
        return BOOL_FALSE;
    }
    if (zk_property_flash_store_config(config) == BOOL_FALSE)
    {
        return BOOL_FALSE;
    }
    zk_dev_cfg = *config;
    return BOOL_TRUE;
}


static void zk_add_dev_info_prop(cJSON *dt_root)
{
    cJSON *item;
    const zk_mqtt_config_t *mqtt_cfg;

    item = zk_cjson_create_tx_object("DevInfo");
    if (item == NULL)
    {
        return;
    }
    mqtt_cfg = zk_mqtt_get_config();
    cJSON_AddNumberToObject(item, "protId", zk_dev_cfg.protId);
    cJSON_AddStringToObject(item, "SN", (mqtt_cfg != NULL) ? mqtt_cfg->imei : "");
    cJSON_AddStringToObject(item, "clas", zk_dev_cfg.clas);
    cJSON_AddStringToObject(item, "prodtp", zk_dev_cfg.prottp); /* JSON字段按协议使用prodtp */
    cJSON_AddNumberToObject(item, "hver", zk_dev_cfg.hver);
    cJSON_AddNumberToObject(item, "sver", zk_dev_cfg.sver);
    cJSON_AddItemToObject(dt_root, "DevInfo", item);
}

static void zk_add_mdl_info_prop(cJSON *dt_root)
{
    cJSON *item;
    const zk_mqtt_config_t *mqtt_cfg;

    item = zk_cjson_create_tx_object("MdlInfo");
    if (item == NULL)
    {
        return;
    }
    mqtt_cfg = zk_mqtt_get_config();
    cJSON_AddStringToObject(item, "mver", zk_dev_cfg.mver);
    cJSON_AddStringToObject(item, "imei", (mqtt_cfg != NULL) ? mqtt_cfg->imei : "");
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

static void zk_add_factory_prop(cJSON *dt_root)
{
    cJSON *item;

    item = zk_cjson_create_tx_object("Factory");
    if (item == NULL)
    {
        return;
    }
    cJSON_AddNumberToObject(item, "MID", MID);
    cJSON_AddNumberToObject(item, "BOUND_OUTPUT_VOLTAGE_01V",
                            BOUND_OUTPUT_VOLTAGE_01V);
    cJSON_AddNumberToObject(item, "SET_OUTCUR", SET_OUTCUR);
    cJSON_AddNumberToObject(item, "HWMAX_OUTCUR", HWMAX_OUTCUR);
    cJSON_AddNumberToObject(item, "OUTPUT_CUR_SENSOR", OUTPUT_CUR_SENSOR);
    cJSON_AddNumberToObject(item, "OP_PWM_OFFSET", OP_PWM_OFFSET);
    cJSON_AddNumberToObject(item, "INNRE_TEMP_PRO_EN", INNRE_TEMP_PRO_EN);
    cJSON_AddNumberToObject(item, "INNRE_TEMP_PRO", INNRE_TEMP_PRO);
    cJSON_AddNumberToObject(item, "CX", CX);
    cJSON_AddItemToObject(dt_root, "Factory", item);
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
    else if (strcmp(name, "Factory") == 0)
    {
        zk_add_factory_prop(dt_root);
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
    /* RunTm/LightTm: 共享运行时统计组构建函数（内部同时生成 RunTm + LightTm 两个属性组）
     * 无论请求其中哪一个，DT中都会同时包含两者，因统计数据需一次计算获取，额外返回相关数据无副作用 */
    else if (strcmp(name, "RunTm") == 0 || strcmp(name, "LightTm") == 0)
    {
        zk_add_runtime_time_groups(dt_root);
    }
    else if (strcmp(name, "Angle") == 0)
    {
        /* Angle: 当前无倾角传感器硬件，返回 x/y/z=0 表示设备支持此属性但无有效数据 */
        zk_add_angle_group(dt_root);
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

static void zk_factory_buf_set_u16be(u8 *factory_buf, u16 offset, u16 value)
{
    factory_buf[offset] = (u8)(value >> 8);
    factory_buf[offset + 1] = (u8)(value & 0xFFu);
}

static int zk_factory_current_validation_error(
    sys_product_current_validation_en result)
{
    switch (result)
    {
        case SYS_PRODUCT_CURRENT_VALID:
            return 0;
        case SYS_PRODUCT_CURRENT_ZERO:
            return ZK_FACTORY_CURRENT_ZERO_ERROR;
        case SYS_PRODUCT_CURRENT_HW_MAX:
            return ZK_FACTORY_CURRENT_HWMAX_ERROR;
        case SYS_PRODUCT_CURRENT_CALIBRATION_ACTIVE:
            return ZK_FACTORY_CALIBRATION_ACTIVE_ERROR;
        default:
            return ZK_FACTORY_PROFILE_MISMATCH_ERROR;
    }
}

static int zk_apply_factory_config(cJSON *factory, u8 *factory_buf, int *changed)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    sys_product_current_validation_en current_result;
    int value;
    int err;

    if (factory == NULL || factory_buf == NULL || changed == NULL ||
        sys_product_profile_is_complete(profile) != BOOL_TRUE)
    {
        return 4;
    }
    if (!cJSON_IsObject(factory))
    {
        return 4;
    }

    *changed = 0;

    if (zk_json_pick_config_number(factory, "MID", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value != (int)profile->mid)
        {
            return ZK_FACTORY_PROFILE_PROTECTED_ERROR;
        }
    }
    if (zk_json_pick_config_number(factory, "BOUND_OUTPUT_VOLTAGE_01V",
                                   &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value < 0 || value > 65535)
        {
            return 3;
        }
        zk_factory_buf_set_u16be(factory_buf, 0x08, (u16)value);
        *changed = 1;
    }
    if (zk_json_pick_config_number(factory, "SET_OUTCUR", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value <= 0 || value > FACTORY_OUTCUR_MAX_MA)
        {
            return ZK_FACTORY_CURRENT_ZERO_ERROR;
        }
        zk_factory_buf_set_u16be(factory_buf, 0x10, (u16)value);
        *changed = 1;
    }
    if (zk_json_pick_config_number(factory, "HWMAX_OUTCUR", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value <= 0 || value > (int)profile->hw_max_current_ma)
        {
            return ZK_FACTORY_CURRENT_HWMAX_ERROR;
        }
        zk_factory_buf_set_u16be(factory_buf, 0x12, (u16)value);
        *changed = 1;
    }
    if (zk_json_pick_config_number(factory, "OUTPUT_CUR_SENSOR", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value != (int)profile->rs3_mohm)
        {
            return ZK_FACTORY_PROFILE_PROTECTED_ERROR;
        }
    }
    if (zk_json_pick_config_number(factory, "OP_PWM_OFFSET", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value < 0 || value > 1000)
        {
            return 3;
        }
        zk_factory_buf_set_u16be(factory_buf, 0x16, (u16)value);
        *changed = 1;
    }
    if (zk_json_pick_config_number(factory, "INNRE_TEMP_PRO_EN", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value < 0 || value > 1)
        {
            return 3;
        }
        factory_buf[0x18] = (u8)value;
        *changed = 1;
    }
    if (zk_json_pick_config_number(factory, "INNRE_TEMP_PRO", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value < 0 || value > 127)
        {
            return 3;
        }
        factory_buf[0x19] = (u8)value;
        *changed = 1;
    }
    if (zk_json_pick_config_number(factory, "CX", &value, &err) == BOOL_TRUE)
    {
        if (err != 0)
        {
            return err;
        }
        if (value <= 0 || value >= 0xFF)
        {
            return 3;
        }
        factory_buf[0x1E] = (u8)value;
        *changed = 1;
    }

    if (*changed == 0)
    {
        return 1;
    }

    current_result = factory_user_validate_candidate(factory_buf);
    if (current_result != SYS_PRODUCT_CURRENT_VALID)
    {
        return zk_factory_current_validation_error(current_result);
    }
    return 0;
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

    /* 属性读取返回带 ER=0 的完整响应，DT 内只放平台请求的属性组。 */
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
    cJSON *factory;
    zk_device_config_t candidate;
    RtcTime_t rtc_value;
    u8 factory_buf[128];
    int err;
    int handled;
    int persist_needed;
    int reset_period_timers;
    int update_rtc;
    int factory_changed;

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
    factory = cJSON_GetObjectItem(dt, "Factory");

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
    if (factory != NULL)
    {
        handled = 1;
    }
    if (handled == 0)
    {
        zk_publish_simple_response(header, 1);
        return BOOL_TRUE;
    }

    /*
     * 写属性先在 candidate 上完成全部校验，只有 Flash 保存成功后才提交到
     * zk_dev_cfg，避免半包或非法字段影响当前运行参数。
     */
    candidate = zk_dev_cfg;
    persist_needed = (gis != NULL || dim != NULL || sense != NULL || svr != NULL) ? 1 : 0;
    reset_period_timers = (svr != NULL) ? 1 : 0;
    update_rtc = (rtc != NULL) ? 1 : 0;
    memcpy(factory_buf, sys_data.fa_Parambuf, sizeof(factory_buf));
    factory_changed = 0;

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
    if (factory != NULL && (err = zk_apply_factory_config(factory, factory_buf, &factory_changed)) != 0)
    {
        zk_publish_simple_response(header, err);
        return BOOL_TRUE;
    }

    if (persist_needed != 0 || factory_changed != 0)
    {
        if (zk_property_flash_store_transaction(
                &candidate,
                (persist_needed != 0) ? BOOL_TRUE : BOOL_FALSE,
                factory_buf,
                (factory_changed != 0) ? BOOL_TRUE : BOOL_FALSE) == BOOL_FALSE)
        {
            zk_publish_simple_response(header, ZK_FLASH_SAVE_ERROR);
            return BOOL_TRUE;
        }
        if (persist_needed != 0)
        {
            zk_dev_cfg = candidate;
            if (reset_period_timers != 0)
            {
                zk_reset_config_period_timers();
            }
        }
    }
    if (update_rtc != 0)
    {
        zk_set_local_rtc(&rtc_value);
    }
    if (factory_changed != 0)
    {
        memcpy(sys_data.fa_Parambuf, factory_buf, sizeof(factory_buf));
        factory_user_load_data();
        sys_pwm_reload();
    }

    zk_publish_simple_response(header, 0);
    return BOOL_TRUE;
}
