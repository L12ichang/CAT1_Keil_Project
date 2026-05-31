#ifndef ZK_PROTOCOL_INTERNAL_H_
#define ZK_PROTOCOL_INTERNAL_H_

#include "mqtt_zk_protocol.h"
#include "sys_aip1302.h"

cJSON *zk_cjson_create_tx_object(const char *context);
cJSON *zk_cjson_create_tx_array(const char *context);
cJSON *zk_create_root_from_header(const zk_message_header_t *header, int with_er, int er_code);
int zk_send_json_root(cJSON *root, const char *topic);
int zk_publish_simple_response(const zk_message_header_t *request, int err_code);
void zk_schedule_simple_response(const zk_message_header_t *request, int err_code);

void zk_get_time_text(char *buf, int buf_size);
boolean_en zk_parse_rtc_text(const char *text, RtcTime_t *rtc);
const char *zk_json_get_rtc_time_text(cJSON *node);
void zk_set_local_rtc(const RtcTime_t *rtc);
void zk_reset_config_period_timers(void);

void zk_add_run_status_group(cJSON *dt_root);
void zk_add_ele_info_group(cJSON *dt_root);
void zk_add_per_sts_group(cJSON *dt_root);
void zk_add_signal_group(cJSON *dt_root);
void zk_add_runtime_time_groups(cJSON *dt_root);
void zk_add_angle_group(cJSON *dt_root);

#endif
