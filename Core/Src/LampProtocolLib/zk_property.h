#ifndef ZK_PROPERTY_H_
#define ZK_PROPERTY_H_

#include "mqtt_zk_protocol.h"

const zk_device_config_t *zk_device_config_get(void);
boolean_en zk_device_config_restore_defaults(void);

/* 将完整的配置结构体原子写入Flash并提交到RAM，用于批量配置场景 */
boolean_en zk_device_config_commit(const zk_device_config_t *config);

#endif
