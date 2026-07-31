#include "Protocol.h"
#include "stdio.h"
#include "string.h"
#include "Portable.h"
#include "Utils.h"
#include "NbDriver.h"
#include "FirmwareUpdater.h"
#include "SystemConfig.h"
#include "hw_gateway.h"
#include "hw_uart1.h"
#include "build_date.h"
#include "sys_aip1302.h"
#include "factory_user_data.h"
#include "Json_Protocol.h"
#include "TcpClient.h"
#include "mqtt_zk_protocol.h"
#include "sys_cellular.h"
#include "sys_connectivity.h"
#include "sys_mqtt.h"
#define STATUS_NOT_CONNECTED 0
#define STATUS_TCP_CONNECTED 1
#define STATUS_LOG_IN_SUCCESS 2
#define PING_PERIOD_S 60      //60s
#define PONG_PERIOD_S 3*60    //360s
static  uint32  timer = 0;//1s��ʱ��
static  uint8 tcpConnectState = STATUS_NOT_CONNECTED;//0��δ���ӣ�1��tcp�����ӣ�2���ѵ�¼
static  uint8 module_signal_level = 0;//�ź�ǿ��
static boolean_en network_bridge_configured = BOOL_FALSE;
static boolean_en network_bridge_ready_notified = BOOL_FALSE;
static u16 network_bridge_generation = 0U;

#if APP_LOG_ENABLE
static const char *tcp_connect_state_name(uint8 value)
{
    switch (value)
    {
        case STATUS_NOT_CONNECTED: return "NOT_CONNECTED";
        case STATUS_TCP_CONNECTED: return "TCP_CONNECTED";
        case STATUS_LOG_IN_SUCCESS: return "LOG_IN_SUCCESS";
        default: return "UNKNOWN";
    }
}
#endif

extern u1t _HexToBcd(u1t hex);
extern u8 online;
void SET_NB_STAT_EPOWER_DOWN(void);
/**
*@brief   ��ȡ�ź�ǿ��
*@return  �ź�ǿ�ȣ�0���ź�����1���ź��У�2���ź�ǿ
*/
uint8 getSignal(void)
{
    return module_signal_level;
}

static boolean_en tcp_payload_is_json(uint8 *pData, uint16 length)
{
    uint16 i;

    if (pData == 0 || length == 0)
    {
        return BOOL_FALSE;
    }
    for (i = 0; i < length; ++i)
    {
        if (pData[i] == ' ' || pData[i] == '\r' || pData[i] == '\n' || pData[i] == '\t')
        {
            continue;
        }
        return (pData[i] == '{') ? BOOL_TRUE : BOOL_FALSE;
    }
    return BOOL_FALSE;
}

/**
*@brief   ��λtcp״̬��׼������
*@return  ��
*/
 void resetTcpState(void) 
{
    tcpConnectState = STATUS_NOT_CONNECTED;
    printf("����\n");
    online=0;
    gateway_state=GATEWAY_STATE_POWER_DOWN;//����״̬
    SET_NB_STAT_EPOWER_DOWN();//����URC����״̬
    zk_mqtt_reset_session();
}

/**
*@brief   ����tcp����
*@param	  pData�����ݻ���
*@param	  length�����ݳ���
*@return  ��
*/
void sendTcpData(uint8 *pData, uint16 length)
{
   if (tcpConnectState == STATUS_NOT_CONNECTED)
   {
       printf(" login_ STATUS_NOT_CONNECTED\n");
       return;
   }
#if ZK_PROTOCOL_ONLY
   if (tcp_payload_is_json(pData, length) == BOOL_FALSE)
   {
       printf("drop legacy tcp payload in ZK mode\n");
       return;
   }
#endif
   if (nbSendTcpData(pData, length) != NB_ERROR_NONE)
   {
       printf("QMTPUBEX busy or invalid\n");
   }
}

void send_MQTT_Data(char *topic,char *pData)
{
#if ZK_PROTOCOL_ONLY
   if (topic == 0 || strncmp(topic, "MS/", 3) != 0)
   {
       printf("drop legacy mqtt topic in ZK mode\n");
       return;
   }
#endif
   if (g4Send_MQTT_Data(topic,pData) != NB_ERROR_NONE)
   {
       printf("QMTPUBEX topic send busy or invalid\n");
   }

}

void onLogInResponse(uint8 result) 
{
    if (result == LOGIN_SUCCESS) 
    {
#ifdef DEBUG_PRINT
        printf("log in success\n");
#endif
        tcpConnectState = STATUS_LOG_IN_SUCCESS;
        online = 1;
        timer = Timer_GetTickCount();
        longin_sucess();//hw_gateway�ο�Ӧ��ɹ���־
        nb_mark_business_online();
        nb_trace_milestone("BUSINESS_LOGIN_OK");
        printf("[MQTT] business login ok tcpConnectState=%s online=%u\n",
               tcp_connect_state_name(tcpConnectState),
               online);
    }
}

/**
*@brief   Pong��Ϣ�ص�
*@return  ��
*/
void onPongMsg(void) 
{
    printf( "pingok");
}

extern uint8 IMEI[18];
extern u8 IMEI10[13];
extern uint8 simCardICCID[22];
extern u8 online;
extern u1t GetWeek(u2t year, u1t mon, u1t day);
 #include "ntc.h"   
/**
*@brief   NB�¼��ص����������ӳɹ����Ͽ����ӵ��¼�ʱ�����ô˺���
*@param	  subEvent������NB�¼�
*@param	  pData��NB�¼�Я�������ݣ���������·�������
*@param	  length�����ݳ���
*@return  ��
*/
void onNBEvent(uint8 subEvent, uint8 *pData, uint16 length) 
{
    (void)length;
    switch (subEvent) 
    {
        case NB_EVENT_CONNECTED:
            // MQTT connected, publish ZK JSON login packet.
            tcpConnectState = STATUS_TCP_CONNECTED;
            printf("[MQTT] transport connected tcpConnectState=%s online=%u\n",
                   tcp_connect_state_name(tcpConnectState),
                   online);
            if (zk_publish_login_packet() != 0)
            {
                printf("ZK login blocked: MQTT sender busy or invalid config\n");
            }
            break;

        case NB_EVENT_LOST_CONNECTION:
             tcpConnectState = STATUS_NOT_CONNECTED;
             online=0;
             zk_mqtt_reset_session();
             printf("[MQTT] lost connection tcpConnectState=%s online=%u\n",
                    tcp_connect_state_name(tcpConnectState),
                    online);
             break;

        case NB_EVENT_DATA:
        //������ʼ--------------------------------------------------------------------------------------------------------------------------------------------------------
             app_mqtt_rx((char*)pData);


                
   //-----------------------------------------------------------------------------------------------------------��������---------------------------------------------------------------------------------------------------
                break;

          default:
          break;
    }
}

/*
 * 阶段3临时 App 桥：只翻译传输配置、READY、消息和发布结果。
 * 业务登录/会话仍保留在现有 App 层，阶段4再迁移。
 */
static void tcpClientNetworkBridgeProcess(void)
{
    sys_cellular_snapshot_st cellular;
    sys_connectivity_snapshot_st connectivity;
    sys_mqtt_message_st message;
    sys_mqtt_publish_result_st publish_result;
    sys_mqtt_config_st transport_config;
    const zk_mqtt_config_t *legacy_config;
    const zk_device_config_t *device_config;
    int will_length;

    sys_cellular_get_snapshot(&cellular);
    if ((network_bridge_configured != BOOL_TRUE) &&
        (cellular.imei_ready == BOOL_TRUE) &&
        (cellular.iccid_ready == BOOL_TRUE))
    {
        memset(IMEI, 0, sizeof(IMEI));
        memcpy(IMEI, cellular.imei, 15U);
        memset(simCardICCID, 0, sizeof(simCardICCID));
        memcpy(simCardICCID, cellular.iccid, 20U);
        if (zk_mqtt_init() == BOOL_TRUE)
        {
            legacy_config = zk_mqtt_get_config();
            if (legacy_config != NULL)
            {
                memset(&transport_config, 0, sizeof(transport_config));
                device_config = zk_device_config_get();
                if ((device_config != NULL) &&
                    (device_config->svrIp[0] != '\0') &&
                    (device_config->svrPort > 0) &&
                    (device_config->svrPort <= 65535))
                {
                    strcpy(
                        transport_config.server_host,
                        device_config->svrIp);
                    transport_config.server_port =
                        (u16)device_config->svrPort;
                }
                else
                {
                    strcpy(
                        transport_config.server_host,
                        NETWORK_MQTT_SERVER_HOST);
                    transport_config.server_port =
                        NETWORK_MQTT_SERVER_PORT;
                }
                strcpy(transport_config.client_id, legacy_config->client_id);
                strcpy(transport_config.username, legacy_config->username);
                strcpy(transport_config.password, legacy_config->password);
                strcpy(
                    transport_config.downlink_topic,
                    legacy_config->sub_topic);
                strcpy(
                    transport_config.upgrade_topic,
                    legacy_config->sub_upgrade_topic);
                strcpy(
                    transport_config.will_topic,
                    legacy_config->will_topic);
                will_length = zk_make_offline_packet(
                    (char *)transport_config.will_payload,
                    sizeof(transport_config.will_payload));
                if ((will_length > 0) &&
                    (will_length <=
                     (int)sizeof(transport_config.will_payload)))
                {
                    transport_config.will_payload_length =
                        (u16)will_length;
                }
                if ((transport_config.will_payload_length > 0U) &&
                    (sys_mqtt_configure(&transport_config) == BOOL_TRUE) &&
                    (sys_connectivity_set_probe_topic(
                         legacy_config->pub_topic) == BOOL_TRUE))
                {
                    network_bridge_configured = BOOL_TRUE;
                }
            }
        }
    }

    sys_connectivity_get_snapshot(&connectivity);
    if (connectivity.session_generation != network_bridge_generation)
    {
        if (network_bridge_ready_notified == BOOL_TRUE)
        {
            onNBEvent(NB_EVENT_LOST_CONNECTION, NULL, 0U);
        }
        network_bridge_generation = connectivity.session_generation;
        network_bridge_ready_notified = BOOL_FALSE;
    }
    if ((connectivity.transport_ready == BOOL_TRUE) &&
        (network_bridge_ready_notified != BOOL_TRUE))
    {
        network_bridge_ready_notified = BOOL_TRUE;
        onNBEvent(NB_EVENT_CONNECTED, NULL, 0U);
    }
    else if ((connectivity.transport_ready != BOOL_TRUE) &&
             (network_bridge_ready_notified == BOOL_TRUE))
    {
        network_bridge_ready_notified = BOOL_FALSE;
        onNBEvent(NB_EVENT_LOST_CONNECTION, NULL, 0U);
    }

    while (sys_mqtt_get_message(&message) == BOOL_TRUE)
    {
        onNBEvent(
            NB_EVENT_DATA,
            (u8 *)message.payload,
            message.payload_length);
    }
    while (sys_connectivity_get_publish_result(&publish_result) ==
           BOOL_TRUE)
    {
        nb_mqtt_stage3_process_result(
            publish_result.source_id,
            publish_result.request_id,
            publish_result.packet_id,
            publish_result.session_generation,
            (u8)publish_result.result);
    }
}


/**
*@brief   ����tcp�ͻ�������
*@return  ��
*/
void tcpClientProcess(void)
{
    tcpClientNetworkBridgeProcess();
    if (tcpConnectState == STATUS_NOT_CONNECTED) 
    {
        return;
    }

    if (!Timer_PassedDelay(timer, 1000))
    {
        return;
    }
    timer = Timer_GetTickCount();
    
    zk_mqtt_session_process();

    if (tcpConnectState == STATUS_TCP_CONNECTED)
    {
        return;
    }

    if (tcpConnectState == STATUS_LOG_IN_SUCCESS)
    {
        return;
    }
}

/**
*@brief   ��ʼ��tcp�ͻ���
*@param	  id���豸id
*@param	  model���豸�ͺ�
*@param	  firmware���̼��汾��
*@return  ��
*/
void tcpClientInit(uint32 id, uint32 model, uint32 firmware) 
{
    DeviceId = id;
    (void)model;
    (void)firmware;
}

void mac_reset(void)
{
 tcpClientInit(DEVICE_ID, DEVICE_MODEL, FIRMWARE_VERSION);
 resetTcpState();
 network_bridge_configured=BOOL_FALSE;
 network_bridge_ready_notified=BOOL_FALSE;
 network_bridge_generation=0U;
}
