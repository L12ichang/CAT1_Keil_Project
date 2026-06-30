#include "json_protocol.h"

#include <stdio.h>
#include <string.h>

#include "Protocol.h"
#include "TcpClient.h"
#include "cJSON.h"
#include "mqtt_zk_protocol.h"

void app_mqtt_rx(char *rx)
{
    cJSON *root = NULL;
    zk_message_header_t header_storage;
    zk_message_header_t *header = &header_storage;
    size_t rx_len;

    if (rx == NULL || *rx == '\0')
    {
        return;
    }

    rx_len = strlen(rx);
    if (rx_len > ZK_JSON_RX_MAX)
    {
        printf("[MQTT] ZK JSON too long\r\n");
        return;
    }

    zk_cjson_prepare_parse();
    root = cJSON_Parse(rx);
    if (root == NULL)
    {
        printf("[MQTT] Parse Failed\r\n");
        return;
    }

    if (zk_parse_message_header_from_root(root, header) == 0)
    {
        if (zk_message_header_matches_device(header) == BOOL_FALSE)
        {
            goto EXIT_CLEANUP;
        }
        if (zk_mqtt_accept_login_ack(header))
        {
            onLogInResponse(LOGIN_SUCCESS);
            goto EXIT_CLEANUP;
        }
        if (zk_mqtt_accept_heartbeat_ack(header))
        {
            goto EXIT_CLEANUP;
        }
        if (zk_dispatch_message(root, header))
        {
            goto EXIT_CLEANUP;
        }
    }

EXIT_CLEANUP:
    cJSON_Delete(root);
}

void json_process(void)
{
}
