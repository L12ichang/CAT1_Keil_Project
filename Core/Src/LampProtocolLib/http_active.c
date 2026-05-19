#include "cJSON.h"
#include "NbDriver.h"
#include "Portable.h"
#include "hw_gateway.h"
#include "common.h"
#include "sys_data.h"
#include "http_active.h"
#include "app_active.h"

/* 仅保留Quectel HTTP配置状态机，供激活/OTA相关AT流程复用。 */
static  uint16 recvLength = 0;//数据接收长度
static u32 wait_close_http_timer=0;
void app_activate_rx(u8* buf, u16 length);

 http_congfig_state_en  http_congfig_state=HTTP_CONFIG_IDLE;


void http_config_start(void)  //启动http配置
    {
        printf("http_config_start\n");
        http_congfig_state=HTTP_CONFIG_AT_START0;
    }
    
void http_congfig_fsm(void)//整个过程是2.5S
{
  switch(http_congfig_state)
  {
      
        case HTTP_CONFIG_IDLE:
              break;
        
           case HTTP_CONFIG_AT_START0:   
              send_AT_Command_machine_star("AT+QMTCLOSE=0\r\n",strlen("AT+QMTCLOSE=0\r\n"),"OK", 25, 1);//QHTTPCFG: "contextid",1
              http_congfig_state=HTTP_CONFIG_AT_START;
              break;
        case HTTP_CONFIG_AT_START:   
              send_AT_Command_machine_star("AT+QHTTPCFG=contextid\r\n",strlen("AT+QHTTPCFG=contextid\r\n"),"+QHTTPCFG: \"contextid\",1", 25, 1);//QHTTPCFG: "contextid",1
              http_congfig_state=HTTP_CONFIG_AT_QHTTPCFG1;
              break;
        case  HTTP_CONFIG_AT_QHTTPCFG1:         
              if(send_AT_Command_machine_finish()==TRUE)
             {
              send_AT_Command_machine_star("AT+QHTTPCFG=responseheader\r\n",strlen("AT+QHTTPCFG=responseheader\r\n"),"OK", 25, 1);//+QHTTPCFG: "responseheader",0
              http_congfig_state=HTTP_CONFIG_AT_QHTTPCFG2;  
             }  
             break;
       case  HTTP_CONFIG_AT_QHTTPCFG2:         
            if(send_AT_Command_machine_finish()==TRUE)
             {
              send_AT_Command_machine_star("AT+QHTTPCFG=\"contenttype\",4\r\n",strlen("AT+QHTTPCFG=\"contenttype\",4\r\n"),"OK", 25, 1);//配置http消息体类型 content_type:application/json
              http_congfig_state= HTTP_CONFIG_AT_QHTTPURL_POST;  
             }
               break;
       case  HTTP_CONFIG_AT_QHTTPURL_POST:  
            if(send_AT_Command_machine_finish()==TRUE)
            {
                 //并触发激活流程  ‘
                http_enable=BOOL_TRUE;//http激活使能
                printf("http_config_post_state\n");
                wait_close_http_timer=Timer_GetTickCount();
                http_congfig_state= HTTP_CONFIG_AT_WAIT_CLOSE_HTTP; 
                           
            }   
        break;    

      case HTTP_CONFIG_AT_WAIT_CLOSE_HTTP:
             if(Timer_PassedDelay(wait_close_http_timer, 10000))//超时处理
             {
                 
                 //-------------------------------------------------------------------设置超时关闭http
                 _4G_configModule_star_from_onestate( CONNECT_CONFIG_AT_qmtping) ;//重新开始配置
                  http_congfig_state= HTTP_CONFIG_IDLE;
                 break;
             }
          
           if(http_active_ok== BOOL_TRUE)
           {
              http_active_ok= BOOL_FALSE;                                                
              send_AT_Command_machine_star("AT+QHTTPSTOP\r\n",strlen("AT+QHTTPSTOP\r\n"),"OK", 25, 1);//
              http_congfig_state= HTTP_CONFIG_AT_CLOSE_FINISH;     
           }
           break;
      case HTTP_CONFIG_AT_CLOSE_FINISH:
          {
              
          }
           break;
      
  }
  

}



typedef enum
{
    POST_STATE_IDLE,
    POST_STATE_SEND_URL_CMD,
    POST_STATE_SEND_URL,
    POST_STATE_SEND_POST_CMD,
    POST_STATE_SEND_REQ_BODY,
    POST_STATE_QHTTPREAD   , 
    POST_STATE_RXING,
    POST_STATE_HTTPREAD_RESPONSE,
    POST_STATE_HTTPREAD_RESPONSED
    
}http_post_state_en;
static http_post_state_en http_post_state=POST_STATE_IDLE;
    
typedef enum
{
   CMD_STATE_IDLE,
   CMD_STATE_TX
}cmd_state_en;

cmd_state_en cmd_state=CMD_STATE_IDLE;


char* url_strbuff;
char  body_strbuff[1024];
char url_cmd_strbuff[128];
char  post_cmd_strbuff[64];

u8 url_len=0;
u8 body_len=0;
static u32  wait_timer=0;
static u32 http_get_timer;
/************************************
功能描述：http post 请求体
*************************************/
void  http_post(char* urlbuf,char* bodystr)
{
   if( http_congfig_state== HTTP_CONFIG_AT_WAIT_CLOSE_HTTP&&http_post_state==POST_STATE_IDLE )//要在http配置好的情况下进行
   {
        url_strbuff=urlbuf;
        url_len= strlen(url_strbuff);//URL长度
        sprintf(url_cmd_strbuff, "AT+QHTTPURL=%d,30\r\n",url_len );
     
      //  printf("url_cmd_strbuff=%s\n",url_cmd_strbuff);
        memcpy(body_strbuff ,bodystr,strlen(bodystr));
         body_len=strlen(bodystr);
      //   printf("bodystr=%s\n",bodystr);
        sprintf(post_cmd_strbuff, "AT+QHTTPPOST=%d,80,80\r\n",strlen(bodystr) );
        http_post_state=POST_STATE_SEND_URL_CMD;
      
   }

}



        
     //-------------------------------------------主发回调----------------------------------------------------------------------------
  
       
       
 void  http_post_fsm(void)
{
    
    
  switch(http_post_state)
  {
  
      case POST_STATE_IDLE:
          break;
      case POST_STATE_SEND_URL_CMD:
             if(cmd_state==CMD_STATE_IDLE)
             {

       
                          // "CONNECT"
                           printf(" SEND_QHTTPURL\n");
                        memset(stringBuf,0x00,600);//清空内容
                     sendCommand(url_cmd_strbuff, strlen(url_cmd_strbuff)); 
                 
                    wait_timer=Timer_GetTickCount();
                  //  read_counter=0;//重读清零
                     cmd_state=CMD_STATE_TX;
             }        
            break;
      case POST_STATE_SEND_URL:
      
                  if(cmd_state==CMD_STATE_IDLE)
                 {
                      //  "OK"
                        printf(" SEND_URL\n");
                        memset(stringBuf,0x00,600);//清空内容
                      sendCommand(url_strbuff,strlen(url_strbuff) );   
                      wait_timer=Timer_GetTickCount();
                      cmd_state=CMD_STATE_TX;
                 }  
                 break;
      
      case POST_STATE_SEND_POST_CMD:
      
                if(cmd_state==CMD_STATE_IDLE)
                 {
                     // "CONNECT", 25, 1);//
                      printf(" SEND_POST\n");
                       memset(stringBuf,0x00,600);//清空内容
                       sendCommand(post_cmd_strbuff,strlen(post_cmd_strbuff)) ;   
                       wait_timer=Timer_GetTickCount();
                       cmd_state=CMD_STATE_TX;
                 }  
      
 
              break;
      case POST_STATE_SEND_REQ_BODY:
      if(cmd_state==CMD_STATE_IDLE)
                 {
                 // "OK", 25, 1);//
                      printf(" SEND_body\n");
                    memset(stringBuf,0x00,600);//清空内容
                    sendCommand(body_strbuff,strlen(body_strbuff)) ;   
                       wait_timer=Timer_GetTickCount();
                    cmd_state=CMD_STATE_TX; 
                 }  
              break;
      case POST_STATE_QHTTPREAD:

                if(cmd_state==CMD_STATE_IDLE)
                 {
                     
                  memset(stringBuf,0x00,600);//清空内容
                //  "CONNECT", 25, 1);
                 printf(" SEND_QHTTPREAD\n");
                 sendCommand("AT+QHTTPREAD=80\r\n",strlen("AT+QHTTPREAD=80\r\n")) ;   
                  wait_timer=Timer_GetTickCount();
                   cmd_state=CMD_STATE_TX; 
                 }  
                  break;
         
        case POST_STATE_HTTPREAD_RESPONSE:
              if(!Timer_PassedDelay(http_get_timer, 80000))    
               {   
                 while (readLine(stringBuf, &recvLength, 0) &&(!strstr((const char *)stringBuf, "OK"))&&(!strstr((const char *)stringBuf, "+QHTTPREAD: 0")  )  )    //{"token":"c339c4beeb7022c2c01dc31d014ecf01","status":"ok"} 换行   OK 换行    +QHTTPREAD: 0
                    {
                        app_activate_rx(stringBuf,recvLength);
                        recvLength = 0;
                     }
                  http_post_state=POST_STATE_IDLE;
              }
              break;

       
       }
         
         
           //----------------------------------------------全速------------------------------------------------------------------------
  
    if(http_post_state>POST_STATE_IDLE)
     {
               if(Timer_PassedDelay(wait_timer, 20))
               {
                   
                   while (readLine(stringBuf, &recvLength, 0)  ) 
                    {
                       
                        if(http_post_state==POST_STATE_SEND_URL_CMD)
                        {
                           printf("recv OK :%d,%s\n", recvLength, stringBuf);
                          if( strstr((const char *) stringBuf, "CONNECT"))
                           {

                               printf(" http_post_state=POST_STATE_SEND_URL\n");
                               recvLength = 0;
                             
                               cmd_state=CMD_STATE_IDLE; 
                               http_post_state=POST_STATE_SEND_URL;   
                              break;                               
                           }
                             recvLength = 0;
                          

                         }
                        else if(http_post_state==POST_STATE_SEND_URL)
                        {
                          if( strstr((const char *) stringBuf,"OK"))  //"+QHTTPPOST"       "+QHTTPPOST: 0,200"
                           {  
                                printf(" http_post_state=POST_STATE_SEND_POST_CMD\n");
                                recvLength = 0;
                               cmd_state=CMD_STATE_IDLE; 
                                http_post_state=POST_STATE_SEND_POST_CMD;
                                break;                     
                          }
                               recvLength = 0;
                        
                         }
                        else if(http_post_state==POST_STATE_SEND_POST_CMD)
                        {
                         if(  strstr((const char *) stringBuf, "CONNECT"))
                           {  
                                   printf(" CONNECT_WAIT\n");
                               

                            
                                  cmd_state=CMD_STATE_IDLE; 
                                  http_post_state=POST_STATE_SEND_REQ_BODY;
                               printf(" http_post_state=POST_STATE_SEND_REQ_BODY\n");
                               recvLength = 0;
                         
                                 break;   
                           }
                          
                            recvLength = 0;

                        
                         }
                         else if(http_post_state==POST_STATE_SEND_REQ_BODY)
                        {
                         if(  strstr((const char *) stringBuf,"+QHTTPPOST: 0,200"))//
                           {  
                             printf(" http_post_state=POST_STATE_QHTTPREAD\n");
                             recvLength = 0;
                            cmd_state=CMD_STATE_IDLE; 
                            http_post_state=POST_STATE_QHTTPREAD;
                            break;   
                           }
                          
                         //   recvLength = 0;

                        
                         }
                         else if(http_post_state==POST_STATE_QHTTPREAD)
                        {
                          if( strstr((const char *) stringBuf, "CONNECT"))
                           {  
                                
                               recvLength = 0;
                               http_get_timer=Timer_GetTickCount();
                               cmd_state=CMD_STATE_IDLE; 
                              http_post_state=POST_STATE_HTTPREAD_RESPONSE;
                              break;   
                               
                           }
                            recvLength = 0;

                        }
//                        else if(http_post_state==POST_STATE_HTTPREAD_RESPONSED)
//                        {
//                                if(!Timer_PassedDelay(http_get_timer, 80000))    
//                               {   
//                                 while (readLine(stringBuf, &recvLength, 0) )//&&(!strstr((const char *)stringBuf, "OK")&&(!strstr((const char *)stringBuf, "+QHTTPREAD: 0"))
//                                    {
//                                        app_activate_rx(stringBuf,recvLength);
//                                        recvLength = 0;
//                                     }
//                                  http_post_state=POST_STATE_IDLE;
//                              }
//                        }


                     }
                        
                        
                    }
                }

            

       }  
       
       
       



