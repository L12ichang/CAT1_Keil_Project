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
#define STATUS_NOT_CONNECTED 0
#define STATUS_TCP_CONNECTED 1
#define STATUS_LOG_IN_SUCCESS 2
#define PING_PERIOD_S 60      //60s
#define PONG_PERIOD_S 3*60    //360s
static  uint32  timer = 0;//1s��ʱ��
static  uint8 tcpConnectState = STATUS_NOT_CONNECTED;//0��δ���ӣ�1��tcp�����ӣ�2���ѵ�¼
static  uint8 module_signal_level = 0;//�ź�ǿ��

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


/**
*@brief   ����tcp�ͻ�������
*@return  ��
*/
void tcpClientProcess(void)
{
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
 _4G_configModule_machine_star();//���ϵ�  ����ģ��
 gateway_state=GATEWAY_STATE_POWER_DOWN;//����״̬
}
