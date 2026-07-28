#include "NbDriver.h"
#include "string.h"
#include "stdio.h"
#include "Portable.h"
#include "Utils.h"
#include "Queue.h"
#include "TcpClient.h"
#include "Protocol.h"
#include "App.h"
#include "Portable.h"
#include "watchdog.h"
#include "hw_gateway.h"
#include "hw_4g_io.h"
#include "common.h"
#include "ota.h"
#include "type.h"
#include "mqtt_zk_protocol.h"
extern QUEUE  usartRecvQueue;//�������ݽ��ն���
#define RECV_BUF_LENGTH 600
#define CONNECTING_MAX_WAIT_TIME 20
#define CONNECTED_MAX_WAIT_TIME 5
#define MAX_RECONNECT_COUNT 6*5 //6*20*5s=10min
#define NB_MODEM_OTA_TX_BUFFER_SIZE 320U
#define NB_AT_TICK_MS 20UL
#define NB_QMTOPEN_TIMEOUT_MS 120000UL
#define NB_QMTCONN_TIMEOUT_MS 30000UL
#define NB_QMTSUB_TIMEOUT_MS 15000UL
#define NB_QMTCLOSE_TIMEOUT_MS 30000UL
#define NB_QMTPUB_PROMPT_TIMEOUT_MS 5000UL
#define NB_QMTPUB_ACK_TIMEOUT_MS 15000UL
#define NB_AT_TIMEOUT_COUNT(ms) (((ms) + NB_AT_TICK_MS - 1UL) / NB_AT_TICK_MS)
#define NB_AT_PROBE_TIMEOUT_MS 300UL
#define NB_AT_READY_QUERY_INTERVAL_MS 500UL
#define NB_AT_READY_WAIT_MS 12000UL
#define NB_CEREG_QUERY_INTERVAL_MS 1000UL
#define NB_CEREG_RECOVERY_TIMEOUT_MS 30000UL
#define NB_CFUN_TIMEOUT_MS 15000UL
#define NB_MQTT_FAST_RETRY_COUNT 2U
#define NB_MQTT_RETRY_1_MS 2000UL
#define NB_MQTT_RETRY_2_MS 5000UL
#define NB_WILL_PAYLOAD_SIZE 192U
#define NB_DEBUG_PRINT
uint8 stringBuf[RECV_BUF_LENGTH];//���ݽ��ջ���
static  uint16 recvLength = 0;//���ݽ��ճ���
//static  uint32 connectingTimer = 0;//���Ӷ�ʱ��
//static  uint32 idleTimer = 0;//���ж�ʱ��
static  NB_STATE state = NB_STATE_POWER_DOWN;//nbģ��״̬
//static  uint8 reconnectCount = 0;//��������
//static  uint8 *serverAddress;//���ӵķ�������ַ
//static  uint8 serverAddressLength = 0;//��������ַ����
//static  uint16 serverPort = 0;//�������˿ں�
static  uint32 pub_timer=0;//����delay
 u8   OTA_ENABLE=0;
static u8 nb_modem_ota_lock=0;
static u8 nb_modem_ota_tx_buffer[NB_MODEM_OTA_TX_BUFFER_SIZE];
 u8 IMEI[18]={0};
 u8 IMEI10[13]={0};
uint8 simCardICCID[22]={0};
char * IMEI_CHAR;
u32 IMEI_DEC;
CONNECT_CONFIG_state_en connect_state=CONNECT_CONFIG_STATE_IDLE;
static boolean_en imei_ready = BOOL_FALSE;
static boolean_en iccid_ready = BOOL_FALSE;
static boolean_en rsrp_ready = BOOL_FALSE;
static s32 nb_rsrp_dbm10 = 0;
static s8 nb_cereg_stat = -1;
static int nb_cereg_cause_type = -1;
static int nb_cereg_reject_cause = -1;
static boolean_en nb_cereg_ready = BOOL_FALSE;
static uint32 nb_boot_tick = 0;
static uint32 nb_registration_tick = 0;
static uint32 nb_at_ready_tick = 0;
static uint32 nb_recovery_wait_tick = 0;
static uint32 nb_recovery_wait_ms = 0;
static u8 nb_cfun_recovery_used = 0;
static u8 nb_mqtt_fast_retry_count = 0;
static boolean_en nb_offline_event_sent = BOOL_FALSE;
static char nb_will_command[128];
static char nb_will_payload[NB_WILL_PAYLOAD_SIZE];
static u16 nb_will_payload_length = 0;
static uint32 nb_will_tick = 0;

typedef enum
{
    NB_RECOVERY_NONE = 0,
    NB_RECOVERY_MQTT_FAST,
    NB_RECOVERY_NETWORK,
    NB_RECOVERY_HARD_RESET,
} NB_RECOVERY_LEVEL_EN;

typedef enum
{
    NB_START_REASON_UNKNOWN = 0,
    NB_START_REASON_AT_REUSE,
    NB_START_REASON_COLD_PWRKEY,
    NB_START_REASON_HARD_RECOVERY,
} NB_START_REASON_EN;

typedef enum
{
    NB_DISCONNECT_NONE = 0,
    NB_DISCONNECT_QMTSTAT,
    NB_DISCONNECT_PDP_DEACT,
    NB_DISCONNECT_PUBLISH,
    NB_DISCONNECT_LOGIN,
    NB_DISCONNECT_HEARTBEAT,
    NB_DISCONNECT_MQTT_SETUP,
    NB_DISCONNECT_OTA_PLANNED,
} NB_DISCONNECT_REASON_EN;

static NB_RECOVERY_LEVEL_EN nb_recovery_level = NB_RECOVERY_NONE;
static NB_START_REASON_EN nb_start_reason = NB_START_REASON_UNKNOWN;
static NB_DISCONNECT_REASON_EN nb_disconnect_reason = NB_DISCONNECT_NONE;

static void parseResult(uint8 *buf);
static boolean_en nb_is_link_lost_line(uint8 *buf);
static boolean_en nb_mqtt_publish_read_prompt(void);

typedef enum
{
    NB_ICCID_FAIL_NONE = 0,
    NB_ICCID_FAIL_TIMEOUT,
    NB_ICCID_FAIL_ERROR,
    NB_ICCID_FAIL_BAD_LENGTH,
} NB_ICCID_FAIL_REASON_EN;

static NB_ICCID_FAIL_REASON_EN iccid_fail_reason = NB_ICCID_FAIL_NONE;
#if APP_LOG_ENABLE
static u8 iccid_last_digit_count = 0;

static const char *nb_state_name(NB_STATE value)
{
    switch (value)
    {
        case NB_STATE_POWER_DOWN: return "POWER_DOWN";
        case NB_STATE_NOT_CONNECT: return "NOT_CONNECT";
        case NB_STATE_CONNECTING: return "CONNECTING";
        case NB_STATE_CONNECTED: return "CONNECTED";
        case NB_STATE_IDLE: return "IDLE";
        default: return "UNKNOWN";
    }
}

static const char *connect_state_name(CONNECT_CONFIG_state_en value)
{
    switch (value)
    {
        case CONNECT_CONFIG_STATE_IDLE: return "STATE_IDLE";
        case CONNECT_CONFIG_PROBE_AT: return "PROBE_AT";
        case CONNECT_CONFIG_PWRKEY_START: return "PWRKEY_START";
        case CONNECT_CONFIG_WAIT_AT: return "WAIT_AT";
        case CONNECT_CONFIG_HARD_RESET_START: return "HARD_RESET_START";
        case CONNECT_CONFIG_WAIT_HARD_RESET_AT: return "WAIT_HARD_RESET_AT";
        case CONNECT_CONFIG_AT_CFUN0: return "AT_CFUN0";
        case CONNECT_CONFIG_AT_CFUN1: return "AT_CFUN1";
        case CONNECT_CONFIG_AT_CPIN: return "AT_CPIN";
        case CONNECT_CONFIG_AT_CEREG_ENABLE: return "AT_CEREG_ENABLE";
        case CONNECT_CONFIG_AT_CEREG_QUERY: return "AT_CEREG_QUERY";
        case CONNECT_CONFIG_WAIT_CEREG_QUERY: return "WAIT_CEREG_QUERY";
        case CONNECT_CONFIG_AT_RECVMODE: return "AT_RECVMODE";
        case CONNECT_CONFIG_AT_VERSION: return "AT_VERSION";
        case CONNECT_CONFIG_AT_keepalive: return "AT_KEEPALIVE";
        case CONNECT_CONFIG_AT_SESSION: return "AT_SESSION";
        case CONNECT_CONFIG_AT_TIMEOUT: return "AT_TIMEOUT";
        case CONNECT_CONFIG_AT_IEMI: return "AT_IEMI";
        case CONNECT_CONFIG_AT_QCCID: return "AT_QCCID";
        case CONNECT_CONFIG_AT_WILL_PROMPT: return "AT_WILL_PROMPT";
        case CONNECT_CONFIG_AT_WILL_RESULT: return "AT_WILL_RESULT";
        case CONNECT_CONFIG_WAITING_QMTCLOSE: return "WAITING_QMTCLOSE";
        case CONNECT_CONFIG_RECOVERY_WAIT: return "RECOVERY_WAIT";
        case CONNECT_CONFIG_AT_IPPORT: return "AT_IPPORT";
        case CONNECT_CONFIG_AT_QMTCONN: return "AT_QMTCONN";
        case CONNECT_CONFIG_AT_QMTSUB: return "AT_QMTSUB";
        case CONNECT_CONFIG_AT_LAST: return "AT_LAST";
        case CONNECT_CONFIG__COMPLETE: return "COMPLETE";
        default: return "UNKNOWN";
    }
}
#endif

static void nb_trace_state_change(void)
{
#if APP_LOG_ENABLE
    static boolean_en inited = BOOL_FALSE;
    static NB_STATE last_nb_state = NB_STATE_POWER_DOWN;
    static CONNECT_CONFIG_state_en last_connect_state = CONNECT_CONFIG_STATE_IDLE;

    if (inited == BOOL_FALSE)
    {
        inited = BOOL_TRUE;
        last_nb_state = state;
        last_connect_state = connect_state;
        printf("[NB] boot state=%s connect_state=%s\n",
               nb_state_name(state),
               connect_state_name(connect_state));
        return;
    }

    if (last_connect_state != connect_state)
    {
        printf("[NB] connect_state %s -> %s\n",
               connect_state_name(last_connect_state),
               connect_state_name(connect_state));
        last_connect_state = connect_state;
    }

    if (last_nb_state != state)
    {
        printf("[NB] state %s -> %s\n",
               nb_state_name(last_nb_state),
               nb_state_name(state));
        last_nb_state = state;
    }
#endif
}

void nb_mark_boot_start(void)
{
    nb_boot_tick = Timer_GetTickCount();
    printf("[BOOT] stage=BOOT_START elapsed=0ms\r\n");
}

static const char *nb_start_reason_name(NB_START_REASON_EN reason)
{
    switch (reason)
    {
        case NB_START_REASON_AT_REUSE: return "AT_REUSE";
        case NB_START_REASON_COLD_PWRKEY: return "COLD_PWRKEY";
        case NB_START_REASON_HARD_RECOVERY: return "HARD_RECOVERY";
        case NB_START_REASON_UNKNOWN:
        default: return "UNKNOWN";
    }
}

void nb_trace_milestone(const char *stage)
{
    if (stage == 0)
    {
        return;
    }
    printf("[BOOT] stage=%s elapsed=%lums\r\n",
           stage,
           (unsigned long)(Timer_GetTickCount() - nb_boot_tick));
}

static void nb_trace_at_ready(void)
{
    printf("[BOOT] start_reason=%s\r\n", nb_start_reason_name(nb_start_reason));
    nb_trace_milestone("AT_READY");
}

void nb_mark_business_online(void)
{
    nb_mqtt_fast_retry_count = 0;
    nb_offline_event_sent = BOOL_FALSE;
    nb_recovery_level = NB_RECOVERY_NONE;
    nb_disconnect_reason = NB_DISCONNECT_NONE;
}

 extern   boolean_en  _4g_reset_finish(void) ;

/**
*@brief   �Ӵ������ݶ��ж�ȡһ���ַ���
*@param	  buf���ַ�������
*@param	  len���ַ�������
*@param	  syncMode���Ƿ�������ʽִ�иú���
*@return  ��ȡ�����ַ�������
*/
 uint16 readLine(uint8 *buf, uint16 *len, uint8 syncMode)
{
     uint8 count = 0;
    do {
             while (dequeue(&usartRecvQueue, buf + *len))
            {                
                 if (++(*len) >= RECV_BUF_LENGTH) 
                {
                    *len = 0;
                }
                if ((*len >= 2) && (buf[*len - 2] == '\r') && (buf[*len - 1] == '\n')) 
                {
                    if (*len == 2) 
                    {
                         *len = 0;//���˿���
                         continue;
                    }
                    buf[*len] = 0;
                    return *len;
                }
          }
          if (syncMode) 
           {
               delayMs(20);
           }
       } while (syncMode && (++count < 100));

    return 0;
}
/**
*@brief   �Ӵ������ݶ��ж�ȡһ���ַ���
*@param	  buf���ַ�������
*@param	  len�����յ��ַ���ʵ�ʳ���
*@param	  firmwarelenth���̼��·��ĸ����ĳ���
*@return  ��ȡ�����ַ�������
*/
 uint16 readLine_get_firmware(uint8 *buf, uint16 *len, uint16 *firmwarelenth)
{
   
    do {
            while (dequeue(&usartRecvQueue, buf + *len))
            {
                     if (++(*len) >( *firmwarelenth)+6) //����ģ������ĳ����쳣   �̼���β��\r''\n'��"OK\r\n"
                        {

                            *len = 0;
                        }
                        if ((*len >2) && (buf[*len-2] == '\r') && (buf[*len -1] == '\n')) 
                        {
                            if (*len <= *firmwarelenth) //���˹̼��е� '\r''\n'
                        {
                            continue;
                        }
                        buf[*len] = 0;
                        return *len;
                        }
          }
     
     } while (0);
     return 0;
}
     
/**
*@brief   ִ��AT����
*@param	  command�����͵�AT�����ַ���
*@param	  length��AT�����

*/
static uint8 nb_modem_send_command_raw_result(void *command,uint16 length)
{
    flushQueue(&usartRecvQueue);
   // printf("usartSendData=%s\n",command);
    return usartSendDataWithResult((uint8 *)command, length);
}

static void nb_modem_send_command_raw(void *command,uint16 length)
{
    flushQueue(&usartRecvQueue);
   // printf("usartSendData=%s\n",command);
    usartSendData((uint8 *)command, length);
}

uint8 nb_modem_send_command_ota(void *command,uint16 length)
{
    uint8 uart_ret;

    if (command == NULL || length == 0U)
    {
        return (uint8)HAL_ERROR;
    }
    if (length > (uint16)sizeof(nb_modem_ota_tx_buffer))
    {
        OTA_LOGE("modem ota send too long len=%u cap=%u\r\n",
                 (unsigned int)length,
                 (unsigned int)sizeof(nb_modem_ota_tx_buffer));
        return (uint8)HAL_ERROR;
    }
    memcpy(nb_modem_ota_tx_buffer, command, length);
    uart_ret = nb_modem_send_command_raw_result(nb_modem_ota_tx_buffer, length);
    OTA_LOGD("modem ota uart write ret=%u len=%u\r\n",
             (unsigned int)uart_ret,
             (unsigned int)length);
    return uart_ret;
}

void sendCommand(void *command,uint16 length)
{
    if (nb_modem_ota_lock)
    {
        OTA_LOGW("modem ota lock blocked direct send\r\n");
        return;
    }
    nb_modem_send_command_raw(command, length);
}
    
static SEND_COMMAND_state_en sendcommad_state= SEND_COMMAND_STATE_IDLE;
static u32  wait_timer=0;

static u32  read_counter=0;
static u8   resend_counter=0;
static char * atcommand;
static u8 atlength;
static char *atresponse;
static u32 atwaitCount;
static boolean_en sendcommand_failed = BOOL_FALSE;

static void clear_imei_data(void);
static void clear_iccid_data(void);

static boolean_en nb_at_command_allowed_during_ota(const char *command)
{
    if (command == 0)
    {
        return BOOL_FALSE;
    }
    if (strstr(command, "AT+QMTDISC") != 0 ||
        strstr(command, "AT+QMTCLOSE") != 0 ||
        strstr(command, "AT+QICLOSE") != 0 ||
        strstr(command, "AT+QHTTPCFG") != 0 ||
        strstr(command, "AT+QHTTP") != 0 ||
        strstr(command, "AT+QFLST") != 0 ||
        strstr(command, "AT+QFLDS") != 0 ||
        strstr(command, "AT+QFDEL") != 0 ||
        strstr(command, "AT+QFDWL") != 0 ||
        strstr(command, "AT+QFOPEN") != 0 ||
        strstr(command, "AT+QFSEEK") != 0 ||
        strstr(command, "AT+QFREAD") != 0 ||
        strstr(command, "AT+QFCLOSE") != 0)
    {
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static boolean_en nb_at_command_is_qccid(void)
{
    return (atcommand != 0 && strstr((const char *)atcommand, "AT+QCCID") != 0) ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en nb_at_command_is_qeng(void)
{
    if (atcommand == 0)
    {
        return BOOL_FALSE;
    }
    if (strstr((const char *)atcommand, "QENG") != 0 ||
        strstr((const char *)atcommand, "qeng") != 0)
    {
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static boolean_en nb_at_command_is_cereg(void)
{
    return (atcommand != 0 &&
            strstr((const char *)atcommand, "AT+CEREG") != 0) ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en nb_at_command_is_mqtt_setup(void)
{
    if (atcommand == 0)
    {
        return BOOL_FALSE;
    }
    if (strstr((const char *)atcommand, "AT+QMTOPEN") != 0 ||
        strstr((const char *)atcommand, "AT+QMTCONN") != 0 ||
        strstr((const char *)atcommand, "AT+QMTSUB") != 0 ||
        strstr((const char *)atcommand, "AT+QMTDISC") != 0 ||
        strstr((const char *)atcommand, "AT+QMTCLOSE") != 0)
    {
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static u8 nb_at_command_max_attempts(void)
{
    if (nb_at_command_is_mqtt_setup() == BOOL_TRUE ||
        nb_at_command_is_cereg() == BOOL_TRUE ||
        nb_at_command_is_qeng() == BOOL_TRUE ||
        (atcommand != 0 && strcmp((const char *)atcommand, "AT\r\n") == 0) ||
        (atcommand != 0 && strstr((const char *)atcommand, "AT+CFUN") != 0))
    {
        return 1U;
    }
    return 5U;
}

#if APP_LOG_ENABLE
static const char *nb_at_command_tag(void)
{
    if (atcommand == 0)
    {
        return "NULL";
    }
    if (strstr((const char *)atcommand, "AT+QMTOPEN") != 0)
    {
        return "QMTOPEN";
    }
    if (strstr((const char *)atcommand, "AT+QMTCONN") != 0)
    {
        return "QMTCONN";
    }
    if (strstr((const char *)atcommand, "AT+QMTSUB") != 0)
    {
        return "QMTSUB";
    }
    if (strstr((const char *)atcommand, "AT+QMTDISC") != 0)
    {
        return "QMTDISC";
    }
    if (strstr((const char *)atcommand, "AT+QMTCLOSE") != 0)
    {
        return "QMTCLOSE";
    }
    if (nb_at_command_is_cereg() == BOOL_TRUE)
    {
        return "CEREG";
    }
    if (nb_at_command_is_qeng() == BOOL_TRUE)
    {
        return "QENG";
    }
    if (nb_at_command_is_qccid() == BOOL_TRUE)
    {
        return "QCCID";
    }
    if (strstr((const char *)atcommand, "AT+CGSN") != 0)
    {
        return "CGSN";
    }
    if (strstr((const char *)atcommand, "AT+CFUN") != 0)
    {
        return "CFUN";
    }
    if (strstr((const char *)atcommand, "AT+CPIN") != 0)
    {
        return "CPIN";
    }
    return "AT";
}
#endif

static void send_AT_Command_machine_mark_failed(const char *reason)
{
    sendcommand_failed = BOOL_TRUE;
#if APP_LOG_ENABLE
    printf("[AT][E] %s failed reason=%s\n",
           nb_at_command_tag(),
           (reason != 0) ? reason : "unknown");
#else
    (void)reason;
#endif
}

static boolean_en send_AT_Command_machine_failed(void)
{
    return sendcommand_failed;
}

static boolean_en nb_at_command_terminal_failure(const uint8 *line)
{
    if (line == 0 || atcommand == 0 || nb_at_command_is_qccid() == BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (nb_at_command_is_mqtt_setup() == BOOL_FALSE)
    {
        return BOOL_FALSE;
    }
    if (strstr((const char *)line, "ERROR") != 0 ||
        nb_is_link_lost_line((uint8 *)line) == BOOL_TRUE)
    {
        return BOOL_TRUE;
    }
    if (strstr((const char *)atcommand, "AT+QMTOPEN") != 0 &&
        strstr((const char *)line, "+QMTOPEN:") != 0)
    {
        return BOOL_TRUE;
    }
    if (strstr((const char *)atcommand, "AT+QMTCONN") != 0 &&
        strstr((const char *)line, "+QMTCONN:") != 0)
    {
        return BOOL_TRUE;
    }
    if (strstr((const char *)atcommand, "AT+QMTSUB") != 0 &&
        strstr((const char *)line, "+QMTSUB:") != 0)
    {
        return BOOL_TRUE;
    }
    if (strstr((const char *)atcommand, "AT+QMTDISC") != 0 &&
        strstr((const char *)line, "+QMTDISC:") != 0)
    {
        return BOOL_TRUE;
    }
    if (strstr((const char *)atcommand, "AT+QMTCLOSE") != 0 &&
        strstr((const char *)line, "+QMTCLOSE:") != 0)
    {
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static void nb_net_reg_clear(void)
{
    nb_cereg_ready = BOOL_FALSE;
    nb_cereg_stat = -1;
    nb_cereg_cause_type = -1;
    nb_cereg_reject_cause = -1;
}

static boolean_en nb_net_registered_for_mqtt(void)
{
    return (nb_cereg_ready == BOOL_TRUE &&
            (nb_cereg_stat == 1 || nb_cereg_stat == 5)) ? BOOL_TRUE : BOOL_FALSE;
}

#if APP_LOG_ENABLE
static const char *nb_iccid_fail_reason_name(void)
{
    switch (iccid_fail_reason)
    {
        case NB_ICCID_FAIL_TIMEOUT: return "timeout";
        case NB_ICCID_FAIL_ERROR: return "error";
        case NB_ICCID_FAIL_BAD_LENGTH: return "bad_length";
        case NB_ICCID_FAIL_NONE:
        default: return "unknown";
    }
}
#endif

static void nb_set_default_iccid(void)
{
    memset(simCardICCID, 0, sizeof(simCardICCID));
    strncpy((char *)simCardICCID, NB_ICCID_DEFAULT, sizeof(simCardICCID) - 1);
    simCardICCID[sizeof(simCardICCID) - 1] = 0;
    iccid_ready = BOOL_FALSE;
}

void  send_AT_Command_machine_star(char *command,uint8 length, char *response,uint32 waitCount, uint8 throwAwayTail)
{
    (void)throwAwayTail;
    if (nb_modem_ota_lock &&
        nb_at_command_allowed_during_ota(command) == BOOL_FALSE)
    {
        OTA_LOGW("modem ota lock blocked at command\r\n");
        return;
    }
    atcommand=command;
    atlength =length;
    atresponse=  response;
    atwaitCount=waitCount;
    read_counter=0;
    resend_counter=0;
    sendcommand_failed = BOOL_FALSE;
    if (strstr((const char *)command, "AT+CGSN"))
    {
        clear_imei_data();
    }
    else if (strstr((const char *)command, "AT+QCCID"))
    {
        clear_iccid_data();
        printf("[ICCID] start AT+QCCID max_attempts=%u\n", (unsigned int)NB_ICCID_MAX_ATTEMPTS);
    }
    sendcommad_state=SEND_COMMAND_STATE_READY;
}

boolean_en  send_AT_Command_machine_finish(void) 
{
    if( sendcommad_state==SEND_COMMAND_STATE_RXING_COMPLETE)
      {
          return BOOL_TRUE;
      }
      else
      {
          return BOOL_FALSE; 
      }  
}
void  send_AT_Command_machine_idle(void)
{
    sendcommad_state=SEND_COMMAND_STATE_IDLE ;
}

boolean_en nb_get_rsrp_dbm10(s32 *rsrp_dbm10)
{
    if (rsrp_ready == BOOL_FALSE || rsrp_dbm10 == 0)
    {
        return BOOL_FALSE;
    }
    *rsrp_dbm10 = nb_rsrp_dbm10;
    return BOOL_TRUE;
}

u64 char_to_digita(u8* buf,u8 len)
{ 
    u8*  recvURC;  
    recvURC=buf;
    u64  leng_temp=0;
    u8 lenth=len;
   while (lenth)
   {
        lenth--;
        leng_temp =leng_temp * 10 + *recvURC - '0';
        recvURC++; 
   }
   return leng_temp;         
}

static u8 copy_digits_from_line(u8 *dst, u8 dst_size, const u8 *src, u8 max_digits)
{
    u8 count;
    u16 i;

    count = 0;
    if (dst_size == 0)
    {
        return 0;
    }
    memset(dst, 0, dst_size);
    for (i = 0; src[i] != 0 && count < (dst_size - 1) && count < max_digits; ++i)
    {
        if (src[i] >= '0' && src[i] <= '9')
        {
            dst[count++] = src[i];
        }
    }
    dst[count] = 0;
    return count;
}

static void clear_imei_data(void)
{
    memset(IMEI, 0, sizeof(IMEI));
    memset(IMEI10, 0, sizeof(IMEI10));
    IMEI_DEC = 0;
    imei_ready = BOOL_FALSE;
}

static void clear_iccid_data(void)
{
    memset(simCardICCID, 0, sizeof(simCardICCID));
    iccid_ready = BOOL_FALSE;
    iccid_fail_reason = NB_ICCID_FAIL_NONE;
#if APP_LOG_ENABLE
    iccid_last_digit_count = 0;
#endif
}

static boolean_en nb_imei_is_ready(void)
{
    u8 i;

    if (imei_ready == BOOL_FALSE)
    {
        return BOOL_FALSE;
    }
    for (i = 0; i < 15; ++i)
    {
        if (IMEI[i] < '0' || IMEI[i] > '9')
        {
            return BOOL_FALSE;
        }
    }
    return (IMEI[15] == 0) ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en nb_iccid_is_ready(void)
{
    u8 i;

    if (iccid_ready == BOOL_FALSE)
    {
        return BOOL_FALSE;
    }
    for (i = 0; i < sizeof(simCardICCID) - 1 && simCardICCID[i] != 0; ++i)
    {
        if (simCardICCID[i] < '0' || simCardICCID[i] > '9')
        {
            return BOOL_FALSE;
        }
    }
    return (i >= 19 && i <= 20) ? BOOL_TRUE : BOOL_FALSE;
}

static void capture_imei_from_line(const u8 *line)
{
    u8 digits[18];
    u8 digit_count;

    digit_count = copy_digits_from_line(digits, sizeof(digits), line, 15);
    if (digit_count == 15)
    {
        memcpy(IMEI, digits, 16);
        memset(IMEI10, 0, sizeof(IMEI10));
        memcpy(IMEI10, IMEI + 8, 7);
        IMEI_DEC = char_to_digita(IMEI10, 7);
        imei_ready = BOOL_TRUE;
        printf("IMEI=%s\n", IMEI);
        printf("IMEI_DEC=%d\n", IMEI_DEC);
    }
}

static void capture_iccid_from_line(const u8 *line)
{
    u8 digits[22];
    u8 digit_count;

    digit_count = copy_digits_from_line(digits, sizeof(digits), line, 20);
#if APP_LOG_ENABLE
    iccid_last_digit_count = digit_count;
#endif
    if (digit_count >= 19)
    {
        memset(simCardICCID, 0, sizeof(simCardICCID));
        memcpy(simCardICCID, digits, digit_count);
        iccid_ready = BOOL_TRUE;
        iccid_fail_reason = NB_ICCID_FAIL_NONE;
        printf("[ICCID] ok iccid=%s\n", (const char *)simCardICCID);
    }
    else if (strstr((const char *)line, "+QCCID:") != 0)
    {
        iccid_fail_reason = NB_ICCID_FAIL_BAD_LENGTH;
        printf("[ICCID] bad_length digits=%u line=%s", (unsigned int)digit_count, (const char *)line);
    }
}

static boolean_en capture_rsrp_from_qeng_line(const u8 *line)
{
    const char *p;
    s32 sign;
    s32 value;

    if (line == 0 ||
        strstr((const char *)line, "+QENG:") == 0 ||
        strstr((const char *)line, "servingcell") == 0)
    {
        return BOOL_FALSE;
    }

    p = (const char *)line;
    while (*p != '\0')
    {
        while (*p != '\0' && *p != '-' && (*p < '0' || *p > '9'))
        {
            ++p;
        }
        if (*p == '\0')
        {
            break;
        }

        sign = 1;
        if (*p == '-')
        {
            sign = -1;
            ++p;
        }
        if (*p < '0' || *p > '9')
        {
            continue;
        }

        value = 0;
        while (*p >= '0' && *p <= '9')
        {
            value = (value * 10) + (*p - '0');
            ++p;
        }
        value *= sign;

        if (value >= -160 && value <= -40)
        {
            nb_rsrp_dbm10 = value * 10;
            rsrp_ready = BOOL_TRUE;
            printf("[QENG] rsrp=%ld.%lddBm\n",
                   (long)(nb_rsrp_dbm10 / 10),
                   (long)((nb_rsrp_dbm10 < 0) ? -(nb_rsrp_dbm10 % 10) : (nb_rsrp_dbm10 % 10)));
            return BOOL_TRUE;
        }
    }
    return BOOL_FALSE;
}

static boolean_en capture_cereg_from_line(const u8 *line)
{
    const char *p;
    int first;
    int second;
    int matched;
    int cause_type;
    int reject_cause;

    if (line == 0)
    {
        return BOOL_FALSE;
    }
    p = strstr((const char *)line, "+CEREG:");
    if (p == 0)
    {
        return BOOL_FALSE;
    }
    first = -1;
    second = -1;
    cause_type = -1;
    reject_cause = -1;
    if (sscanf(p,
               "+CEREG: %d,%d,\"%*[^\"]\",\"%*[^\"]\",%*d,%d,%d",
               &first,
               &second,
               &cause_type,
               &reject_cause) == 4)
    {
        nb_cereg_cause_type = cause_type;
        nb_cereg_reject_cause = reject_cause;
    }
    matched = sscanf(p, "+CEREG: %d,%d", &first, &second);
    if (matched == 2)
    {
        nb_cereg_stat = (s8)second;
    }
    else if (sscanf(p, "+CEREG: %d", &first) == 1)
    {
        nb_cereg_stat = (s8)first;
    }
    else
    {
        return BOOL_FALSE;
    }
    nb_cereg_ready = BOOL_TRUE;
    printf("[NET] CEREG stat=%d cause_type=%d reject_cause=%d\r\n",
           (int)nb_cereg_stat,
           nb_cereg_cause_type,
           nb_cereg_reject_cause);
    return BOOL_TRUE;
}

static void capture_identity_line(const u8 *line)
{
    if (strstr((const char *)atcommand, "AT+CGSN"))
    {
        capture_imei_from_line(line);
    }
    else if (strstr((const char *)atcommand, "AT+QCCID"))
    {
        capture_iccid_from_line(line);
    }
    else if (nb_at_command_is_qeng() == BOOL_TRUE)
    {
        (void)capture_rsrp_from_qeng_line(line);
    }
    else if (nb_at_command_is_cereg() == BOOL_TRUE)
    {
        (void)capture_cereg_from_line(line);
    }
}

static void send_AT_Command_machine_wait_or_retry(void)
{
    if(++read_counter<atwaitCount)
    {
        wait_timer=Timer_GetTickCount();//�ٴζ�ȡ
    }
    else
    {
        if (nb_at_command_is_qccid() == BOOL_TRUE &&
            iccid_fail_reason == NB_ICCID_FAIL_NONE)
        {
            iccid_fail_reason = NB_ICCID_FAIL_TIMEOUT;
        }
        sendcommad_state= SEND_COMMAND_STATE_READY;//���ζ�ȡ���ɹ��ط�
    }
}

static boolean_en nb_mqtt_publish_owns_uart(void)
{
    switch (pubsend_state)
    {
        case PUBSEDN_STATE_SEND_HEADER:
        case PUBSEDN_STATE_WAIT_PROMPT:
        case PUBSEDN_STATE_SEND_PAYLOAD:
        case PUBSEDN_STATE_WAIT_ACK:
            return BOOL_TRUE;
        default:
            return BOOL_FALSE;
    }
}


/**
*@brief   �첽��ʽִ��AT����
*@param	  command�����͵�AT�����ַ���
*@param	  length��AT�����
*@param	  response�������յ���ִ�н���Ӽ�
*@param	  waitCount�����εȴ�������20msΪ��λ��
*@param	  throwAwayTail���Ƿ�����Ӧ�еġ�OK��
*@return  nbģ��ظ���ִ�н�����ַ�����
*/
 void send_AT_Command_machine(void)
{
    if (nb_mqtt_publish_owns_uart() == BOOL_TRUE)
    {
        return;
    }
    if (nb_modem_ota_lock &&
        nb_at_command_allowed_during_ota(atcommand) == BOOL_FALSE)
    {
        sendcommad_state= SEND_COMMAND_STATE_RXING_COMPLETE;
        return;
    }
    switch (sendcommad_state)
    {
        case  SEND_COMMAND_STATE_IDLE :
               break;
        case  SEND_COMMAND_STATE_READY:
        case  SEND_COMMAND_STATE_RESETING :
                if (nb_at_command_is_qccid() == BOOL_TRUE)
                {
                    if (resend_counter >= NB_ICCID_MAX_ATTEMPTS)
                    {
                        if (iccid_fail_reason == NB_ICCID_FAIL_NONE)
                        {
                            iccid_fail_reason = NB_ICCID_FAIL_TIMEOUT;
                        }
                        nb_set_default_iccid();
                        printf("[ICCID] failed reason=%s attempts=%u digits=%u use default invalid iccid=%s\n",
                               nb_iccid_fail_reason_name(),
                               (unsigned int)NB_ICCID_MAX_ATTEMPTS,
                               (unsigned int)iccid_last_digit_count,
                               NB_ICCID_DEFAULT);
                        resend_counter=0;
                        sendcommand_failed = BOOL_FALSE;
                        sendcommad_state= SEND_COMMAND_STATE_RXING_COMPLETE;
                        break;
                    }
                    ++resend_counter;
                }
                else if (resend_counter >= nb_at_command_max_attempts())
                {
                    resend_counter=0;
                    send_AT_Command_machine_mark_failed("timeout");
                    sendcommad_state= SEND_COMMAND_STATE_RXING_COMPLETE;
                    break; 
                 }
                 else
                 {
                    ++resend_counter;
                 }
                 
                  sendcommad_state= SEND_COMMAND_STATE_TXING;

               break;
        case  SEND_COMMAND_STATE_TXING:
               {
                    if (nb_at_command_is_qccid() == BOOL_TRUE)
                    {
                        printf("[ICCID] attempt=%u send AT+QCCID\n", (unsigned int)resend_counter);
                    }
                    if (nb_modem_ota_lock)
                    {
                        nb_modem_send_command_ota((uint8*)atcommand, atlength);
                    }
                    else
                    {
                        sendCommand((uint8*)atcommand, atlength);
                    }
                    sendcommad_state= SEND_COMMAND_STATE_RXING;
                    wait_timer=Timer_GetTickCount();
                    read_counter=0;//�ض�����
               }
              break;
        
        case  SEND_COMMAND_STATE_RXING :   
              if(Timer_PassedDelay(wait_timer, 20))
               {
                   if (readLine(stringBuf, &recvLength, 0))
                    {
                        capture_identity_line(stringBuf);
                        if (nb_at_command_is_qccid() == BOOL_TRUE &&
                            strstr((const char *)stringBuf, "ERROR") != 0)
                        {
                            iccid_fail_reason = NB_ICCID_FAIL_ERROR;
                            printf("[ICCID] error line=%s", (const char *)stringBuf);
                        }
                        if (strstr((const char *) stringBuf, atresponse))
                        {
                            if (strstr((const char *)atcommand, "AT+CGSN") &&
                                nb_imei_is_ready() == BOOL_FALSE)
                            {
                                recvLength = 0;
                                send_AT_Command_machine_wait_or_retry();
                                break;
                            }
                            if (nb_at_command_is_qccid() == BOOL_TRUE &&
                                nb_iccid_is_ready() == BOOL_FALSE)
                            {
                                recvLength = 0;
                                send_AT_Command_machine_wait_or_retry();
                                break;
                            }
                            recvLength = 0;
                            sendcommand_failed = BOOL_FALSE;
                            sendcommad_state= SEND_COMMAND_STATE_RXING_COMPLETE;
                            resend_counter=0;//�ط���������
                        }
                        else if (nb_at_command_terminal_failure(stringBuf) == BOOL_TRUE)
                        {
                            recvLength = 0;
                            resend_counter=0;
                            send_AT_Command_machine_mark_failed("terminal");
                            sendcommad_state= SEND_COMMAND_STATE_RXING_COMPLETE;
                        }
                        else
                        {
                            recvLength = 0;
                            send_AT_Command_machine_wait_or_retry();
                        }
                    }
                    else
                    {
                        send_AT_Command_machine_wait_or_retry();
                    }
             }          
             break;
          case  SEND_COMMAND_STATE_RXING_COMPLETE :
            break;
       
        default :
          break;
    }
    
    
}



static void nb_start_at_probe(CONNECT_CONFIG_state_en next_state)
{
    send_AT_Command_machine_idle();
    send_AT_Command_machine_star("AT\r\n",
                                 strlen("AT\r\n"),
                                 "OK",
                                 NB_AT_TIMEOUT_COUNT(NB_AT_PROBE_TIMEOUT_MS),
                                 0);
    connect_state = next_state;
}

static void nb_start_cpin_query(void)
{
    send_AT_Command_machine_star("AT+CPIN?\r\n",
                                 strlen("AT+CPIN?\r\n"),
                                 "+CPIN: READY",
                                 NB_AT_TIMEOUT_COUNT(1000UL),
                                 0);
    connect_state = CONNECT_CONFIG_AT_CPIN;
}

static void nb_start_cereg_query(void)
{
    nb_net_reg_clear();
    send_AT_Command_machine_star("AT+CEREG?\r\n",
                                 strlen("AT+CEREG?\r\n"),
                                 "OK",
                                 NB_AT_TIMEOUT_COUNT(1000UL),
                                 0);
    connect_state = CONNECT_CONFIG_AT_CEREG_QUERY;
}

static void nb_start_mqtt_configuration(void)
{
    clear_imei_data();
    send_AT_Command_machine_star("AT+CGSN\r\n",
                                 strlen("AT+CGSN\r\n"),
                                 "OK",
                                 NB_AT_TIMEOUT_COUNT(1000UL),
                                 1);
    connect_state = CONNECT_CONFIG_AT_IEMI;
}

static void nb_schedule_hardware_reset(const char *reason)
{
    printf("[NB][E] hardware recovery reason=%s\r\n",
           (reason != 0) ? reason : "unknown");
    nb_recovery_level = NB_RECOVERY_HARD_RESET;
    send_AT_Command_machine_idle();
    connect_state = CONNECT_CONFIG_HARD_RESET_START;
}

static NB_DISCONNECT_REASON_EN nb_disconnect_reason_from_text(const char *reason)
{
    if (reason == 0)
    {
        return NB_DISCONNECT_NONE;
    }
    if (strstr(reason, "QMTSTAT") != 0)
    {
        return NB_DISCONNECT_QMTSTAT;
    }
    if (strstr(reason, "PDP") != 0)
    {
        return NB_DISCONNECT_PDP_DEACT;
    }
    if (strstr(reason, "HEARTBEAT") != 0 ||
        strstr(reason, "heartbeat") != 0)
    {
        return NB_DISCONNECT_HEARTBEAT;
    }
    if (strstr(reason, "LOGIN") != 0 ||
        strstr(reason, "login") != 0)
    {
        return NB_DISCONNECT_LOGIN;
    }
    if (strstr(reason, "PUBLISH") != 0)
    {
        return NB_DISCONNECT_PUBLISH;
    }
    if (strstr(reason, "OTA") != 0)
    {
        return NB_DISCONNECT_OTA_PLANNED;
    }
    return NB_DISCONNECT_MQTT_SETUP;
}

static void nb_notify_offline_once(const char *reason)
{
    if (nb_offline_event_sent == BOOL_FALSE)
    {
        nb_offline_event_sent = BOOL_TRUE;
        nb_disconnect_reason = nb_disconnect_reason_from_text(reason);
        printf("[NB] offline reason=%s code=%u\r\n",
               (reason != 0) ? reason : "unknown",
               (unsigned int)nb_disconnect_reason);
        onNBEvent(NB_EVENT_LOST_CONNECTION, 0, 0);
    }
}

void nb_request_reconnect(const char *reason)
{
    uint32 backoff_ms;

    nb_notify_offline_once(reason);
    pubsend_state_set_idle();
    if (nb_modem_ota_lock)
    {
        printf("[NB] intentional OTA disconnect; reconnect suppressed\r\n");
        return;
    }

    if (nb_mqtt_fast_retry_count < NB_MQTT_FAST_RETRY_COUNT)
    {
        ++nb_mqtt_fast_retry_count;
        backoff_ms = (nb_mqtt_fast_retry_count == 1U) ?
                     NB_MQTT_RETRY_1_MS : NB_MQTT_RETRY_2_MS;
        nb_recovery_level = NB_RECOVERY_MQTT_FAST;
        nb_recovery_wait_ms = backoff_ms;
        printf("[NB] recovery=mqtt attempt=%u/%u backoff=%lums reason=%s\r\n",
               (unsigned int)nb_mqtt_fast_retry_count,
               (unsigned int)NB_MQTT_FAST_RETRY_COUNT,
               (unsigned long)backoff_ms,
               (reason != 0) ? reason : "unknown");
        send_AT_Command_machine_idle();
        send_AT_Command_machine_star("AT+QMTCLOSE=0\r\n",
                                     strlen("AT+QMTCLOSE=0\r\n"),
                                     "+QMTCLOSE: 0,0",
                                     NB_AT_TIMEOUT_COUNT(NB_QMTCLOSE_TIMEOUT_MS),
                                     1);
        connect_state = CONNECT_CONFIG_WAITING_QMTCLOSE;
        return;
    }

    nb_mqtt_fast_retry_count = 0;
    nb_recovery_level = NB_RECOVERY_NETWORK;
    nb_cfun_recovery_used = 0;
    nb_registration_tick = Timer_GetTickCount();
    printf("[NB] recovery=network reason=%s\r\n",
           (reason != 0) ? reason : "unknown");
    nb_start_cpin_query();
}

void _4G_configModule_machine_star(void)
{
    clear_imei_data();
    clear_iccid_data();
    nb_net_reg_clear();
    nb_cfun_recovery_used = 0;
    nb_mqtt_fast_retry_count = 0;
    nb_offline_event_sent = BOOL_FALSE;
    nb_recovery_level = NB_RECOVERY_NONE;
    nb_start_reason = NB_START_REASON_UNKNOWN;
    nb_disconnect_reason = NB_DISCONNECT_NONE;
    sendcommand_failed = BOOL_FALSE;
    state = NB_STATE_CONNECTING;
    nb_start_at_probe(CONNECT_CONFIG_PROBE_AT);
}

boolean_en _4G_configModule_machine_finish(void)
{
    return (connect_state == CONNECT_CONFIG__COMPLETE) ? BOOL_TRUE : BOOL_FALSE;
}

static void nb_process_wait_at(boolean_en after_hard_reset)
{
    if (_4g_reset_finish() == BOOL_FALSE)
    {
        return;
    }
    if (sendcommad_state == SEND_COMMAND_STATE_IDLE)
    {
        nb_at_ready_tick = Timer_GetTickCount();
        nb_recovery_wait_tick = 0;
        nb_start_at_probe(after_hard_reset ?
                          CONNECT_CONFIG_WAIT_HARD_RESET_AT :
                          CONNECT_CONFIG_WAIT_AT);
        return;
    }
    if (send_AT_Command_machine_finish() == BOOL_FALSE)
    {
        return;
    }
    if (send_AT_Command_machine_failed() == BOOL_FALSE)
    {
        nb_trace_at_ready();
        _4g_reset_idle();
        nb_start_cpin_query();
        return;
    }
    if (Timer_PassedDelay(nb_at_ready_tick, NB_AT_READY_WAIT_MS) == BOOL_TRUE)
    {
        send_AT_Command_machine_idle();
        if (after_hard_reset == BOOL_TRUE)
        {
            nb_recovery_level = NB_RECOVERY_HARD_RESET;
            nb_recovery_wait_tick = Timer_GetTickCount();
            nb_recovery_wait_ms = NB_MQTT_RETRY_2_MS;
            connect_state = CONNECT_CONFIG_RECOVERY_WAIT;
        }
        else
        {
            connect_state = CONNECT_CONFIG_HARD_RESET_START;
        }
        return;
    }
    if (nb_recovery_wait_tick == 0U ||
        Timer_PassedDelay(nb_recovery_wait_tick, NB_AT_READY_QUERY_INTERVAL_MS) == BOOL_TRUE)
    {
        nb_recovery_wait_tick = Timer_GetTickCount();
        nb_start_at_probe(after_hard_reset ?
                          CONNECT_CONFIG_WAIT_HARD_RESET_AT :
                          CONNECT_CONFIG_WAIT_AT);
    }
}

static boolean_en nb_mqtt_step_failed(const char *step)
{
    if (send_AT_Command_machine_failed() == BOOL_FALSE)
    {
        return BOOL_FALSE;
    }
    printf("[NB][E] mqtt step=%s failed\r\n", (step != 0) ? step : "unknown");
    nb_request_reconnect(step);
    return BOOL_TRUE;
}

static boolean_en nb_prepare_will(void)
{
    const char *topic;
    int payload_len;
    int command_len;

    topic = zk_mqtt_get_will_topic();
    if (topic == 0)
    {
        return BOOL_FALSE;
    }
    payload_len = zk_make_offline_packet(nb_will_payload, sizeof(nb_will_payload));
    if (payload_len <= 0 || payload_len >= (int)sizeof(nb_will_payload))
    {
        return BOOL_FALSE;
    }
    command_len = snprintf(nb_will_command,
                           sizeof(nb_will_command),
                           "AT+QMTCFG=\"willex\",0,1,1,0,\"%s\",%u\r\n",
                           topic,
                           (unsigned int)payload_len);
    if (command_len <= 0 || command_len >= (int)sizeof(nb_will_command))
    {
        return BOOL_FALSE;
    }
    nb_will_payload_length = (u16)payload_len;
    return BOOL_TRUE;
}

static void nb_start_qmtopen(void)
{
    static char sendStringBufOpen[96];
    int cmd_len;

    cmd_len = zk_build_qmt_open_cmd(sendStringBufOpen, sizeof(sendStringBufOpen));
    if (cmd_len <= 0 || cmd_len >= (int)sizeof(sendStringBufOpen))
    {
        nb_request_reconnect("QMTOPEN_BUILD");
        return;
    }
    send_AT_Command_machine_star(sendStringBufOpen,
                                 (uint8)cmd_len,
                                 "+QMTOPEN: 0,0",
                                 NB_AT_TIMEOUT_COUNT(NB_QMTOPEN_TIMEOUT_MS),
                                 1);
    connect_state = CONNECT_CONFIG_AT_IPPORT;
}

void _4G_configModule_machine(void)
{
    if (nb_modem_ota_lock)
    {
        return;
    }
    nb_trace_state_change();
    switch(connect_state)
    {
        case CONNECT_CONFIG_STATE_IDLE:
            break;

        case CONNECT_CONFIG_PROBE_AT:
            if (send_AT_Command_machine_finish() == BOOL_TRUE)
            {
                if (send_AT_Command_machine_failed() == BOOL_FALSE)
                {
                    nb_start_reason = NB_START_REASON_AT_REUSE;
                    nb_trace_at_ready();
                    nb_start_cpin_query();
                }
                else
                {
                    send_AT_Command_machine_idle();
                    connect_state = CONNECT_CONFIG_PWRKEY_START;
                }
            }
            break;

        case CONNECT_CONFIG_PWRKEY_START:
            nb_start_reason = NB_START_REASON_COLD_PWRKEY;
            resetNbModule();
            send_AT_Command_machine_idle();
            connect_state = CONNECT_CONFIG_WAIT_AT;
            break;

        case CONNECT_CONFIG_WAIT_AT:
            nb_process_wait_at(BOOL_FALSE);
            break;

        case CONNECT_CONFIG_HARD_RESET_START:
            nb_start_reason = NB_START_REASON_HARD_RECOVERY;
            nb_cfun_recovery_used = 0U;
            hardResetNbModule();
            send_AT_Command_machine_idle();
            connect_state = CONNECT_CONFIG_WAIT_HARD_RESET_AT;
            break;

        case CONNECT_CONFIG_WAIT_HARD_RESET_AT:
            nb_process_wait_at(BOOL_TRUE);
            break;

        case CONNECT_CONFIG_AT_CPIN:
            if (send_AT_Command_machine_finish() == BOOL_TRUE)
            {
                if (send_AT_Command_machine_failed() == BOOL_TRUE)
                {
                    nb_schedule_hardware_reset("CPIN");
                    break;
                }
                nb_trace_milestone("SIM_READY");
                nb_registration_tick = Timer_GetTickCount();
                send_AT_Command_machine_star("AT+CEREG=1\r\n",
                                             strlen("AT+CEREG=1\r\n"),
                                             "OK",
                                             NB_AT_TIMEOUT_COUNT(1000UL),
                                             0);
                connect_state = CONNECT_CONFIG_AT_CEREG_ENABLE;
            }
            break;

        case CONNECT_CONFIG_AT_CEREG_ENABLE:
            if (send_AT_Command_machine_finish() == BOOL_TRUE)
            {
                if (send_AT_Command_machine_failed() == BOOL_TRUE)
                {
                    nb_schedule_hardware_reset("CEREG_ENABLE");
                    break;
                }
                nb_start_cereg_query();
            }
            break;

        case CONNECT_CONFIG_AT_CEREG_QUERY:
            if (send_AT_Command_machine_finish() == BOOL_TRUE)
            {
                if (send_AT_Command_machine_failed() == BOOL_FALSE &&
                    nb_net_registered_for_mqtt() == BOOL_TRUE)
                {
                    nb_trace_milestone("REGISTERED");
                    nb_start_mqtt_configuration();
                    break;
                }
                if ((nb_cereg_ready == BOOL_TRUE && nb_cereg_stat == 3) ||
                    Timer_PassedDelay(nb_registration_tick, NB_CEREG_RECOVERY_TIMEOUT_MS) == BOOL_TRUE)
                {
                    if (nb_cfun_recovery_used == 0U)
                    {
                        nb_cfun_recovery_used = 1U;
                        printf("[NET] CEREG recovery via CFUN stat=%d\r\n", (int)nb_cereg_stat);
                        send_AT_Command_machine_star("AT+CFUN=0\r\n",
                                                     strlen("AT+CFUN=0\r\n"),
                                                     "OK",
                                                     NB_AT_TIMEOUT_COUNT(NB_CFUN_TIMEOUT_MS),
                                                     0);
                        connect_state = CONNECT_CONFIG_AT_CFUN0;
                    }
                    else
                    {
                        nb_schedule_hardware_reset("CEREG");
                    }
                    break;
                }
                nb_recovery_wait_tick = Timer_GetTickCount();
                send_AT_Command_machine_idle();
                connect_state = CONNECT_CONFIG_WAIT_CEREG_QUERY;
            }
            break;

        case CONNECT_CONFIG_WAIT_CEREG_QUERY:
            if (Timer_PassedDelay(nb_recovery_wait_tick, NB_CEREG_QUERY_INTERVAL_MS) == BOOL_TRUE)
            {
                nb_start_cereg_query();
            }
            break;

        case CONNECT_CONFIG_AT_CFUN0:
            if (send_AT_Command_machine_finish() == BOOL_TRUE)
            {
                if (send_AT_Command_machine_failed() == BOOL_TRUE)
                {
                    nb_schedule_hardware_reset("CFUN0");
                    break;
                }
                send_AT_Command_machine_star("AT+CFUN=1\r\n",
                                             strlen("AT+CFUN=1\r\n"),
                                             "OK",
                                             NB_AT_TIMEOUT_COUNT(NB_CFUN_TIMEOUT_MS),
                                             0);
                connect_state = CONNECT_CONFIG_AT_CFUN1;
            }
            break;

        case CONNECT_CONFIG_AT_CFUN1:
            if (send_AT_Command_machine_finish() == BOOL_TRUE)
            {
                if (send_AT_Command_machine_failed() == BOOL_TRUE)
                {
                    nb_schedule_hardware_reset("CFUN1");
                    break;
                }
                nb_registration_tick = Timer_GetTickCount();
                nb_start_cpin_query();
            }
            break;

        case CONNECT_CONFIG_AT_IEMI:
            if (send_AT_Command_machine_finish() == BOOL_TRUE)
            {
                if (send_AT_Command_machine_failed() == BOOL_TRUE ||
                    zk_mqtt_init() == BOOL_FALSE)
                {
                    nb_request_reconnect("CGSN");
                    break;
                }
                send_AT_Command_machine_star("AT+QMTCFG=\"recv/mode\",0,0,0\r\n",
                                             strlen("AT+QMTCFG=\"recv/mode\",0,0,0\r\n"),
                                             "OK",
                                             NB_AT_TIMEOUT_COUNT(1000UL),
                                             1);
                connect_state = CONNECT_CONFIG_AT_RECVMODE;
            }
            break;

        case CONNECT_CONFIG_AT_RECVMODE:
            if (send_AT_Command_machine_finish() == BOOL_TRUE)
            {
                if (nb_mqtt_step_failed("RECVMODE") == BOOL_TRUE) break;
                send_AT_Command_machine_star("AT+QMTCFG=\"version\",0,4\r\n",
                                             strlen("AT+QMTCFG=\"version\",0,4\r\n"),
                                             "OK",
                                             NB_AT_TIMEOUT_COUNT(1000UL),
                                             1);
                connect_state = CONNECT_CONFIG_AT_VERSION;
            }
            break;

        case CONNECT_CONFIG_AT_VERSION:
            if (send_AT_Command_machine_finish() == BOOL_TRUE)
            {
                if (nb_mqtt_step_failed("VERSION") == BOOL_TRUE) break;
                send_AT_Command_machine_star("AT+QMTCFG=\"keepalive\",0,30\r\n",
                                             strlen("AT+QMTCFG=\"keepalive\",0,30\r\n"),
                                             "OK",
                                             NB_AT_TIMEOUT_COUNT(1000UL),
                                             1);
                connect_state = CONNECT_CONFIG_AT_keepalive;
            }
            break;

        case CONNECT_CONFIG_AT_keepalive:
            if (send_AT_Command_machine_finish() == BOOL_TRUE)
            {
                if (nb_mqtt_step_failed("KEEPALIVE") == BOOL_TRUE) break;
                send_AT_Command_machine_star("AT+QMTCFG=\"session\",0,1\r\n",
                                             strlen("AT+QMTCFG=\"session\",0,1\r\n"),
                                             "OK",
                                             NB_AT_TIMEOUT_COUNT(1000UL),
                                             1);
                connect_state = CONNECT_CONFIG_AT_SESSION;
            }
            break;

        case CONNECT_CONFIG_AT_SESSION:
            if (send_AT_Command_machine_finish() == BOOL_TRUE)
            {
                if (nb_mqtt_step_failed("SESSION") == BOOL_TRUE) break;
                send_AT_Command_machine_star("AT+QMTCFG=\"timeout\",0,5,3,1\r\n",
                                             strlen("AT+QMTCFG=\"timeout\",0,5,3,1\r\n"),
                                             "OK",
                                             NB_AT_TIMEOUT_COUNT(1000UL),
                                             1);
                connect_state = CONNECT_CONFIG_AT_TIMEOUT;
            }
            break;

        case CONNECT_CONFIG_AT_TIMEOUT:
            if (send_AT_Command_machine_finish() == BOOL_TRUE)
            {
                if (nb_mqtt_step_failed("TIMEOUT") == BOOL_TRUE) break;
                if (nb_prepare_will() == BOOL_FALSE)
                {
                    nb_request_reconnect("WILL_BUILD");
                    break;
                }
                send_AT_Command_machine_idle();
                sendCommand((uint8 *)nb_will_command, (uint16)strlen(nb_will_command));
                nb_will_tick = Timer_GetTickCount();
                connect_state = CONNECT_CONFIG_AT_WILL_PROMPT;
            }
            break;

        case CONNECT_CONFIG_AT_WILL_PROMPT:
            if (nb_mqtt_publish_read_prompt() == BOOL_TRUE)
            {
                usartSendData((uint8 *)nb_will_payload, nb_will_payload_length);
                recvLength = 0;
                nb_will_tick = Timer_GetTickCount();
                connect_state = CONNECT_CONFIG_AT_WILL_RESULT;
            }
            else if (Timer_PassedDelay(nb_will_tick, 5000UL) == BOOL_TRUE)
            {
                nb_request_reconnect("WILL_PROMPT");
            }
            break;

        case CONNECT_CONFIG_AT_WILL_RESULT:
            if (readLine(stringBuf, &recvLength, 0))
            {
                if (strstr((const char *)stringBuf, "OK") != 0)
                {
                    recvLength = 0;
                    nb_start_qmtopen();
                }
                else if (strstr((const char *)stringBuf, "ERROR") != 0)
                {
                    recvLength = 0;
                    nb_request_reconnect("WILL_RESULT");
                }
                else
                {
                    recvLength = 0;
                }
            }
            else if (Timer_PassedDelay(nb_will_tick, 5000UL) == BOOL_TRUE)
            {
                nb_request_reconnect("WILL_TIMEOUT");
            }
            break;

        case CONNECT_CONFIG_AT_IPPORT:
            if (send_AT_Command_machine_finish() == BOOL_TRUE)
            {
                static char sendStringBufConn[128];
                int cmd_len;

                if (nb_mqtt_step_failed("QMTOPEN") == BOOL_TRUE) break;
                nb_trace_milestone("QMTOPEN");
                cmd_len = zk_build_qmt_conn_cmd(sendStringBufConn, sizeof(sendStringBufConn));
                if (cmd_len <= 0 || cmd_len >= (int)sizeof(sendStringBufConn))
                {
                    nb_request_reconnect("QMTCONN_BUILD");
                    break;
                }
                send_AT_Command_machine_star(sendStringBufConn,
                                             (uint8)cmd_len,
                                             "+QMTCONN: 0,0,0",
                                             NB_AT_TIMEOUT_COUNT(NB_QMTCONN_TIMEOUT_MS),
                                             1);
                connect_state = CONNECT_CONFIG_AT_QMTCONN;
            }
            break;

        case CONNECT_CONFIG_AT_QMTCONN:
            if (send_AT_Command_machine_finish() == BOOL_TRUE)
            {
                if (nb_mqtt_step_failed("QMTCONN") == BOOL_TRUE) break;
                nb_trace_milestone("QMTCONN");
                send_AT_Command_machine_star("AT+QCCID\r\n",
                                             strlen("AT+QCCID\r\n"),
                                             "+QCCID:",
                                             NB_AT_TIMEOUT_COUNT(1000UL),
                                             1);
                connect_state = CONNECT_CONFIG_AT_QCCID;
            }
            break;

        case CONNECT_CONFIG_AT_QCCID:
            if (send_AT_Command_machine_finish()==TRUE)
            {
                static char sendStringBufSub[96];
                int cmd_len;

                zk_device_config_refresh_iccid();
                cmd_len = zk_build_qmt_sub_cmd(sendStringBufSub, sizeof(sendStringBufSub));
                if (cmd_len <= 0 || cmd_len >= (int)sizeof(sendStringBufSub))
                {
                    nb_request_reconnect("QMTSUB_BUILD");
                    break;
                }
                send_AT_Command_machine_star(sendStringBufSub,
                                             (uint8)cmd_len,
                                             "+QMTSUB: 0,1,0",
                                             NB_AT_TIMEOUT_COUNT(NB_QMTSUB_TIMEOUT_MS),
                                             1);
                connect_state = CONNECT_CONFIG_AT_QMTSUB;
            }
            break;

        case CONNECT_CONFIG_AT_QMTSUB:
            if (send_AT_Command_machine_finish()==TRUE)
            {
                static char sendStringBufSubUp[96];
                int cmd_len;

                if (nb_mqtt_step_failed("QMTSUB_DOWN") == BOOL_TRUE) break;
                cmd_len = zk_build_qmt_sub_upgrade_cmd(sendStringBufSubUp, sizeof(sendStringBufSubUp));
                if (cmd_len <= 0 || cmd_len >= (int)sizeof(sendStringBufSubUp))
                {
                    nb_request_reconnect("QMTSUB_UP_BUILD");
                    break;
                }
                send_AT_Command_machine_star(sendStringBufSubUp,
                                             (uint8)cmd_len,
                                             "+QMTSUB: 0,2,0",
                                             NB_AT_TIMEOUT_COUNT(NB_QMTSUB_TIMEOUT_MS),
                                             1);
                connect_state = CONNECT_CONFIG_AT_LAST;
            }
            break;

        case CONNECT_CONFIG_AT_LAST:
            if (send_AT_Command_machine_finish()==TRUE)
            {
                if (nb_mqtt_step_failed("QMTSUB_UP") == BOOL_TRUE) break;
                connect_state = CONNECT_CONFIG__COMPLETE;
                state = NB_STATE_CONNECTED;
                nb_trace_milestone("SUBSCRIBED");
                onNBEvent(NB_EVENT_CONNECTED, 0, 0);
            }
            break;

        case CONNECT_CONFIG_WAITING_QMTCLOSE:
            if (send_AT_Command_machine_finish()==TRUE)
            {
                send_AT_Command_machine_idle();
                nb_recovery_wait_tick = Timer_GetTickCount();
                connect_state = CONNECT_CONFIG_RECOVERY_WAIT;
            }
            break;

        case CONNECT_CONFIG_RECOVERY_WAIT:
            if (Timer_PassedDelay(nb_recovery_wait_tick, nb_recovery_wait_ms) == BOOL_TRUE)
            {
                if (nb_recovery_level == NB_RECOVERY_MQTT_FAST)
                {
                    nb_start_mqtt_configuration();
                }
                else if (nb_recovery_level == NB_RECOVERY_NETWORK)
                {
                    nb_start_cpin_query();
                }
                else
                {
                    connect_state = CONNECT_CONFIG_HARD_RESET_START;
                }
            }
            break;

        case CONNECT_CONFIG__COMPLETE:
            break;

        default:
            break;
    }
}


/**
*@brief   �������ӵķ�������Ϣ
*@param	  address����������ַ��ע�⣺������ֻ�����������ַ����ָ�룬����������
*@param	  length����������ַ����
*@param	  port���������˿ں�
*@return  ��
*/
/*
void nbDriverInit(uint8 *address, uint8 length, uint32 port) { 
    serverAddressLength = length;
    serverAddress = address;
    serverPort = port;
}
*/
/**
*@brief   ���ӷ�����
*@return  ��
*/
/*
static void connectServer(void) 
{
    if(0 == sendCommandAndReceiveResponse("AT+CGPADDR?\r\n", 13, "+CGPADDR: 0,\"", 20, 0))
    {
        return;
    }

    sendCommandAndReceiveResponse("AT+QICLOSE=0\r\n", 14, "OK", 20, 0);
    usartSendData("AT+QIOPEN=0,0,\"TCP\",\"", 21);
    usartSendData((uint8 *) serverAddress, serverAddressLength);
    usartSendData("\",", 2);
    sprintf((char *) stringBuf, "%d\r\n", serverPort);
    usartSendData(stringBuf, strlen((char const *) stringBuf));
#ifdef NB_DEBUG_PRINT
    printf("nb:connect server:%s,%d\r\n", serverAddress, serverPort);
#endif
}*/

/**
*@brief   �������ģʽ
*@return  ��
*/
/*
void nbEnterIDLE(void) {                                        //-----------------------------------------------
    recvLength = 0;
    sendCommandAndReceiveResponse("AT+QICLOSE=0\r\n", 14, "CLOSE OK", 20, 0);
    reconnectCount = 0;
    flushQueue(&usartRecvQueue);
    state = NB_STATE_IDLE;
    idleTimer = Timer_GetTickCount();
#ifdef NB_DEBUG_PRINT
    printf("nb:enter idle\r\n");
#endif
}*/


//MQTT ������Ϣ     

/**
*@brief   ����tcp����
*@param	  pData��Ҫ���͵����ݻ���
*@param	  length�����͵����ݳ���
*@return  NB_ERROR_NONE�����ͳɹ�������������ʧ��
*/


PUBSEDN_STATE_EN pubsend_state=PUBSEDN_STATE_IDLE ;
static uint8 *pubData;
static uint16 publength;
static uint8 pubDataBuf[ZK_JSON_BUF_SIZE];
static u8 pub_en_flag=0;
static uint16 pub_msg_id=0;
static u32 nb_mqtt_pub_success_count=0;
static u32 nb_mqtt_pub_fail_count=0;
static u32 nb_mqtt_pub_timeout_count=0;
//
static  char sendStringBuf3[128];

static boolean_en nb_at_command_is_busy(void)
{
    if (sendcommad_state == SEND_COMMAND_STATE_IDLE ||
        sendcommad_state == SEND_COMMAND_STATE_RXING_COMPLETE)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

static boolean_en pubsend_is_busy(void)
{
    /* pub_en_flag covers the QMTPUBEX header-to-payload window. */
    if (pub_en_flag ||
        pubsend_state != PUBSEDN_STATE_IDLE ||
        nb_at_command_is_busy() == BOOL_TRUE)
    {
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static uint8 nb_mqtt_publish_prepare(const char *topic, const uint8 *payload, uint16 length)
{
    int cmd_len;

    if (topic == 0 || payload == 0 || length == 0 || length >= sizeof(pubDataBuf))
    {
        return NB_ERROR_SEND_FAIL;
    }
    if (pubsend_is_busy() == BOOL_TRUE)
    {
        return NB_ERROR_SEND_FAIL;
    }

    pub_msg_id = zk_mqtt_next_packet_id();
    cmd_len = snprintf(sendStringBuf3, sizeof(sendStringBuf3),
                       "AT+QMTPUBEX=0,%u,1,0,\"%s\",%u\r\n",
                       (unsigned int)pub_msg_id,
                       topic,
                       (unsigned int)length);
    if (cmd_len <= 0 || cmd_len >= (int)sizeof(sendStringBuf3))
    {
        return NB_ERROR_SEND_FAIL;
    }

    memcpy(pubDataBuf, payload, length);
    pubData = pubDataBuf;
    publength = length;
    pub_en_flag = 0;
    pubsend_state = PUBSEDN_STATE_SEND_HEADER;
    pub_timer = Timer_GetTickCount();
    printf("[MQTT] publish topic=%s len=%u pkt=%u\n",
           topic,
           (unsigned int)length,
           (unsigned int)pub_msg_id);
    return NB_ERROR_NONE;
}

static boolean_en nb_mqtt_publish_read_prompt(void)
{
    uint8 ch;

    while (dequeue(&usartRecvQueue, &ch))
    {
        if (ch == '>')
        {
            return BOOL_TRUE;
        }
    }
    return BOOL_FALSE;
}

static boolean_en nb_mqtt_parse_publish_ack(const uint8 *line, unsigned int *packet_id, int *result)
{
    const char *ack;
    unsigned int client_id;

    if (line == 0 || packet_id == 0 || result == 0)
    {
        return BOOL_FALSE;
    }
    ack = strstr((const char *)line, "+QMTPUBEX:");
    if (ack == 0)
    {
        return BOOL_FALSE;
    }
    if (sscanf(ack, "+QMTPUBEX: %u,%u,%d", &client_id, packet_id, result) != 3)
    {
        return BOOL_FALSE;
    }
    if (client_id != 0U)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

static boolean_en nb_mqtt_publish_ack_ok(const uint8 *line)
{
    unsigned int ack_packet_id;
    int ack_result;

    if (nb_mqtt_parse_publish_ack(line, &ack_packet_id, &ack_result) == BOOL_TRUE &&
        ack_packet_id == (unsigned int)pub_msg_id &&
        ack_result == 0)
    {
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static boolean_en nb_mqtt_publish_ack_failed(const uint8 *line)
{
    unsigned int ack_packet_id;
    int ack_result;

    if (strstr((const char *)line, "ERROR") != 0 ||
        nb_is_link_lost_line((uint8 *)line) == BOOL_TRUE)
    {
        return BOOL_TRUE;
    }
    if (nb_mqtt_parse_publish_ack(line, &ack_packet_id, &ack_result) == BOOL_TRUE &&
        ack_packet_id == (unsigned int)pub_msg_id &&
        ack_result != 0)
    {
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static void nb_mqtt_publish_fail(const char *reason, boolean_en timeout)
{
    if (timeout == BOOL_TRUE)
    {
        nb_mqtt_pub_timeout_count++;
    }
    nb_mqtt_pub_fail_count++;
    printf("[MQTT] publish fail pkt=%u reason=%s fail=%lu timeout=%lu\n",
           (unsigned int)pub_msg_id,
           reason,
           nb_mqtt_pub_fail_count,
           nb_mqtt_pub_timeout_count);
    pub_en_flag = 0;
    pubsend_state = PUBSEDN_STATE_FAIL;
}

uint8 nbSendTcpData(uint8 *pData, uint16 length)
{
    const char *topic;

    if (nb_modem_ota_lock)
    {
        return NB_ERROR_SEND_FAIL;
    }
    if (pData == 0 || length == 0 || length >= sizeof(pubDataBuf))
    {
        return NB_ERROR_SEND_FAIL;
    }
    if (pubsend_is_busy() == BOOL_TRUE)
    {
        return NB_ERROR_SEND_FAIL;
    }

    topic = zk_mqtt_get_pub_topic();
    if (topic == 0)
    {
        return NB_ERROR_SEND_FAIL;
    }
    return nb_mqtt_publish_prepare(topic, pData, length);
}
uint8 g4Send_MQTT_Data(char *topic,char *pData)
{
    const char *pub_topic;
    uint16 length;

    if (nb_modem_ota_lock)
    {
        return NB_ERROR_SEND_FAIL;
    }
    if (pData == 0)
    {
        return NB_ERROR_SEND_FAIL;
    }
    pub_topic = topic;
#if ZK_PROTOCOL_ONLY
    if(pub_topic == 0 || strncmp(pub_topic, "MS/", 3) != 0)
    {
        return NB_ERROR_SEND_FAIL;
    }
#else
    if(pub_topic == 0 || strncmp(pub_topic, "MS/", 3) != 0)
    {
        pub_topic = zk_mqtt_get_pub_topic();
    }
    if (pub_topic == 0)
    {
        return NB_ERROR_SEND_FAIL;
    }
#endif
    length = (uint16)strlen(pData);
    if (length == 0 || length >= sizeof(pubDataBuf))
    {
        return NB_ERROR_SEND_FAIL;
    }
    if (pubsend_is_busy() == BOOL_TRUE)
    {
        return NB_ERROR_SEND_FAIL;
    }

    return nb_mqtt_publish_prepare(pub_topic, (uint8 *)pData, length);
}

boolean_en pubsend_state_finish()
{
    if( pubsend_state==PUBSEDN_STATE_SENDFINISH)
    {
     return BOOL_TRUE ;
    }
    else{
    return BOOL_FALSE;}
    
}
boolean_en pubsend_state_idle()
{
    return (pubsend_is_busy() == BOOL_FALSE) ? BOOL_TRUE : BOOL_FALSE;
}

uint32 nb_mqtt_get_publish_success_count(void)
{
    return nb_mqtt_pub_success_count;
}

uint32 nb_mqtt_get_publish_fail_count(void)
{
    return nb_mqtt_pub_fail_count;
}

uint32 nb_mqtt_get_publish_timeout_count(void)
{
    return nb_mqtt_pub_timeout_count;
}


void pubsend_state_set_idle(void)
{
    pub_en_flag=0;
    pubsend_state=PUBSEDN_STATE_IDLE;
}

void nb_modem_lock_for_ota(void)
{
    if (nb_modem_ota_lock == 0U)
    {
        OTA_LOGI("modem ota lock on\r\n");
    }
    nb_modem_ota_lock = 1U;
    if (nb_at_command_allowed_during_ota(atcommand) == BOOL_FALSE)
    {
        atcommand = 0;
        atresponse = 0;
        atlength = 0;
        atwaitCount = 0;
        read_counter = 0;
        resend_counter = 0;
        sendcommad_state= SEND_COMMAND_STATE_RXING_COMPLETE;
    }
    pub_en_flag = 0U;
    pubsend_state = PUBSEDN_STATE_IDLE;
}

void nb_modem_unlock_for_ota(void)
{
    if (nb_modem_ota_lock)
    {
        OTA_LOGI("modem ota lock off\r\n");
    }
    nb_modem_ota_lock = 0U;
}

boolean_en nb_modem_locked_by_ota(void)
{
    return nb_modem_ota_lock ? BOOL_TRUE : BOOL_FALSE;
}

void nbSendTcpData_sm(void)
{
    if (nb_modem_ota_lock)
    {
        return;
    }
    switch (pubsend_state)
    {
        case PUBSEDN_STATE_IDLE:
             break;
        case PUBSEDN_STATE_SEND_HEADER:
              sendCommand((uint8*)sendStringBuf3, strlen(sendStringBuf3));
              recvLength = 0;
              pub_timer = Timer_GetTickCount();
              pubsend_state = PUBSEDN_STATE_WAIT_PROMPT;
               break;
        case PUBSEDN_STATE_WAIT_PROMPT:
              if (nb_mqtt_publish_read_prompt() == BOOL_TRUE)
              {
                  pubsend_state = PUBSEDN_STATE_SEND_PAYLOAD;
              }
              else if (Timer_PassedDelay(pub_timer, NB_QMTPUB_PROMPT_TIMEOUT_MS))
              {
                  nb_mqtt_publish_fail("prompt_timeout", BOOL_TRUE);
                  if (zk_ota_is_busy() == BOOL_FALSE)
                  {
                      nb_request_reconnect("PUBLISH_PROMPT_TIMEOUT");
                  }
              }
              break;
        case PUBSEDN_STATE_SEND_PAYLOAD:
              printf("[MQTT] publish payload len=%u\n", (unsigned int)publength);
              usartSendData(pubData, publength);
              recvLength = 0;
              pub_timer = Timer_GetTickCount();
              pubsend_state = PUBSEDN_STATE_WAIT_ACK;
              break;
        case PUBSEDN_STATE_WAIT_ACK:
              if (readLine(stringBuf, &recvLength, 0))
              {
                  if (nb_mqtt_publish_ack_ok(stringBuf) == BOOL_TRUE)
                  {
                      nb_mqtt_pub_success_count++;
                      printf("[MQTT] publish ack pkt=%u ok=%lu\n",
                             (unsigned int)pub_msg_id,
                             nb_mqtt_pub_success_count);
                      recvLength = 0;
                      pubsend_state=PUBSEDN_STATE_SENDFINISH;
                  }
                  else if (nb_mqtt_publish_ack_failed(stringBuf) == BOOL_TRUE)
                  {
                      boolean_en link_lost;

                      link_lost = nb_is_link_lost_line(stringBuf);
                      recvLength = 0;
                      nb_mqtt_publish_fail("ack_error", BOOL_FALSE);
                      if (zk_ota_is_busy() == BOOL_FALSE)
                      {
                          nb_request_reconnect((link_lost == BOOL_TRUE) ?
                                               "PUBLISH_LINK_LOST" :
                                               "PUBLISH_ACK_ERROR");
                      }
                  }
                  else
                  {
                      parseResult(stringBuf);
                      recvLength = 0;
                  }
              }
              else if (Timer_PassedDelay(pub_timer, NB_QMTPUB_ACK_TIMEOUT_MS))
              {
                  nb_mqtt_publish_fail("ack_timeout", BOOL_TRUE);
                  if (zk_ota_is_busy() == BOOL_FALSE)
                  {
                      nb_request_reconnect("PUBLISH_ACK_TIMEOUT");
                  }
              }
              break;
        case  PUBSEDN_STATE_SENDFINISH:
                 pubsend_state=PUBSEDN_STATE_IDLE;//����ط�Ҫ�ص�
              break;
        case PUBSEDN_STATE_FAIL:
              pubsend_state=PUBSEDN_STATE_IDLE;
              break;
        
        default:
              break;
    }
}





/**
*@brief   �첽��ȡsim��ICCID
*@param	  simCardICCIDLength��ICCID����
*@param	  simCardICCID��ICCID����
*@return  1����ȡ�ɹ���0������ʧ��
*/


uint8 geteSimCardICCID(uint8 *simCardICCIDLength, uint8 *simCardICCID) 
{
    //+QCCID: 89860492192071686658
     uint8 *p = 0;
    *simCardICCIDLength = 0;
   // p = sendCommandAndReceiveResponse("AT+QCCID\r\n", 10, "+QCCID", 100, 1);
    send_AT_Command_machine_star("AT+QCCID\r\n",strlen("AT+QCCID\r\n"),"+QCCID",100,1);  //
    if (p == 0) 
    {
       // nbEnterIDLE();
   extern void resetTcpState(void);
       resetTcpState();
        return 0;
    }

    //��λ����һ������
    while (*p != ' ')p++;
    p++;

    //�������ŵ�simCardICCID
    while (*p != '\r') {
        simCardICCID[*simCardICCIDLength] = *p;
        if (++*simCardICCIDLength >= 30)
        {
            *simCardICCIDLength = 0;
            return 0;
        }
        p++;
    }
    return 1;
}

/**
*@brief   ��ȡ�ź�ǿ��
*@return  0���ź�����1���ź��У�2���ź�ǿ
*/
/*
uint8 getSignalQuality(void) {
     uint8 rsrq = 0, rsrp = 0;
     uint8 *p = sendCommandAndReceiveResponse("AT+CESQ\r\n", 9, "+CESQ", 100, 1);
    if (p == 0) {
        nbEnterIDLE();
        return 0;
    }

    while (*p != '\r')p++;//��λ�����һ������
    while (*p != ',')p--;//�˻ص����һ��','
    p--;
    while (*p != ',')p--;//�˻ص������ڶ���','
    p++;

    while (*p != ',') {
        rsrq = rsrq * 10 + *p - '0';
        p++;
    }
    p++;
    while (*p != '\r') {
        rsrp = rsrp * 10 + *p - '0';
        p++;
    }

#ifdef NB_DEBUG_PRINT
    printf("nb:signal:%d,%d\r\n", rsrq, rsrp);
#endif
    if (rsrq >= 26 && rsrp >= 41) {
        return 2;
    } else if (rsrq >= 18 && rsrp >= 31) {
        return 1;
    }
    return 0;
}
*/
/**
*@brief   ���num�Ƿ�Ϊ�����ַ�
*@param	  num���ַ�����
*@return  1�������֣�0����������
*/
/*
static uint8 isNumber(char num) {
    if (num >= '0' && num <= '9') {
        return 1;
    }
    return 0;
}
*/
/**
*@brief   ��ȡ��ǰ����ʱ��
*@param	  sTime��ʱ��
*@param	  sDate������
*@param	  timeZone��ʱ������ 1/4 СʱΪ��λ��ʾ����ʱ��� GMT ֮���ʱ������
*         ��Χ��-47 ~ +48�����磬2019/05/06,22:10:00+8 ��ʾ2019��5��6�ţ�22:10:00GMT+2Сʱ��
*@return  1����ȡ�ɹ���0����ȡʧ��
*/
/*
uint8 getCurrentTime(TimeType *sTime,
                     DateType *sDate,
                     signed char *timeZone) {
     uint8 *p;
     uint8 i = 0;
     signed char timeZoneSign = 1;
     uint8 msgLength;
    if (timeZone == 0)return 0;
    p = sendCommandAndReceiveResponse("AT+CCLK?\r\n", 10, "+CCLK", 100, 1);
    //+CCLK: "2022/05/06,10:01:35+32"
    if (p == 0) {
        nbEnterIDLE();
        return 0;
    }

    msgLength = strlen((char const *) p);


    //�ҵ���һ�����֣���������λ
    while (i < msgLength) {
        if (isNumber(p[i])) {
            break;
        }
        i++;
    }
    if (i >= msgLength) {
        return 0;
    }

    //��
    i += 2;
    if (!isNumber(p[i]) || !isNumber(p[i + 1])) {
        return 0;
    }
    sDate->Year = (p[i] - '0') * 10 + p[i + 1] - '0';
    if (sDate->Year > 99) {
        return 0;
    }
    //��
    i += 2;
    if (p[i] != '/')return 0;
    i++;
    if (!isNumber(p[i]) || !isNumber(p[i + 1])) {
        return 0;
    }
    sDate->Month = (p[i] - '0') * 10 + p[i + 1] - '0';
    if (sDate->Month > 12) {
        return 0;
    }
    //��
    i += 2;
    if (p[i] != '/')return 0;
    i++;
    if (!isNumber(p[i]) || !isNumber(p[i + 1])) {
        return 0;
    }
    sDate->Date = (p[i] - '0') * 10 + p[i + 1] - '0';
    if (sDate->Date > 31) {
        return 0;
    }

    //ʱ
    i += 2;
    if (p[i] != ',')return 0;
    i++;
    if (!isNumber(p[i]) || !isNumber(p[i + 1])) {
        return 0;
    }
    sTime->Hours = (p[i] - '0') * 10 + p[i + 1] - '0';
    if (sTime->Hours > 24) {
        return 0;
    }
    //��
    i += 2;
    if (p[i] != ':')return 0;
    i++;
    if (!isNumber(p[i]) || !isNumber(p[i + 1])) {
        return 0;
    }
    sTime->Minutes = (p[i] - '0') * 10 + p[i + 1] - '0';
    if (sTime->Minutes > 60) {
        return 0;
    }
    //��
    i += 2;
    if (p[i] != ':')return 0;
    i++;
    if (!isNumber(p[i]) || !isNumber(p[i + 1])) {
        return 0;
    }
    sTime->Seconds = (p[i] - '0') * 10 + p[i + 1] - '0';
    if (sTime->Seconds > 60) {
        return 0;
    }

    *timeZone = 0;
    i += 2;
    timeZoneSign = p[i] == '+' ? 1 : -1;
    i++;
    if (i >= msgLength) {
        return 0;
    }

    while (isNumber(p[i])) {
        *timeZone = *timeZone * 10 + p[i] - '0';
        i++;
        if (i >= msgLength) {
            return 0;
        }
    }

    *timeZone *= timeZoneSign;

    return 1;
}*/

/**
*@brief   ���ϱ���URC�ж�ȡ�������·�������
*@param	  pData���������·������ݻ���
*@param	  pLength���������·������ݳ���
*@param	  recvURC��nbģ���ϱ���URC
*@return  1����ȡ�ɹ���0����ȡʧ��
*/

static uint8 nb_payload_is_quoted(char *line_start, char *payload_start)
{
    char *p;

    p = payload_start;
    while (p > line_start)
    {
        --p;
        if (*p == ' ' || *p == '\t')
        {
            continue;
        }
        return (*p == '"') ? 1 : 0;
    }
    return 0;
}

static uint16 nb_unescape_qmtrecv_payload(uint8 *payload, uint16 length)
{
    uint16 src;
    uint16 dst;

    src = 0;
    dst = 0;
    while (src < length)
    {
        if (payload[src] == '\\' && (src + 1) < length)
        {
            if (payload[src + 1] == '"' ||
                payload[src + 1] == '\\' ||
                payload[src + 1] == '/')
            {
                payload[dst++] = payload[src + 1];
                src += 2;
                continue;
            }
        }
        payload[dst++] = payload[src++];
    }
    payload[dst] = 0;
    return dst;
}

static uint8 readTcpData(uint8 *pData, uint16 *pLength, uint8 *recvURC)
{
    char *line_start;
    char *payload_start;
    char *payload_end;
    uint16 len;
    uint8 payload_quoted;

    *pLength = 0;
    line_start = (char *)recvURC;
    payload_start = strchr(line_start, '{');
    if (payload_start == 0)
    {
        return 0;
    }

    payload_end = strrchr(payload_start, '}');
    if (payload_end == 0 || payload_end < payload_start)
    {
        return 0;
    }

    len = (uint16)(payload_end - payload_start + 1);
    if (len >= RECV_BUF_LENGTH)
    {
        return 0;
    }

    payload_quoted = nb_payload_is_quoted(line_start, payload_start);
    memmove(pData, payload_start, len);
    pData[len] = 0;
    if (payload_quoted)
    {
        len = nb_unescape_qmtrecv_payload(pData, len);
    }
    *pLength = len;
    return 1;
}

static boolean_en nb_is_link_lost_line(uint8 *buf)
{
    const char *qmtstat;
    unsigned int client_id;
    int error_code;

    if (buf == 0)
    {
        return BOOL_FALSE;
    }
    if (strstr((const char *)buf, "+QIURC: \"pdpdeact\"") != 0)
    {
        return BOOL_TRUE;
    }
    qmtstat = strstr((const char *)buf, "+QMTSTAT:");
    if (qmtstat != 0 &&
        sscanf(qmtstat, "+QMTSTAT: %u,%d", &client_id, &error_code) == 2 &&
        client_id == 0U &&
        error_code >= 1 &&
        error_code <= 255)
    {
        printf("[MQTT] QMTSTAT disconnect code=%d\r\n", error_code);
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

/**
*@brief   NBģ���ϱ���URC 
*@param	  buf��ģ���ϱ���URC
*@return  ��
*/
static void parseResult(uint8 *buf)                //�������
    {
    uint16 dataLength;

    (void)capture_cereg_from_line(buf);
    if (nb_is_link_lost_line(buf) == BOOL_TRUE)
    {
        nb_request_reconnect((strstr((const char *)buf, "pdpdeact") != 0) ?
                             "PDP_DEACT" :
                             "QMTSTAT");
        return;
    }
    switch (state)
        {
        case NB_STATE_CONNECTING:
            /*
            if (strstr((const char *) buf, "+QIOPEN: 0,0")) {//���ӷ������ɹ�
                state = NB_STATE_CONNECTED;
                reconnectCount = 0;
                onNBEvent(NB_EVENT_CONNECTED, 0, 0);
            } else if (strstr((const char *) buf, "ERROR")) {//���ӷ�����ʧ��
                sendCommandAndReceiveResponse("AT+QICLOSE=0\r\n", 14, "OK", 20, 0);
                if (reconnectCount >= MAX_RECONNECT_COUNT) {                                           
                    resetNbModule();
                    nbEnterIDLE();
                    onNBEvent(NB_EVENT_CONNECT_TIME_OUT, 0, 0);
                }
            }*/
            break;

        case NB_STATE_CONNECTED:
        /*
            if (strstr((const char *) buf, "closed")) {//�����������Ͽ�����
                onNBEvent(NB_EVENT_LOST_CONNECTION, 0, 0);
                nbEnterIDLE();
            } else if (strstr((const char *) buf, "ERROR")) {
                nbEnterIDLE();
            } else */  
        
        
            if (strstr((const char *) buf,"+QMTRECV:"))
            {
                    //+QMTRECV: 0,0,"DL-WDJ","erewre"       "+QIURC: \"recv\""
                    //�յ��������·�����Ϣ
                  //  printf("NB:here3\r\n");
                   //  delayMs(10);//�˴���ʱ�����ȴ���
                    
                   if (readTcpData(stringBuf, &dataLength, buf) > 0)
                     {   //��ȡ�����MQTT�������ݵ� stringBuf  
                        
                              //  printf("NB:recv payload buf ok :%d,%s\r\n", dataLength, stringBuf);
                                extern   void printf_buf2(const char* str, u8* buf, u16 length);
                           //    printf_buf2(stringBuf,stringBuf,dataLength);
                                
                                if(OTA_ENABLE==0)//��OTA�¶�ȡ����������
                                {
                                   recive_flag_MQTT();
                                   onNBEvent(NB_EVENT_DATA, stringBuf, dataLength);//ת�Ƶ�gateway.c�д���  ---------------------------------------- ����ط���JSON���ݣ���Ҫ�������ݴ���
                                }
                            
                        
                    } 
                    else 
                    {

                      //  printf("NB:recv payload buf NG :%d,%x\r\n", dataLength, stringBuf);
                      //   printf("nb:read tcp data fail\r\n");

                     }
                
                
                   /*
                    } else if (strstr((const char *) buf, "SEND OK")) {
                       
                        onNBEvent(NB_EVENT_UPLOAD_COMPLETE, 0, 0);
                    }
                    break;
                   */
            }
            break;
        default:
            break;
    }
}


void SET_NB_STAT_EPOWER_DOWN(void)
{
    state = NB_STATE_POWER_DOWN;  
}





u8   OTA_ENABLE_state=0;

void set_OTA_ENABLE(void)
{
     nb_modem_lock_for_ota();
     OTA_ENABLE_state=1;//�͸������ϱ��Ǳ�֪ͨ��־
    
     _4G_OTA_machine_contextid();//����ģ������,��������·��
    
}
void changea_to_MQTT_modle(void)
{
    nb_modem_unlock_for_ota();
    
    OTA_ENABLE=0;//�ر�OTA���е�MQTT
    OTA_ENABLE_state=0;//�͸������ϱ��Ǳ�֪ͨ��־
    gateway_state=GATEWAY_STATE_CYCLIC_SCAN_AND_REAPORT_DRIVER_DATA;            
    //����OTA����ǰ��MQTT����ָ��
    _4G_configModule_machine_star();
}
boolean_en OTA_ENABLE_IS_SET(void)
{
    
    if(OTA_ENABLE)
    {
        return BOOL_TRUE;
    }
    else
    {
       return BOOL_FALSE;
    }
    
}






/**
*@brief   4Gģ��״̬����ʵ���Զ����ӷ������ͻص��¼�
*@return  ��
*/
void nbModuleProcess(void) 
{     

    switch (state) 
    {
        case NB_STATE_POWER_DOWN:

        if(OTA_ENABLE)
        {
            

        }
        else
        {
           _4G_configModule_machine_star();//ģ������
        }
            state = NB_STATE_CONNECTED;
            break;
 
        case NB_STATE_NOT_CONNECT:     
            /*            
            sendCommandAndReceiveResponse("ATE0\r\n", 6, "OK", 10, 0);
            state = NB_STATE_CONNECTING;
            connectServer();                                                //��������?
            connectingTimer = Timer_GetTickCount();
           */
            break;
        case NB_STATE_CONNECTING:
            /* 
            if (Timer_PassedDelay(connectingTimer, CONNECTING_MAX_WAIT_TIME * 1000)) 
            {
                if (++reconnectCount > MAX_RECONNECT_COUNT)   //30��  10��������
                {
                    reconnectCount = 0;

                    resetNbModule();
                    nbEnterIDLE();
                    onNBEvent(NB_EVENT_CONNECT_TIME_OUT , 0, 0);  
                    break;
                }

                connectServer();//����   //20��������
                connectingTimer = Timer_GetTickCount();
                break;
            }
             */
        case   NB_STATE_CONNECTED:
        case   NB_STATE_IDLE:
            
            /* ���ﱣ�����룬��������ת����
            if (state == NB_STATE_IDLE && Timer_PassedDelay(idleTimer, CONNECTING_MAX_WAIT_TIME * 1000)) 
                {
#ifdef NB_DEBUG_PRINT
                printf("nb:idle time out\r\n");
#endif
                state = NB_STATE_NOT_CONNECT;
            }*/
//printf("NB:here\n"); 
                
               if(OTA_ENABLE)//OTAģʽ��HTTP��       //���յ��ǹ̼��������û����ݰ�
                {
                    if(  mcu_copy_firmware_getdata() ==BOOL_TRUE)
                    {   
                           //  
                         while (readLine_get_firmware(stringBuf, &recvLength,&pack_length)) 
                        { 
                            pack_length=0;
                            printf("0MCU�洢1recvLength=%d\n",recvLength);
                            printf_buf(stringBuf,recvLength);           //  �����Ѿ�����̼���Ϣ�����յ��ǹ̼�����
                            {
                               OTA_STROE_MCU(stringBuf,recvLength-2);   //-2��ȥ��β����\r\n
                            }
                            recvLength = 0; 
                        }
                     }
                }
                else         //MQTTģʽ
                {
                      if (nb_modem_ota_lock)
                      {
                          break;
                      }
                      if(_4G_configModule_machine_finish() ==BOOL_TRUE)
                      {  //ģ���������
                             
                            if (nb_mqtt_publish_owns_uart() == BOOL_TRUE ||
                                nb_at_command_is_busy() == BOOL_TRUE)
                            {
                                break;
                            }
                            while (readLine(stringBuf, &recvLength, 0)) 
                            {
//                                printf("stringBuf\n");
//                                printf("stringBuf=%s\n",stringBuf);
                                parseResult(stringBuf);//������
                                
                                recvLength = 0;
                             }
                      }
               }
              break;
    }//switch����
      
}
