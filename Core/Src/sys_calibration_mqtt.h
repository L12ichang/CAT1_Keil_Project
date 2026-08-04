#ifndef SYS_CALIBRATION_MQTT_H
#define SYS_CALIBRATION_MQTT_H

#include "mqtt_zk_protocol.h"

/* 校准业务只接受SV=cal，禁止落入普通prop/ctrl/plan/OTA路由。 */
extern boolean_en sys_calibration_mqtt_handle(
    cJSON *root,
    const zk_message_header_t *header);

#endif
