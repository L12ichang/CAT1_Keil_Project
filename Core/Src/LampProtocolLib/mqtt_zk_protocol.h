#ifndef MQTT_ZK_PROTOCOL_H_
#define MQTT_ZK_PROTOCOL_H_

#include "common.h"
#include "cJSON.h"

#define ZK_MQTT_SERVER_IP       "47.120.15.220"
#define ZK_MQTT_SERVER_PORT     1883
#define ZK_MQTT_CLIENT_IDX      0
#define ZK_MQTT_VERSION         4
#define ZK_MQTT_SUB_QOS         1
#define ZK_MQTT_PUB_QOS         1
#define ZK_MQTT_RETAIN          0

#define ZK_TOPIC_PREFIX         "MS"
#define ZK_SV_REPT              "rept"
#define ZK_SV_PROP              "prop"
#define ZK_SV_CTRL              "ctrl"
#define ZK_SV_RQST              "rqst"
#define ZK_SV_OTA               "ota"
#define ZK_SV_PLAN              "plan"
#define ZK_SV_ALAM             "alam"
#define ZK_CT_LOGIN             "L"
#define ZK_CT_HEARTBEAT         "H"
#define ZK_CT_WRITE             "W"
#define ZK_CT_READ              "R"
#define ZK_CT_CYCLIC            "C"
#define ZK_CT_CHANGE            "B"
#define ZK_CT_ALARM             "A"
#define ZK_CT_PROGRESS          "P"
#define ZK_CT_ERROR             "E"
#define ZK_ALARM_OVER_VOLTAGE       10000
#define ZK_ALARM_UNDER_VOLTAGE      10001
#define ZK_ALARM_OVER_CURRENT       10002
#define ZK_ALARM_UNDER_CURRENT      10003
#define ZK_ALARM_LIGHT_ON_FAIL      10004
#define ZK_ALARM_LIGHT_OFF_FAIL     10005
#define ZK_ALARM_POLE_TILT          10006
#define ZK_ALARM_LEAK_CURRENT       10007
#define ZK_ALARM_DEVICE_FAULT       10008
#define ZK_ALARM_POWER_DOWN         10009
#define ZK_ALARM_OUT_OVER_VOLTAGE   10010
#define ZK_ALARM_OUT_UNDER_VOLTAGE  10011
#define ZK_ALARM_OUT_OVER_CURRENT   10012
#define ZK_ALARM_OUT_UNDER_CURRENT  10013
#define ZK_ALARM_OVER_POWER         10014
#define ZK_ALARM_TC_OVER_TEMP       10015
#define ZK_ALARM_CTRL_OVER_TEMP     10016
#define ZK_ALARM_COUNT              17
#define ZK_LOGIN_REQUEST_ID     "000001"
#define ZK_JSON_ID_FIRST_REPORT 2UL
#define ZK_JSON_ID_MAX          999999UL
#define ZK_LOGIN_ACK_TIMEOUT_MS (30UL * 1000UL)
#define ZK_HEARTBEAT_INTERVAL_SEC 60
#define ZK_UPLOAD_INTERVAL_SEC  300
#define ZK_TIME_REQUEST_INTERVAL_SEC 3600
#define ZK_JSON_BUF_SIZE        2048
#define ZK_JSON_RX_MAX          2048
#define ZK_CJSON_POOL_SIZE      8192
#define ZK_CJSON_RX_POOL_SIZE   4096
#define ZK_CJSON_TX_POOL_SIZE   4096
#ifndef ZK_PROTOCOL_ONLY
#define ZK_PROTOCOL_ONLY        1
#endif
#define ZK_FLASH_SAVE_ERROR     99

typedef enum
{
    ZK_LOGIN_STATE_IDLE = 0,
    ZK_LOGIN_STATE_WAIT_ACK = 1,
    ZK_LOGIN_STATE_ONLINE = 2,
    ZK_LOGIN_STATE_WAIT_PUBLISH = 3,
} zk_login_state_en;

typedef struct
{
    char imei[16];
    char client_id[16];
    char username[16];
    char password[13];
    char sub_topic[40];
    char pub_topic[40];
    char will_topic[40];
    char sub_upgrade_topic[40];
    char pub_upgrade_topic[40];
} zk_mqtt_config_t;

typedef struct
{
    int protId;
    char clas[16];
    char prottp[24];
    int hver;
    int sver;
    char mver[16];
    char iccid[22];
    long lng;
    long lat;
    int zone;
    int cns;
    int dimTp;
    int polar;
    int dlmt;
    int ulmt;
    int rti;
    int rtPwr;
    int di;
    int sBri;
    int sBriTm;
    char svrIp[32];
    int svrPort;
    int uPeriod;
    int hPeriod;
    int tPeriod;
    int commMain;
    int commSub;
    int commSAuto;
    int spreadOffset;
    int spreadWindow;
    int spreadInterval;
    /* 告警阈值配置（按 almId-10000 索引，共17组：10000~10016） */
    int almValue[17];      /* 告警触发阈值 */
    int almRecValue[17];   /* 告警恢复阈值 */
    int almEn[17];         /* 告警使能标志：1=启用，0=禁用 */
} zk_device_config_t;

typedef struct
{
    char sn[16];
    char tm[20];
    char sv[8];
    char id[8];
    char ct[4];
} zk_message_header_t;

typedef zk_message_header_t zk_login_response_t;
typedef int (*zk_response_dt_builder_t)(cJSON *dt, void *ctx);

boolean_en zk_mqtt_init(void);
void zk_cjson_prepare_parse(void);
void zk_mqtt_reset_session(void);
void zk_mqtt_generate_password(const char *imei, char *password);
void zk_device_config_init(void);
void zk_device_config_refresh_iccid(void);
const zk_device_config_t *zk_device_config_get(void);
boolean_en zk_device_config_restore_defaults(void);
const zk_mqtt_config_t *zk_mqtt_get_config(void);
const char *zk_mqtt_get_pub_topic(void);
const char *zk_mqtt_get_sub_topic(void);
const char *zk_mqtt_get_upgrade_sub_topic(void);
uint32 zk_mqtt_next_json_id(void);
uint16 zk_mqtt_next_packet_id(void);

int zk_build_qmt_open_cmd(char *buf, int buf_size);
int zk_build_qmt_conn_cmd(char *buf, int buf_size);
int zk_build_qmt_sub_cmd(char *buf, int buf_size);
int zk_build_qmt_sub_upgrade_cmd(char *buf, int buf_size);

void zk_fill_message_header(zk_message_header_t *header,
                            const char *sv,
                            const char *id,
                            const char *ct);
int zk_parse_message_header(const char *json_str, zk_message_header_t *header);
int zk_parse_message_header_from_root(cJSON *root, zk_message_header_t *header);
boolean_en zk_message_header_matches_device(const zk_message_header_t *header);
int zk_make_login_packet(char *buf, int buf_size);
int zk_make_heartbeat_packet(char *buf, int buf_size);
int zk_publish_login_packet(void);
int zk_publish_heartbeat_packet(void);
int zk_publish_error_response(const zk_message_header_t *request, int err_code);
int zk_publish_response_with_dt(const zk_message_header_t *request,
                                int err_code,
                                zk_response_dt_builder_t builder,
                                void *ctx);
int zk_publish_alarm_report(uint16 alarm_id, u8 status, uint32 value, uint32 threshold);
void zk_mqtt_session_process(void);
void zk_runtime_stats_init(void);
void zk_runtime_counter_process(void);
boolean_en zk_mqtt_accept_login_ack(const zk_message_header_t *header);
boolean_en zk_mqtt_accept_heartbeat_ack(const zk_message_header_t *header);
int zk_parse_login_response(const char *json_str, zk_login_response_t *response);
void zk_apply_server_time_from_header(const zk_message_header_t *header);
boolean_en zk_dispatch_message(cJSON *root, const zk_message_header_t *header);
boolean_en zk_handle_property_read(cJSON *root, const zk_message_header_t *header);
boolean_en zk_handle_property_write(cJSON *root, const zk_message_header_t *header);
boolean_en zk_handle_control_message(cJSON *root, const zk_message_header_t *header);
boolean_en zk_handle_request_message(cJSON *root, const zk_message_header_t *header);
boolean_en zk_handle_ota_message(cJSON *root, const zk_message_header_t *header);
boolean_en zk_handle_plan_message(cJSON *root, const zk_message_header_t *header);
boolean_en zk_handle_alam_message(cJSON *root, const zk_message_header_t *header);
boolean_en zk_ota_is_busy(void);
void zk_notify_state_changed(void);
void zk_apply_plan_brightness(int brightness);
int zk_publish_ota_progress(uint32 progress);
int zk_publish_ota_error(int err_code);
void zk_ota_report_mark_pending(const char *ota_id, const char *url);
void zk_ota_report_mark_verified(u32 checksum, u32 size, u16 device_type);
const char *zk_get_ota_url(void);

#endif
