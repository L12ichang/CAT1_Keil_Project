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
#define NB_QMTOPEN_TIMEOUT_MS 45000UL
#define NB_QMTCONN_TIMEOUT_MS 30000UL
#define NB_QMTSUB_TIMEOUT_MS 30000UL
#define NB_QMTCLOSE_TIMEOUT_MS 5000UL
#define NB_QMTPUB_PROMPT_TIMEOUT_MS 5000UL
#define NB_QMTPUB_ACK_TIMEOUT_MS 30000UL
#define NB_AT_TIMEOUT_COUNT(ms) (((ms) + NB_AT_TICK_MS - 1UL) / NB_AT_TICK_MS)
#define NB_NET_REG_QUERY_INTERVAL_MS 3000UL
#define NB_NET_REG_MAX_WAIT_ATTEMPTS 100U
#define NB_MQTT_SETUP_RETRY_INTERVAL_MS 30000UL
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
static u8 POWERED_DOWN_read_count=0;
CONNECT_CONFIG_state_en connect_state=CONNECT_CONFIG_STATE_IDLE;
static boolean_en imei_ready = BOOL_FALSE;
static boolean_en iccid_ready = BOOL_FALSE;
static boolean_en rsrp_ready = BOOL_FALSE;
static s32 nb_rsrp_dbm10 = 0;
/* 本次QENG查询是否解析到有效RSRP；与rsrp_ready区分:后者为历史最近一次有效值,可保留上报 */
static boolean_en qeng_capture_valid = BOOL_FALSE;
static char nb_net_reg_status[16] = "";
static boolean_en nb_net_reg_ready = BOOL_FALSE;
static u8 nb_net_reg_wait_count = 0;
static boolean_en nb_net_reg_waiting = BOOL_FALSE;
static uint32 nb_net_reg_query_interval_ms = NB_NET_REG_QUERY_INTERVAL_MS;

static void parseResult(uint8 *buf);
static boolean_en nb_is_link_lost_line(uint8 *buf);

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
        case CONNECT_CONFIG_RESETING: return "RESETING";
        case CONNECT_CONFIG_READY: return "READY";
        case CONNECT_CONFIG_AT_CFUN0: return "AT_CFUN0";
        case CONNECT_CONFIG_AT_CFUN1: return "AT_CFUN1";
        case CONNECT_CONFIG_AT_CPIN: return "AT_CPIN";
        case CONNECT_CONFIG_AT_QENG: return "AT_QENG";
        case CONNECT_CONFIG_AT_RECVMODE: return "AT_RECVMODE";
        case CONNECT_CONFIG_AT_VERSION: return "AT_VERSION";
        case CONNECT_CONFIG_AT_keepalive: return "AT_KEEPALIVE";
        case CONNECT_CONFIG_AT_IEMI: return "AT_IEMI";
        case CONNECT_CONFIG_AT_QCCID: return "AT_QCCID";
        case CONNECT_CONFIG_HTTP_ACTIVE: return "HTTP_ACTIVE";
        case CONNECT_CONFIG_WAIT_ACTIVE: return "WAIT_ACTIVE";
        case CONNECT_CONFIG_AT_qmtping: return "AT_QMTPING";
        case CONNECT_CONFIG_WAITING_QMTCLOSE: return "WAITING_QMTCLOSE";
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
    if (strstr(command, "AT+QMTCLOSE") != 0 ||
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

static boolean_en nb_at_command_is_mqtt_setup(void)
{
    if (atcommand == 0)
    {
        return BOOL_FALSE;
    }
    if (strstr((const char *)atcommand, "AT+QMTOPEN") != 0 ||
        strstr((const char *)atcommand, "AT+QMTCONN") != 0 ||
        strstr((const char *)atcommand, "AT+QMTSUB") != 0)
    {
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static u8 nb_at_command_max_attempts(void)
{
    if (nb_at_command_is_mqtt_setup() == BOOL_TRUE)
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
    return BOOL_FALSE;
}

static void nb_net_reg_clear(void)
{
    nb_net_reg_ready = BOOL_FALSE;
    memset(nb_net_reg_status, 0, sizeof(nb_net_reg_status));
}

static boolean_en nb_net_registered_for_mqtt(void)
{
    if (nb_net_reg_ready == BOOL_FALSE)
    {
        return BOOL_FALSE;
    }
    if (strcmp(nb_net_reg_status, "NOCONN") == 0 ||
        strcmp(nb_net_reg_status, "CONNECT") == 0)
    {
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
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

/** 本次QENG是否真正解析到有效RSRP；须与nb_at_command_is_failed()同时判断 */
boolean_en nb_qeng_last_capture_valid(void)
{
    return qeng_capture_valid;
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
    const char *state_start;
    const char *state_end;
    s32 sign;
    s32 value;
    u8 reg_len;

    if (line == 0 ||
        strstr((const char *)line, "+QENG:") == 0 ||
        strstr((const char *)line, "servingcell") == 0)
    {
        return BOOL_FALSE;
    }

    /* 提取 servingcell 后的注册状态字段，存入静态变量供初始化阶段日志使用 */
    p = strstr((const char *)line, "servingcell");
    if (p != 0)
    {
        state_start = strchr(p, ',');
        if (state_start != 0)
        {
            ++state_start;
            while (*state_start == ' ' || *state_start == '\"')
            {
                ++state_start;
            }
            state_end = state_start;
            while (*state_end != '\0' && *state_end != '\"' && *state_end != ',')
            {
                ++state_end;
            }
            reg_len = (u8)(state_end - state_start);
            if (reg_len > 0 && reg_len < (u8)sizeof(nb_net_reg_status))
            {
                memcpy(nb_net_reg_status, state_start, reg_len);
                nb_net_reg_status[reg_len] = '\0';
                nb_net_reg_ready = BOOL_TRUE;
            }
        }
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
            qeng_capture_valid = BOOL_TRUE;
            printf("[QENG] rsrp=%ld.%lddBm\n",
                   (long)(nb_rsrp_dbm10 / 10),
                   (long)((nb_rsrp_dbm10 < 0) ? -(nb_rsrp_dbm10 % 10) : (nb_rsrp_dbm10 % 10)));
            return BOOL_TRUE;
        }
    }
    return BOOL_FALSE;
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
*@brief   检查 UART 是否可用于发送新的 AT 命令
*@return  1：可用；0：AT 机忙或 MQTT 发布占用
*@note    同时满足 AT 命令机空闲 且 MQTT 发布未占用 UART
*/
boolean_en nb_uart_is_available_for_at(void)
{
    if (sendcommad_state != SEND_COMMAND_STATE_IDLE)
    {
        return BOOL_FALSE;
    }
    if (nb_mqtt_publish_owns_uart() == BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

/**
*@brief   检查上一条 AT 命令是否失败(超时/重试耗尽)
*@return  1：失败；0：未失败
*/
boolean_en nb_at_command_is_failed(void)
{
    return sendcommand_failed;
}

/**
*@brief   运行期触发 QENG 查询,用于周期/按需刷新信号
*@return  1：已发送 QENG；0：UART 繁忙,未发送
*@note    不复用连接配置状态机；RSRP 由 capture_rsrp_from_qeng_line()
*         在 AT 命令机 RXING 阶段自动解析更新
*/
boolean_en nb_qeng_trigger_runtime(void)
{
    if (nb_uart_is_available_for_at() == BOOL_FALSE)
    {
        return BOOL_FALSE;
    }
    /* OTA加锁/OTA使能期间禁止插入QENG，避免send_AT_Command_machine_star静默拒绝
       后仍返回TRUE、上层状态机进入WAIT_FINISH却无实际事务 */
    if (nb_modem_locked_by_ota() == BOOL_TRUE ||
        OTA_ENABLE_IS_SET() == BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    /* 每次新查询清零"本次捕获有效"标志，完成时据此区分AT成功与真正取得新RSRP */
    qeng_capture_valid = BOOL_FALSE;
    send_AT_Command_machine_star("at+qeng=\"servingcell\"\r\n",
                                  strlen("at+qeng=\"servingcell\"\r\n"),
                                  "OK", 20, 0);
    printf("[SIG] runtime qeng triggered\r\n");
    return BOOL_TRUE;
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


static void nb_mqtt_recovery_process(void);

void  _4G_configModule_machine_star(void)
{
    clear_imei_data();
    clear_iccid_data();
    nb_net_reg_clear();
    nb_net_reg_wait_count = 0;
    nb_net_reg_waiting = BOOL_FALSE;
    nb_net_reg_query_interval_ms = NB_NET_REG_QUERY_INTERVAL_MS;
    sendcommand_failed = BOOL_FALSE;
    sendcommad_state=SEND_COMMAND_STATE_IDLE;
    connect_state=CONNECT_CONFIG_RESETING;
}

void  _4G_configModule_star_from_onestate(CONNECT_CONFIG_state_en start_state) 
{//��ĳһ��״̬��ʼ����MQTT
    connect_state=start_state;
    sendcommand_failed = BOOL_FALSE;
    sendcommad_state=SEND_COMMAND_STATE_RXING_COMPLETE;
}
  
boolean_en  _4G_configModule_machine_finish(void)
{
   if( connect_state==CONNECT_CONFIG__COMPLETE)
    {
        return BOOL_TRUE;
    }
    else
    {
    return BOOL_FALSE; 
      }  
}

static boolean_en nb_config_step_is_mqtt_setup(const char *step)
{
    if (step == 0)
    {
        return BOOL_FALSE;
    }
    if (strcmp(step, "QMTOPEN") == 0 ||
        strcmp(step, "QMTCONN") == 0 ||
        strcmp(step, "QMTSUB_DOWN") == 0 ||
        strcmp(step, "QMTSUB_UP") == 0)
    {
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static void nb_config_enter_mqtt_retry_wait(const char *step)
{
    printf("[NB][E] config step=%s failed, wait net %lus before mqtt retry\n",
           (step != 0) ? step : connect_state_name(connect_state),
           (unsigned long)(NB_MQTT_SETUP_RETRY_INTERVAL_MS / 1000UL));
    sendcommand_failed = BOOL_FALSE;
    sendcommad_state=SEND_COMMAND_STATE_IDLE;
    nb_net_reg_clear();
    nb_net_reg_wait_count = 0;
    nb_net_reg_query_interval_ms = NB_MQTT_SETUP_RETRY_INTERVAL_MS;
    wait_timer = Timer_GetTickCount();
    nb_net_reg_waiting = BOOL_TRUE;
    connect_state=CONNECT_CONFIG_AT_QENG;
}

static boolean_en nb_config_step_failed_recover(const char *step)
{
    if (send_AT_Command_machine_failed() == BOOL_FALSE)
    {
        return BOOL_FALSE;
    }
    if (nb_config_step_is_mqtt_setup(step) == BOOL_TRUE)
    {
        printf("[NB][E] config step=%s failed, close mqtt before retry\n",
               (step != 0) ? step : connect_state_name(connect_state));
        sendcommand_failed = BOOL_FALSE;
        send_AT_Command_machine_star("AT+QMTCLOSE=0\r\n",
                                     strlen("AT+QMTCLOSE=0\r\n"),
                                     "OK",
                                     NB_AT_TIMEOUT_COUNT(NB_QMTCLOSE_TIMEOUT_MS),
                                     1);
        connect_state=CONNECT_CONFIG_WAITING_QMTCLOSE;
        return BOOL_TRUE;
    }
    printf("[NB][E] config step=%s failed, restart mqtt init\n",
           (step != 0) ? step : connect_state_name(connect_state));
    _4G_configModule_machine_star();
    return BOOL_TRUE;
}

void _4G_configModule_machine(void)
{
    if (nb_modem_ota_lock)
    {
        return;
    }
    /* MQTT恢复管理器：单轮180s超时检查（内部自带OTA保护，active时推进下一轮） */
    nb_mqtt_recovery_process();
    nb_trace_state_change();
    switch(connect_state)
    {
        case CONNECT_CONFIG_STATE_IDLE:
        break;
        case CONNECT_CONFIG_RESETING:
           resetNbModule();//ģ�鸴λ
           connect_state=CONNECT_CONFIG_READY;
        break;
        case CONNECT_CONFIG_READY:                 
             if( _4g_reset_finish()==BOOL_TRUE)
             {//ģ�鸴λ�ɹ���ʼ��ATָ��
                recvLength = 0;
                  // printf_buf(stringBuf,72);
                if (readLine(stringBuf, &recvLength, 0))
                {   
                     printf_buf(stringBuf,recvLength);
                    if( strstr((const char *) stringBuf, "POWERED DOWN"))
                    {
                         printf("POWERED DOWN");
                         connect_state=CONNECT_CONFIG_RESETING;
                         _4g_reset_idle() ;//��λ�ź���ɺ����idle
                         printf("__________________ģ�����ϵ�___________________\n");
                         break;
                    }
                    else if( strstr((const char *) stringBuf, "RDY"))
                    {
                        printf("POWERED RDY");
                        send_AT_Command_machine_star("AT+CFUN=0\r\n", 11, "OK", 50, 0) ;
                        connect_state=CONNECT_CONFIG_AT_CFUN0;
                    }
               
                    else
                    {//δ����
                            printf_buf(stringBuf,recvLength);
                           if(POWERED_DOWN_read_count<2) 
                             {    //�ض�                   
                                 connect_state= CONNECT_CONFIG_READY;
                                 break;
                             }
                             else
                             {//ֱ������ȥ
                                 
                                 POWERED_DOWN_read_count=0;
                             }
                    
                    }
                 }
                     send_AT_Command_machine_star("AT+CFUN=0\r\n", 11, "OK", 50, 0) ;
                     connect_state=CONNECT_CONFIG_AT_CFUN0;
              }
       
       break;
       case CONNECT_CONFIG_AT_CFUN0:
             if(send_AT_Command_machine_finish()==TRUE)
             {
              if (nb_config_step_failed_recover("CFUN0") == BOOL_TRUE)
              {
                  break;
              }
              send_AT_Command_machine_star("AT+CFUN=1\r\n", 11, "OK", 50, 0);
              connect_state=CONNECT_CONFIG_AT_CFUN1;
             }
       break;   
       case CONNECT_CONFIG_AT_CFUN1:
             if(send_AT_Command_machine_finish()==TRUE)
             {
             if (nb_config_step_failed_recover("CFUN1") == BOOL_TRUE)
             {
                 break;
             }
             send_AT_Command_machine_star("AT+CPIN?\r\n", strlen("AT+CPIN?\r\n"), "+CPIN: READY",20, 0);
        
              connect_state=CONNECT_CONFIG_AT_CPIN;   
             }                 
      break;  
      case CONNECT_CONFIG_AT_CPIN:
             if(send_AT_Command_machine_finish()==TRUE)
             {
               if (nb_config_step_failed_recover("CPIN") == BOOL_TRUE)
               {
                   break;
               }
               nb_net_reg_clear();
               nb_net_reg_wait_count = 0;
               nb_net_reg_waiting = BOOL_FALSE;
               nb_net_reg_query_interval_ms = NB_NET_REG_QUERY_INTERVAL_MS;
               send_AT_Command_machine_star("at+qeng=\"servingcell\"\r\n", strlen("at+qeng=\"servingcell\"\r\n"), "OK", 20, 0);
        
              connect_state=CONNECT_CONFIG_AT_QENG;   
             }                 
      break;                         
      case CONNECT_CONFIG_AT_QENG:
             if (nb_net_reg_waiting == BOOL_TRUE)
             {
              if (Timer_PassedDelay(wait_timer, nb_net_reg_query_interval_ms) == BOOL_TRUE)
              {
                  nb_net_reg_waiting = BOOL_FALSE;
                  nb_net_reg_query_interval_ms = NB_NET_REG_QUERY_INTERVAL_MS;
                  nb_net_reg_clear();
                  send_AT_Command_machine_star("at+qeng=\"servingcell\"\r\n", strlen("at+qeng=\"servingcell\"\r\n"), "OK", 20, 0);
              }
             }
             else
             {
             if(send_AT_Command_machine_finish()==TRUE)
             {
              if (nb_config_step_failed_recover("QENG") == BOOL_TRUE)
              {
                  break;
              }
              if (nb_net_reg_ready == BOOL_TRUE)
              {
                  printf("[NET] reg=%s\n", nb_net_reg_status);
              }
              else
              {
                  printf("[NET] reg=UNKNOWN\n");
              }
              if (nb_net_registered_for_mqtt() == BOOL_FALSE)
              {
                  ++nb_net_reg_wait_count;
                  printf("[NET] wait mqtt reg=%s attempt=%u/%u interval=%lus\n",
                         (nb_net_reg_ready == BOOL_TRUE) ? nb_net_reg_status : "UNKNOWN",
                         (unsigned int)nb_net_reg_wait_count,
                         (unsigned int)NB_NET_REG_MAX_WAIT_ATTEMPTS,
                         (unsigned long)(nb_net_reg_query_interval_ms / 1000UL));
                  if (nb_net_reg_wait_count >= NB_NET_REG_MAX_WAIT_ATTEMPTS)
                  {
                      printf("[NET][E] mqtt registration timeout, restart modem\n");
                      _4G_configModule_machine_star();
                      break;
                  }
                  nb_net_reg_query_interval_ms = NB_NET_REG_QUERY_INTERVAL_MS;
                  wait_timer = Timer_GetTickCount();
                  nb_net_reg_waiting = BOOL_TRUE;
                  connect_state=CONNECT_CONFIG_AT_QENG;
                  break;
              }
              nb_net_reg_wait_count = 0;
              nb_net_reg_waiting = BOOL_FALSE;
              send_AT_Command_machine_star("AT+QMTCFG=\"recv/mode\",0,0,0\r\n",strlen("AT+QMTCFG=\"recv/mode\",0,0,0\r\n"),"OK",20, 1);
              connect_state=CONNECT_CONFIG_AT_RECVMODE;
             }
             }
       break;

       case CONNECT_CONFIG_AT_RECVMODE:
             if(send_AT_Command_machine_finish()==TRUE)
             {
              if (nb_config_step_failed_recover("RECVMODE") == BOOL_TRUE)
              {
                  break;
              }
              send_AT_Command_machine_star("AT+QMTCFG=\"version\",0,4\r\n",strlen("AT+QMTCFG=\"version\",0,4\r\n"),"OK",25, 1);
              connect_state=CONNECT_CONFIG_AT_VERSION;
             }
        break;
        case CONNECT_CONFIG_AT_VERSION:
             if(send_AT_Command_machine_finish()==TRUE)
             {
              if (nb_config_step_failed_recover("VERSION") == BOOL_TRUE)
              {
                  break;
              }
              send_AT_Command_machine_star("AT+QMTCFG=\"keepalive\",0,120\r\n",strlen("AT+QMTCFG=\"keepalive\",0,120\r\n"),"OK",25, 1);
              connect_state=CONNECT_CONFIG_AT_keepalive;
             }
        break;
        case CONNECT_CONFIG_AT_keepalive:
             if(send_AT_Command_machine_finish()==TRUE)
             {
              if (nb_config_step_failed_recover("KEEPALIVE") == BOOL_TRUE)
              {
                  break;
              }
              send_AT_Command_machine_star("AT+QMTCFG=\"qmtping\",0,30\r\n",strlen("AT+QMTCFG=\"qmtping\",0,30\r\n"),"OK", 25, 1);
              connect_state=CONNECT_CONFIG_AT_IEMI;
             }
      break;
      case   CONNECT_CONFIG_AT_IEMI:
           if(send_AT_Command_machine_finish()==TRUE)
             {
                 if (nb_config_step_failed_recover("QMTPING") == BOOL_TRUE)
                 {
                     break;
                 }
                 send_AT_Command_machine_star("AT+CGSN\r\n",strlen("AT+CGSN\r\n"),"OK", 50, 1);
              connect_state=CONNECT_CONFIG_AT_qmtping;
             }
      break;
      case   CONNECT_CONFIG_AT_QCCID:
           if(send_AT_Command_machine_finish()==TRUE)
             {
                 static char sendStringBufSub[96];
                 int cmd_len;
                 zk_device_config_refresh_iccid();
                 cmd_len = zk_build_qmt_sub_cmd(sendStringBufSub, sizeof(sendStringBufSub));
                 if (cmd_len <= 0 || cmd_len >= (int)sizeof(sendStringBufSub))
                 {
                     _4G_configModule_machine_star();
                     break;
                 }
                 send_AT_Command_machine_star(sendStringBufSub,(uint8)cmd_len,"+QMTSUB: 0,1,0", NB_AT_TIMEOUT_COUNT(NB_QMTSUB_TIMEOUT_MS), 1);
                 connect_state=CONNECT_CONFIG_AT_QMTSUB;
             }
       break;

       case CONNECT_CONFIG_HTTP_ACTIVE:
       break;
       case CONNECT_CONFIG_WAIT_ACTIVE:
       break;

       case CONNECT_CONFIG_AT_qmtping:
             if(send_AT_Command_machine_finish()==TRUE)
             {
              static char sendStringBufOpen[96];
              int cmd_len;
              if (nb_config_step_failed_recover("CGSN") == BOOL_TRUE)
              {
                  break;
              }
              if (zk_mqtt_init() == BOOL_FALSE)
              {
                  printf("MQTT config blocked: invalid real IMEI\n");
                  _4G_configModule_machine_star();
                  break;
              }
              cmd_len = zk_build_qmt_open_cmd(sendStringBufOpen, sizeof(sendStringBufOpen));
              if (cmd_len <= 0 || cmd_len >= (int)sizeof(sendStringBufOpen))
              {
                  _4G_configModule_machine_star();
                  break;
              }
              send_AT_Command_machine_star(sendStringBufOpen,(uint8)cmd_len, "+QMTOPEN: 0,0", NB_AT_TIMEOUT_COUNT(NB_QMTOPEN_TIMEOUT_MS), 1);
              connect_state=CONNECT_CONFIG_AT_IPPORT;
             }
       break;
       case CONNECT_CONFIG_WAITING_QMTCLOSE:
             if(send_AT_Command_machine_finish()==TRUE)
             {
              nb_config_enter_mqtt_retry_wait("QMTCLOSE");
             }
       break;
       case CONNECT_CONFIG_AT_IPPORT:
             if(send_AT_Command_machine_finish()==TRUE)
              {
              static char sendStringBufConn[128];
              int cmd_len;
              if (nb_config_step_failed_recover("QMTOPEN") == BOOL_TRUE)
              {
                  break;
              }
              cmd_len = zk_build_qmt_conn_cmd(sendStringBufConn, sizeof(sendStringBufConn));
              if (cmd_len <= 0 || cmd_len >= (int)sizeof(sendStringBufConn))
              {
                  _4G_configModule_machine_star();
                  break;
              }
              send_AT_Command_machine_star(sendStringBufConn,(uint8)cmd_len,"+QMTCONN: 0,0,0", NB_AT_TIMEOUT_COUNT(NB_QMTCONN_TIMEOUT_MS), 1);
              connect_state=CONNECT_CONFIG_AT_QMTCONN;
              }
        break;
        case CONNECT_CONFIG_AT_QMTCONN:
             if(send_AT_Command_machine_finish()==TRUE)
             {
                 if (nb_config_step_failed_recover("QMTCONN") == BOOL_TRUE)
                 {
                     break;
                 }
                 send_AT_Command_machine_star("AT+QCCID\r\n",strlen("AT+QCCID\r\n"),"+QCCID:", 25, 1);
                 connect_state=CONNECT_CONFIG_AT_QCCID;
             }
       break;
       case CONNECT_CONFIG_AT_QMTSUB:
             if(send_AT_Command_machine_finish()==TRUE)
             {
                 static char sendStringBufSubUp[96];
                 int cmd_len;
                 if (nb_config_step_failed_recover("QMTSUB_DOWN") == BOOL_TRUE)
                 {
                     break;
                 }
                 cmd_len = zk_build_qmt_sub_upgrade_cmd(sendStringBufSubUp, sizeof(sendStringBufSubUp));
                 if (cmd_len <= 0 || cmd_len >= (int)sizeof(sendStringBufSubUp))
                 {
                     _4G_configModule_machine_star();
                     break;
                 }
                 send_AT_Command_machine_star(sendStringBufSubUp,(uint8)cmd_len,"+QMTSUB: 0,2,0", NB_AT_TIMEOUT_COUNT(NB_QMTSUB_TIMEOUT_MS), 1);
                 connect_state=CONNECT_CONFIG_AT_LAST;
             }
       break;
       case CONNECT_CONFIG_AT_LAST:
             if(send_AT_Command_machine_finish()==TRUE)
             {
              if (nb_config_step_failed_recover("QMTSUB_UP") == BOOL_TRUE)
              {
                  break;
              }
              send_AT_Command_machine_idle();
              connect_state=CONNECT_CONFIG__COMPLETE;
              state = NB_STATE_CONNECTED;
              onNBEvent(NB_EVENT_CONNECTED, 0, 0);
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

/* ===================== MQTT假在线分级自愈：4G恢复管理器 ===================== */
#define NB_MQTT_RECOVERY_MAX_ATTEMPTS        3U
#define NB_MQTT_RECOVERY_ATTEMPT_TIMEOUT_MS  (180UL * 1000UL)

static boolean_en nb_mqtt_recovery_active = BOOL_FALSE;
static u8 nb_mqtt_recovery_attempt_count = 0;
static u32 nb_mqtt_recovery_attempt_tick = 0;
static u32 nb_mqtt_recovery_start_count = 0;
static u32 nb_mqtt_recovery_success_count = 0;
static u32 nb_mqtt_recovery_fail_count = 0;

/* 单轮恢复超时检查：非阻塞，供_4G_configModule_machine周期调用 */
static void nb_mqtt_recovery_process(void)
{
    if (nb_mqtt_recovery_active != BOOL_TRUE)
    {
        return;
    }
    /* OTA独占4G链路期间暂停恢复超时计时，避免把OTA耗时误算为恢复失败 */
    if (nb_modem_locked_by_ota() == BOOL_TRUE || OTA_ENABLE_IS_SET() == BOOL_TRUE)
    {
        return;
    }
    if (Timer_PassedDelay(nb_mqtt_recovery_attempt_tick,
                          NB_MQTT_RECOVERY_ATTEMPT_TIMEOUT_MS) != BOOL_TRUE)
    {
        return;
    }
    nb_mqtt_recovery_fail_count++;
    printf("[MQTT][RECOVERY][E] attempt=%u timeout\r\n",
           (unsigned int)nb_mqtt_recovery_attempt_count);
    if (nb_mqtt_recovery_attempt_count >= NB_MQTT_RECOVERY_MAX_ATTEMPTS)
    {
        /* 网络故障不再复位MCU，避免打断PWM等本地业务；重新进入一轮4G恢复 */
        printf("[MQTT][RECOVERY][W] modem recovery failed %u times, continue retrying\r\n",
               (unsigned int)NB_MQTT_RECOVERY_MAX_ATTEMPTS);
        nb_mqtt_recovery_attempt_count = 1;
    }
    else
    {
        nb_mqtt_recovery_attempt_count++;
    }
    nb_mqtt_recovery_attempt_tick = Timer_GetTickCount();
    printf("[MQTT][RECOVERY] attempt=%u/%u\r\n",
           (unsigned int)nb_mqtt_recovery_attempt_count,
           (unsigned int)NB_MQTT_RECOVERY_MAX_ATTEMPTS);
    _4G_configModule_machine_star();
}

void nb_mqtt_recovery_start(const char *reason)
{
    (void)reason;
    if (nb_modem_locked_by_ota() == BOOL_TRUE || OTA_ENABLE_IS_SET() == BOOL_TRUE)
    {
        return;
    }
    if (nb_mqtt_recovery_active == BOOL_TRUE)
    {
        return;  /* 已有恢复任务进行中，禁止并发/重复启动 */
    }
    nb_mqtt_recovery_active = BOOL_TRUE;
    nb_mqtt_recovery_attempt_count = 1;
    nb_mqtt_recovery_attempt_tick = Timer_GetTickCount();
    nb_mqtt_recovery_start_count++;
    printf("[MQTT][RECOVERY] attempt=%u/%u\r\n",
           (unsigned int)nb_mqtt_recovery_attempt_count,
           (unsigned int)NB_MQTT_RECOVERY_MAX_ATTEMPTS);
    _4G_configModule_machine_star();
}

void nb_mqtt_recovery_mark_transport_success(void)
{
    if (nb_mqtt_recovery_active != BOOL_TRUE)
    {
        return;
    }
    nb_mqtt_recovery_active = BOOL_FALSE;
    nb_mqtt_recovery_attempt_count = 0;
    nb_mqtt_recovery_attempt_tick = 0;
    nb_mqtt_recovery_success_count++;
    printf("[MQTT][RECOVERY] transport restored\r\n");
}

boolean_en nb_mqtt_recovery_is_active(void)
{
    return nb_mqtt_recovery_active;
}

u8 nb_mqtt_recovery_get_attempt_count(void)
{
    return nb_mqtt_recovery_attempt_count;
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
                      recvLength = 0;
                      nb_mqtt_publish_fail("ack_error", BOOL_FALSE);
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
     uint8 *p;
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
    if (strstr((const char *)buf, "+QIURC: \"pdpdeact\"") ||
        strstr((const char *)buf, "+QMTSTAT: 0,1") ||
        strstr((const char *)buf, "+QMTSTAT: 0,2") ||
        strstr((const char *)buf, "+QMTSTAT: 0,3"))
    {
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
        
        
            if (nb_is_link_lost_line(buf))
            {
                extern void resetTcpState(void);
                onNBEvent(NB_EVENT_LOST_CONNECTION, 0, 0);
                resetTcpState();
                /* 明确断线URC(+QMTSTAT/pdpdeact)：触发4G恢复管理器。
                   否则login_state被置IDLE后，心跳监督与recovery均不运行(非ONLINE)，
                   tcpConnectState=NOT_CONNECTED又切断session_process，设备将永久假离线。 */
                nb_mqtt_recovery_start("urc_link_lost");
            }
            else if (strstr((const char *) buf,"+QMTRECV:"))
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
    _4G_configModule_star_from_onestate( CONNECT_CONFIG_AT_qmtping) ;    //��ĳһ��״̬��ʼ����,����MQTT����ָ��  
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
                      if(_4G_configModule_machine_finish() ==BOOL_TRUE)
                      {  //ģ���������
                             
                            if (nb_mqtt_publish_owns_uart() == BOOL_TRUE)
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
