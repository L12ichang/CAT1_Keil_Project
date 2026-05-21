#ifndef ZK_PROPERTY_H_
#define ZK_PROPERTY_H_

#include "mqtt_zk_protocol.h"

const zk_device_config_t *zk_device_config_get(void);
boolean_en zk_device_config_restore_defaults(void);

#endif
