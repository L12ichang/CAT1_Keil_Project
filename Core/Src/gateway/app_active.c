#include "app_active.h"

#include "cJSON.h"
#include "NbDriver.h"
#include "Portable.h"
#include "hw_gateway.h"
#include "common.h"
#include "sys_data.h"
#include "http_active.h"
#define APP_ACTIVE_LEGACY_IMPLEMENTATION
/* 旧HTTP激活主流程已停用；保留timer/状态标志供CAT.1状态机兼容调用。 */
#define  GET_TOKEN_CYCLE            (u32)25920000  //72小时


typedef enum
{
    ACTIVATE_STATE_IDLE,
    ACTIVATE_STATE_TX,
    ACTIVATE_STATE_NONE

}activate_state_en;
activate_state_en activate_state = ACTIVATE_STATE_IDLE;
typedef enum
{
    ACTIVATE_CMD_NONE=0,
    ACTIVATE_CMD_ACTIVATE,
    ACTIVATE_CMD_ACTIVATE_ACK,
    ACTIVATE_CMD_TOKEN,
 
}activate_cmd_en;


static activate_cmd_en activate_cmd=ACTIVATE_CMD_NONE;
cJSON *json_root = NULL;
cJSON *json_root_rx = NULL;
boolean_en has_new_token = BOOL_FALSE;
//u8 temp_buffer[1024];  //临时缓存
char temp_str[1024];  //临时缓存
char openid[33];
char token[33];
static char str[128];
static u16 _timer_for_retry=0;
static u32 _timer_for_token=0;
static  uint16 recvLength = 0;//数据接收长度
boolean_en http_active_ok= BOOL_FALSE;
boolean_en http_enable= BOOL_FALSE;






void app_activate_timer(void)
{
    if(_timer_for_retry > 0)
    {
        --_timer_for_retry;
        if(_timer_for_retry == 0)
        {
            activate_state = ACTIVATE_STATE_IDLE;
        }
    }
    if(_timer_for_token > 0)
    {
        --_timer_for_token;
    }
} 

void  app_mqtt_reconnect(void)
{
    

    http_congfig_state=HTTP_CONFIG_IDLE;
    activate_cmd=ACTIVATE_CMD_NONE;
    activate_state = ACTIVATE_STATE_IDLE;   
    _4G_configModule_machine_star();

}

/************************************
功能描述：http响应体接收
httpclient回调， 分批从buf传过来
length: 单次的长度，
offset：全局缓冲的开始偏移地址
data_end：最后一帧数据
*************************************/
void app_activate_rx(u8* buf, u16 length)
{
    cJSON *tmp = NULL;
    printf("length=%d,rx=%s\n", length,buf);
//    if(str)
//    {
//        cJSON_free(str);
//        str = 0;
//    }
    json_root_rx=cJSON_Parse((const char *)buf);
    tmp=cJSON_GetObjectItem(json_root_rx,"status");
    if(tmp != NULL)
    { printf("status\n");
      printf("activate_cmd=%02x\n",activate_cmd); 
       void printf_buf(u8* buf, u16 length);
    //   printf_buf(sys_data.openid, 33);
        if(activate_cmd == ACTIVATE_CMD_ACTIVATE)
        {printf("status2\n");
            if(strcmp(tmp->valuestring, "ok") == 0)
            {//printf("status3\n");
                tmp=cJSON_GetObjectItem(json_root_rx,"openid");            
                if(tmp != NULL)
                {
                    strcpy(sys_data.openid, tmp->valuestring);
                    printf("openid=%s\r\n",tmp->valuestring);  
                    activate_cmd = ACTIVATE_CMD_ACTIVATE_ACK;
                    _timer_for_retry = 0;
                    activate_state = ACTIVATE_STATE_IDLE;     
                    sys_data_store();
                }
            }
        }
        else if(activate_cmd == ACTIVATE_CMD_ACTIVATE_ACK)
        {
            if(strcmp(tmp->valuestring, "ok") == 0)
            {
                activate_cmd = ACTIVATE_CMD_TOKEN;
                _timer_for_retry = 0;
                activate_state = ACTIVATE_STATE_IDLE;
                printf("openid ack\n");
            }
        }
        else if(activate_cmd == ACTIVATE_CMD_TOKEN)
        {            
            if(strcmp(tmp->valuestring, "ok") == 0)
            {
                tmp=cJSON_GetObjectItem(json_root_rx,"token");            
                if(tmp != NULL)
                {
                    strcpy(sys_data.token, tmp->valuestring);
                    sys_data_store();
                    printf("token=%s\r\n",tmp->valuestring);  
                    activate_cmd = ACTIVATE_CMD_NONE;
                    _timer_for_retry = 0;
                    _timer_for_token = GET_TOKEN_CYCLE;
                    activate_state = ACTIVATE_STATE_IDLE;  
                    
                    if(has_new_token == BOOL_FALSE)
                    {
                       has_new_token = BOOL_TRUE;
                       http_active_ok= BOOL_TRUE;//激活完毕--------------------------------------------待重审
                       http_enable=BOOL_FALSE;//http 关闭
                    }
                    else
                    {
                       app_mqtt_reconnect();
                    }
                }
            }
            else if(strcmp(tmp->valuestring, "openid unlock") == 0)
            {
                activate_cmd = ACTIVATE_CMD_ACTIVATE_ACK;
                _timer_for_retry = 100;
                activate_state = ACTIVATE_STATE_IDLE;                    
            }
        }
        
    }
    cJSON_Delete(json_root_rx);   
}

void app_activate_process(void)
{
    char* json_str;
    boolean_en strlen_error = BOOL_FALSE;
    static u8 active_counter=0;
    if(http_enable )//&& http_rx_ready == BOOL_TRUE
    {
        if(sys_data.openid[0] ==0 ||(u8)sys_data.openid[0] == 0xff && (u8)sys_data.openid[1] == 0xff)
        {

            if(activate_state == ACTIVATE_STATE_IDLE)
            {
                if(_timer_for_retry == 0)
                {
                     
                      printf("sys_data.openid=%s\n",(char*)sys_data.openid);
                    json_root = cJSON_CreateObject();
                    cJSON_AddStringToObject(json_root, "type","CAT.1");
                    cJSON_AddStringToObject(json_root, "id",(char*)IMEI10 );       //(char*)IMEI    "9359528"  (char*)IMEI10
                    json_str = cJSON_Print(json_root);
                    if(strlen(json_str)<sizeof(str))
                    {
                        strcpy(str, json_str);
                    }
                    else
                    {
                        strlen_error = BOOL_TRUE;
                        printf("strlen_error3\n");
                    }
                    cJSON_free(json_str);
                    cJSON_Delete(json_root);                    
                    temp_str[0]=0;
                    strcat(temp_str, "http://8.130.79.31:8000");
                    strcat(temp_str, "/smart_lighting?cmd_code=activate");
                    if(strlen_error == BOOL_FALSE)
                    {
                        printf("http_post1\n");
                        activate_cmd = ACTIVATE_CMD_ACTIVATE;
                        activate_state = ACTIVATE_STATE_TX;
                        http_post(temp_str, str);//   http_post("http://8.130.79.31:8000/smart_lighting?cmd_code=activate","{"type":"CAT.1","id":"9359528"}\r\n");
                     
                    }
                    _timer_for_retry = 1000;
//                    activate_cmd = ACTIVATE_CMD_ACTIVATE;
//                    activate_state = ACTIVATE_STATE_TX;
                }
            }
        }
        else
        {
            if(activate_cmd == ACTIVATE_CMD_ACTIVATE_ACK)
            {
                if(activate_state == ACTIVATE_STATE_IDLE)
                {
                    if(_timer_for_retry == 0)
                    {
                        json_root = cJSON_CreateObject();
                        cJSON_AddStringToObject(json_root, "openid", sys_data.openid);
                        json_str = cJSON_Print(json_root);
                        if(strlen(json_str)<sizeof(str))
                        {
                            strcpy(str, json_str);
                        }
                        else
                        {
                            strlen_error = BOOL_TRUE;
                            printf("strlen_error\n");
                        }
                        cJSON_free(json_str);
                        cJSON_Delete(json_root);
                        temp_str[0]=0;
                        strcat(temp_str,"http://8.130.79.31:8000");
                        strcat(temp_str, "/smart_lighting?cmd_code=activate_ack");
                        if(strlen_error == BOOL_FALSE)
                        {    printf("http_post2\n");
                            http_post(temp_str, str);//由NB发送和接收？
                        }
                        _timer_for_retry = 1000;
                        activate_state = ACTIVATE_STATE_TX;
                    }
                }
            }            
            else if(activate_cmd == ACTIVATE_CMD_TOKEN)
            {
                if(activate_state == ACTIVATE_STATE_IDLE)
                {
                    if(_timer_for_retry == 0)
                    {
                        
                          printf(  "%s",(char*)sys_data.openid  );
                        
                       
                        json_root = cJSON_CreateObject();
                        cJSON_AddStringToObject(json_root, "openid", sys_data.openid);  // "687b6ca685b607b4"
                        cJSON_AddStringToObject(json_root, "relative_path", "CAT.1");
                        json_str = cJSON_Print(json_root);
                        printf("json_str=%s\n",json_str);
                        
                        if(strlen(json_str)<sizeof(str))
                        {
                           strcpy(str, json_str);
                        }
                        else
                        {
                            strlen_error = BOOL_TRUE;
                            printf("strlen_error2\n");
                        }
                        cJSON_free(json_str);
                        cJSON_Delete(json_root);
                        temp_str[0]=0;
                        strcat(temp_str, "http://8.130.79.31:8000");
                        strcat(temp_str, "/smart_lighting?cmd_code=token");
                        if(strlen_error == BOOL_FALSE)
                        {    printf("http_post3\n");
                            http_post(temp_str, str); // str={"openid":"687b6ca685b607b4","relative_path":"CAT.1"};
                        }
                        _timer_for_retry = 1000;
                        
                        activate_state = ACTIVATE_STATE_TX;
                    }
                }
            }
            else
            {
                if(_timer_for_token == 0)
                {
                    _timer_for_retry = 0;
                    activate_cmd = ACTIVATE_CMD_TOKEN;
                    activate_state = ACTIVATE_STATE_IDLE;
                }
            }
        }
    }
}



void app_activate_init(void)
{
    activate_state = ACTIVATE_STATE_IDLE;
    if((u8)sys_data.openid[0] != 0xff && (u8)sys_data.openid[1] != 0xff)//sys_data.openid[0] != 0||
    {
        activate_cmd = ACTIVATE_CMD_TOKEN;
    }
    else
    {
        activate_cmd = ACTIVATE_CMD_NONE;
    }
}




