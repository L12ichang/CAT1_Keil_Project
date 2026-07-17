#ifndef ZK_CALIBRATION_PROPERTY_H
#define ZK_CALIBRATION_PROPERTY_H

#include "mqtt_zk_protocol.h"

boolean_en zk_handle_calibration_property(cJSON *root, const zk_message_header_t *header);

#endif
