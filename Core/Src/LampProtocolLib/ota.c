/*************************************************************
³ÌÐò¹¦ÄÜ£ºCAT.1ÖÇ»ÛµçÔ´OTA¹Ì¼þ¸üÐÂ
¿ª·¢»·¾³£ºkeil 5.37
Ð¾Æ¬ÐÍºÅ£ºSTM32F103CBT6/HK32F103CCT6A
¿ª·¢ÈËÔ±£ºÍõµÀ¾ü
µ¥Î»Ãû³Æ£º¹ã¶«¶«ÁâµçÔ´¿Æ¼¼ÓÐÏÞ¹«Ë¾
±à¼­ÈÕÆÚ£º2024.6.4
*************************************************************/
#include "ota.h"
#include "NbDriver.h"
#include "Portable.h"
#include "hw_gateway.h"
#include "common.h"
#include "hw_flash.h"
#include "Queue.h"
#include "mqtt_zk_protocol.h"
      //  #include "watchdog.h"
extern void soft_reset(void); //´æ´¢Ê§°ÜÏµÍ³¸´Î»
static   uint16 recvLength = 0;//Êý¾Ý½ÓÊÕ³¤¶È
static   u8 *sbuff;      //´òÓ¡ÓÃ
#define  PICK_SIZE                 512   //Æ¬¹Ì¼þ´óÐ¡    
#define  SERVER_PICK_SIZE          20480 //·Ö¹Ì¼þ´óÐ¡
static u32 tihs_time_SERVER_PICK_SIZE=0;//±¾´Î·Ö¼þ´óÐ¡
static u32 last_total_size=0;           //×îºó·þÎñÆ÷¹Ì¼þÇÐÆ¬´óÐ¡
static u32 firmware_total_size=0;       //×Ü¹Ì¼þ´óÐ¡
static u32 save_byete_counter=0;        //ÀÛ¼Æ´æ´¢×Ö½ÚÊý
static u32 server_big_pick_counter=0;//
extern  void SET_NB_STAT_EPOWER_DOWN(void);
extern   void upload_ota_progress_bar(u32 id ,u32 progress_bar   );
extern void set_gateway_state_idle(void) ;
static u8 POWERED_DOWN_read_count=0;
static u32 http_get_timer=0;
static u32 wait_data_timer=0;
static CONNECT_OTA_state_en ota_connect_state=CONNECT_OTA_STATE_IDLE;
 MCU_OTA_state_en  MCU_OTA_state=MCU_OTA_STATE_IDLE;
static u8 OTA_DATA_IS_READY=0;
static u8 OTA_DATA_IS_finish=0;
static u32  pfile=0;//¹Ì¼þ×Ö½ÚÖ¸ÕëÎ»ÖÃ
extern QUEUE  usartRecvQueue;//´®¿ÚÊý¾Ý½ÓÊÕ¶ÓÁÐ
static u8 last_server_big_pick=0;
static u16 SERVER_CHECSUM=0;
static u16 checsum_temp=0;
uint16 pack_length=0;
static  uint8 *pack_buf;
static u8  have_get_pack_length=0;
static u32 wait_openota_timer=0;
u32 congfig_delay_timer=0;
typedef enum 
{//+QFDWL:
DATA_STATE_IDLE,
DATA_STATE_2B,   //+
DATA_STATE_51,   //Q
DATA_STATE_46,   //F
DATA_STATE_44,   //D
DATA_STATE_57,   //W
DATA_STATE_4C,   //L 
DATA_STATE_3A,   //:  
DATA_STATE_20,   //¿Õ¸ñ
DATA_STATE_GET_SUM,   
DATA_STATE_FINEISH    
} DATA_STATE_en  ;
static DATA_STATE_en  data_state=DATA_STATE_IDLE;

char firm_name_buffer[256];
static   char common_send_buff[256];
static void zk_build_ota_url_string(char *buf, u16 buf_size)
{
    const char *url;

    url = zk_get_ota_url();
    if (url != NULL && url[0] != '\0')
    {
        snprintf(buf, buf_size, "%s\r\n", url);
    }
    else
    {
        snprintf(buf, buf_size, "http://47.120.15.220:888/downloads/%s\r\n", firm_name_buffer);
    }
}


/************************************
¹¦ÄÜÃèÊö£ºÆô¶¯Ó²¼þ¸´Î»
*************************************/
void  _4G_OTA_machine_star(void)
{
       ota_connect_state=CONNECT_OTA_RESETING;
       server_big_pick_counter=0;
       POWERED_DOWN_read_count=0;
       http_get_timer=0;
}
/************************************
¹¦ÄÜÃèÊö£ºÆô¶¯·þÎñÆ÷¹Ì¼þÏÂÔØÅäÖÃ
*************************************/
void  _4G_OTA_machine_contextid(void) 
    {   

  // send_AT_Command_machine_star("AT+QHTTPCFG=\"contextid\",1 \r\n", strlen("AT+QHTTPCFG=\"contextid\",1 \r\n"), "OK\r\n", 20, 0);
     congfig_delay_timer=Timer_GetTickCount();
     ota_connect_state=CONNECT_OTA_AT_QHTTPCFG_CINFIG;    
       
    } 
/************************************
¹¦ÄÜÃèÊö£º²éÑ¯¹Ì¼þÏÂÔØÍê·ñ
*************************************/
boolean_en  _4G_OTA_machine_finish(void)
    {
      if( ota_connect_state==CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH)
      {
          return BOOL_TRUE;
      }
      else{
          return BOOL_FALSE; 
      }  
    }
/************************************
¹¦ÄÜÃèÊö£º¹Ì¼þÏÂÔØ´¦Àí
*************************************/
void _4G_OTA_machine(void) 
{  

     switch(ota_connect_state)
    {
      case CONNECT_OTA_STATE_IDLE:
            break;
       case CONNECT_OTA_RESETING:
            resetNbModule();//Ä£¿é¸´Î»
            ota_connect_state=CONNECT_OTA_READY;
            break;
        
       case CONNECT_OTA_READY:                 
             if( _4g_reset_finish()==BOOL_TRUE)
            {       
               recvLength = 0;                
                if (readLine(stringBuf, &recvLength, 0))
                {   
                    if( strstr((const char *) stringBuf, "POWERED DOWN"))
                     {
                            ota_connect_state=CONNECT_OTA_RESETING;
                            printf("__________________Ä£¿éÖØÉÏµç___________________\n");
                           break;
                     }
                    else if( strstr((const char *) stringBuf, "RDY"))
                    {
                              congfig_delay_timer=Timer_GetTickCount();
                              ota_connect_state=CONNECT_OTA_AT_QHTTPCFG_CINFIG; 
                    }
                    else
                    {     //Î´¶Áµ½
                         if(POWERED_DOWN_read_count<2) 
                         { //ÖØ¶Á                   
                                ota_connect_state= CONNECT_OTA_READY;
                          break;
                         }
                         else
                         {//Ö±½Ó×ßÏÂÈ¥
                             POWERED_DOWN_read_count=0;
                         }
                      }
                   }
              }
       
             break;
        case CONNECT_OTA_AT_QHTTPCFG_CINFIG:
             if(Timer_PassedDelay(congfig_delay_timer,400))  //OTA ÇÐ¹ýÀ´µÄÊ±ºòÒªµÈ300mS ÒÔÉÏ
             {
                   send_AT_Command_machine_star("AT+QMTCLOSE=0\r\n",strlen("AT+QMTCLOSE=0\r\n"),"OK", 5, 1);//+QMTCLOSE: <client_idx>,<result>+QMTCLOSE: 0,0
                   ota_connect_state= CONNECT_OTA_AT_QMTT_CLOSE;
             }
             break;
             
         case CONNECT_OTA_AT_QMTT_CLOSE:
              if(send_AT_Command_machine_finish()==TRUE)   
              {    
                    //¹Ø±ÕQMT£¬ÇÐµ½OTAÄ£Ê½
                    set_gateway_state_idle() ;//ÖÃÎ»Í¨ÐÅ¾²Ä¬×´Ì¬  
                    SET_NB_STAT_EPOWER_DOWN();//ÖØÖÃURC´¦Àí×´Ì¬ 
                    OTA_ENABLE=1;  
                    send_AT_Command_machine_star("AT+QHTTPCFG=\"contextid\",1 \r\n", strlen("AT+QHTTPCFG=\"contextid\",1 \r\n"), "OK\r\n", 20, 0);
                    ota_connect_state=CONNECT_OTA_AT_QHTTPCFG_contextid;              
             }
             break;   
       
         case CONNECT_OTA_AT_QHTTPCFG_contextid:
             if(send_AT_Command_machine_finish()==TRUE)
             {
                  send_AT_Command_machine_star("AT+QHTTPCFG=\"responseheader\",0 \r\n", strlen("AT+QHTTPCFG=\"responseheader\",0 \r\n"),"OK\r\n", 20, 0);
                  ota_connect_state=CONNECT_OTA_AT_QHTTPCFG_responseheader;              
             }
           break;   
             
         case CONNECT_OTA_AT_QHTTPCFG_responseheader:
             if(send_AT_Command_machine_finish()==TRUE)
             {
                 u16 length;
                static char buff[64];
                 zk_build_ota_url_string(common_send_buff, sizeof(common_send_buff));
                 length=strlen(common_send_buff)-2;
                 sprintf(buff,"AT+QHTTPURL=%u,30\r\n",length);
                 printf("AT+QHTTPURL=%s\n",buff);
                 send_AT_Command_machine_star(buff, strlen(buff), "CONNECT",20, 0);
              // send_AT_Command_machine_star("AT+QHTTPURL=43,30\r\n", strlen("AT+QHTTPURL=43,30\r\n"), "CONNECT",20, 0);//43

                  ota_connect_state=CONNECT_OTA_AT_QHTTPURL;   
             }                 
             break;  
             
         case CONNECT_OTA_AT_QHTTPURL:
             if(send_AT_Command_machine_finish()==TRUE)
             {
                memset(common_send_buff,0x00,128);
                zk_build_ota_url_string(common_send_buff, sizeof(common_send_buff));
                 printf("firm_name_buffer1=%s\n",common_send_buff);
                 send_AT_Command_machine_star(common_send_buff, strlen(common_send_buff), "OK",20, 0);
                // send_AT_Command_machine_star("http://47.120.15.220:888/downloads/cat1.bin\r\n", strlen("http://47.120.15.220:888/downloads/cat1.bin\r\n"), "OK",20, 0);      //²»ÒªÉ¾³ý
                //  send_AT_Command_machine_star("http://47.120.15.220:888/downloads/cat120250401162230.bin\r\n", strlen("http://47.120.15.220:888/downloads/cat120250401162230.bin\r\n"), "OK",20, 0);   
                   ota_connect_state=CONNECT_OTA_AT_QFDEL;   
             }                 
              break;                
               
          case CONNECT_OTA_AT_QFDEL :
              if(send_AT_Command_machine_finish()==TRUE)    
              {
                   send_AT_Command_machine_star("AT+QFDEL=\"*\"\r\n",strlen("AT+QFDEL=\"*\"\r\n"), "OK",20, 0);//ÏÂÔØÇ°Çå³ý¾É¹Ì¼þ,ÌÚ³ö¿Õ¼ä
                   ota_connect_state=CONNECT_OTA_AT_QHTTPGET;  
              }  
              //´Ë´¦Ã»ÓÐ break; ²»Òª¼Ó break  
             
           case CONNECT_OTA_AT_QHTTPGET:         //  AT+QHTTPGETEX
                if(send_AT_Command_machine_finish()==TRUE)
                {
                    if( server_big_pick_counter==0)
                    { 
                        send_AT_Command_machine_star("AT+QHTTPGETEX=80,0,20480\r\n",strlen("AT+QHTTPGETEX=80,0,20480\r\n"),"+QHTTPGET: 0,206,",200, 1);//+QHTTPGET: 0,206,20480
                    }

                    else
                    { 
                        send_AT_Command_machine_star("AT+QHTTPGETEX=80,-1,20480\r\n",strlen("AT+QHTTPGETEX=80,-1,20480\r\n"),"+QHTTPGET: 0,206,",200, 1);//+QHTTPGET: 0,206,1024
                    }
                        ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE;   
                }  
                break;                   
                 
       case CONNECT_OTA_AT_QHTTPREADFILE:
             if(send_AT_Command_machine_finish()==TRUE)
             {
                sprintf(common_send_buff,"AT+QHTTPREADFILE=\"UFS:%s\",80 \r\n",firm_name_buffer );
                printf("firm_name_buffer2=%s\n",common_send_buff);
                send_AT_Command_machine_star(common_send_buff,strlen(common_send_buff),"OK",100, 1);//ÊÕµ½+QHTTPREADFILE: 0  ÐèÒª500ms
             // send_AT_Command_machine_star("AT+QHTTPREADFILE=\"UFS:cat1.bin\",80 \r\n",strlen("AT+QHTTPREADFILE=\"UFS:cat1.bin\",80 \r\n"),"OK",100, 1);//ÊÕµ½+QHTTPREADFILE: 0  ÐèÒª500ms   //²»ÒªÉ¾³ý
             // send_AT_Command_machine_star("AT+QHTTPREADFILE=\"UFS:cat120250401162230.bin\",80 \r\n",strlen("AT+QHTTPREADFILE=\"UFS:cat120250401162230.bin\",80 \r\n"),"OK",100, 1);//ÊÕµ½+QHTTPREADFILE: 0  ÐèÒª500ms   //²»ÒªÉ¾³ý
                http_get_timer=Timer_GetTickCount();
                ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_WAIT;   
             }                 
             break;   
             
        case CONNECT_OTA_AT_QHTTPREADFILE_STROE_WAIT:       
             if(send_AT_Command_machine_finish()==TRUE)
             {
                if(!Timer_PassedDelay(http_get_timer, 80000))    //    &&Timer_PassedDelay(http_get__data_wait_timer, 150)
                {   
                    if (readLine(stringBuf, &recvLength, 0))
                    {  
                      // printf_buf(stringBuf,recvLength);//ÕâÀï»áÊä³ö¹Ì¼þÐÅÏ¢
                        if( strstr((const char *) stringBuf, "+QHTTPREADFILE: 0"))
                        {  //µ÷ÊÔÍêºó¸ÄÎª¡°+QHTTPREADFILE: 0 ¡±
                                ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH; 
                                mcu_copy_firmware_star();
                                set_gateway_state_idle();//ÖÃÎ»Í¨ÐÅ¾²Ä¬×´Ì¬
                                printf("_____________Í¨ÐÅÄ£¿éÊÕµ½¹Ì¼þ__________\n");
                                printf("_______________MCU¿ªÊ¼OTA_______________n");
                                break;
                        }
                        else
                        {
                          memset(stringBuf,0x00,recvLength);//²»Çå¿Õ»áÖØ¸´¶ÁÏàÍ¬µÄÄÚÈÝ
                          recvLength=0;
                        }    
                     }          
                } // µÈ´ý³É¹¦´¢´æÏìÓ¦ÏûÏ¢<80S
                else
                {
                     printf("__________________Ä£¿éOTA´æ´¢Ê§°Ü___________________\n");
                     ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH; 
                     soft_reset();  
                }
             }                 
             break;        
        case CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH:
                break;
     }        
 }    

 typedef enum
 {
     OTA_PROGRESS_AT_REPORT_IDLE,
     OTA_PROGRESS_REPORT_CONFIG,
     OTA_PROGRESS_AT_REPORT_PROGRESS,
     OTA_PROGRESS_AT_REPORT_CLOSE,
     OTA_PROGRESS_AT_CANHGEBACKTO_OTA,
     OTA_PROGRESS_AT_REPORT_FINISH,
 }OTA_PROGRESS_en;
 OTA_PROGRESS_en ota_progress_state=OTA_PROGRESS_AT_REPORT_IDLE;
 /************************************
¹¦ÄÜÃèÊö£ºÆô¶¯½ø¶ÈÉÏ±¨
*************************************/ 
 void ota_progress_report_statr(void)
 {
   ota_progress_state=OTA_PROGRESS_REPORT_CONFIG;
 }
 
/************************************
¹¦ÄÜÃèÊö£º²éÑ¯½ø¶ÈÉÏ±¨Íê·ñ
*************************************/ 
boolean_en ota_progress_report_is_finish(void)
{
   if(ota_progress_state==OTA_PROGRESS_AT_REPORT_FINISH)  
   {  
    return BOOL_TRUE;
   }
   else
   {
       return BOOL_FALSE;    
   }
       
 }
 

/************************************
¹¦ÄÜÃèÊö£ºÉÏ´«¹Ì¼þ½ø¶È
×¢Òâ£º    Ö»ÄÜÔÚOTA¹Ø±ÕÊ±ÉÏ´«
*************************************/          
void upload_ota_progress_fsm_process(void)
{      
   switch(ota_progress_state)   
   {
       case OTA_PROGRESS_AT_REPORT_IDLE :
                break;
       
       case   OTA_PROGRESS_REPORT_CONFIG:
           if(send_AT_Command_machine_finish()==TRUE)
            {
                OTA_ENABLE=0;//¹Ø±ÕOTA£¬ÇÐµ½MQTT
                 _4G_configModule_star_from_onestate( CONNECT_CONFIG_AT_qmtping) ;//´ÓÄ³Ò»¸ö×´Ì¬¿ªÊ¼Æô¶¯,·¢ËÍMQTT¿ªÆôÖ¸Áî   
                 ota_progress_state=OTA_PROGRESS_AT_REPORT_PROGRESS; 
            }
       
          break;
       case    OTA_PROGRESS_AT_REPORT_PROGRESS:
           
              if(_4G_configModule_machine_finish() ==BOOL_TRUE)
              {
                  if(server_big_pick_counter==0)
                  {
                   upload_ota_progress_bar(DEVICE_ID ,30 ); 
                  }
                  else if(server_big_pick_counter==1)
                  {
                    upload_ota_progress_bar(DEVICE_ID ,50 );
                  }
                  else if(last_server_big_pick)
                  {
                    upload_ota_progress_bar(DEVICE_ID ,90 );
                  }        
                  wait_openota_timer =Timer_GetTickCount();//»ñÈ¡Ê±¼äµã£¬½ø¶ÈÉÏ±¨ÐèÒªºÄÊ±100mS£¬
                  ota_progress_state=OTA_PROGRESS_AT_REPORT_CLOSE;               
               }
                 break;
            
        case   OTA_PROGRESS_AT_REPORT_CLOSE:  
               if (Timer_PassedDelay(wait_openota_timer, 200))    //µÈ,200MS Êý¾Ý·¢Íê ,Õý³£ÊÇ½ø¶ÈÉÏ±¨ÍêºÄÊ±100mS£¬Ê±¼ä²»¹»AT+QMTCLOSE=0   »á²åÈëµ½ PPPP„\0?¡õ
               {                                    
                 send_AT_Command_machine_star("AT+QMTCLOSE=0\r\n",strlen("AT+QMTCLOSE=0\r\n"),"OK", 2, 1);//+QMTCLOSE: <client_idx>,<result>+QMTCLOSE: 0,0//"+QMTCLOSE: 0,0"
                 ota_progress_state=OTA_PROGRESS_AT_CANHGEBACKTO_OTA; 
               }
               break;
            
           case  OTA_PROGRESS_AT_CANHGEBACKTO_OTA:   
                if(send_AT_Command_machine_finish()==TRUE)
                {
                  OTA_ENABLE=1;
                  ota_progress_state=OTA_PROGRESS_AT_REPORT_FINISH; 
                }
                 break;
                
           case OTA_PROGRESS_AT_REPORT_FINISH:
             break;
     }

}

/************************************
¹¦ÄÜÃèÊö£º×ªÒÆ¹Ì¼þ³É¹¦²éÑ¯
*************************************/
boolean_en  mcu_copy_firmware_finish(void) 
{
      if( MCU_OTA_state==MCU_OTA__COMPLETE)
      {
          
          return BOOL_TRUE;
      }
      else{
          return BOOL_FALSE; 
      }  
}
/************************************
¹¦ÄÜÃèÊö£º½ÓÊÕ¹Ì¼þ×´Ì¬²éÑ¯
*************************************/
boolean_en  mcu_copy_firmware_getdata(void) 
{
      if( MCU_OTA_state==MCU_OTA_MCU_GETDATA)
      {
          return BOOL_TRUE;
      }
      else{
          return BOOL_FALSE; 
      }  
}

/************************************
¹¦ÄÜÃèÊö£º¿ªÊ¼×ªÒÆ¹Ì¼þ
*************************************/
 void  mcu_copy_firmware_star(void)
     {
         MCU_OTA_state=MCU_OTA_STATE_RESETING;
         server_big_pick_counter=0;
         save_byete_counter=0;
         firmware_total_size=0;
         SERVER_CHECSUM=0;
         last_server_big_pick=0;
         last_total_size=0;
         pfile=0;
         checsum_temp=0;
         OTA_DATA_IS_READY=0;
         OTA_DATA_IS_finish=0;
         data_state=DATA_STATE_IDLE;
         tihs_time_SERVER_PICK_SIZE=0;
     }


 
/************************************
¹¦ÄÜÃèÊö£ºÒì»ò¼ÆËãÐ£ÑéºÍ+
*************************************/
 #include <stdint.h>
// ¼ÆËã 16 Î»Ð£ÑéºÍ
 u16 bak_frash_checksum_XOR(u32 size) 
{
    u16 checksum = 0;
    u8 *data;
    data=( u8 *)(OTABAKROM_STARTADDR );
    // Èç¹ûÊý¾Ý³¤¶ÈÎªÆæÊý
    if (size % 2 != 0)
    {
        // ½«×îºóÒ»¸ö×Ö·ûÉèÖÃÎª¸ß 8 Î»,µÍ 8 Î»ÉèÖÃÎª 0
        checksum = data[size - 1] << 8;
        size--;
    }

    // Ö´ÐÐ 16 Î» XOR ÔËËã
    for (u32 i = 0; i < size; i += 2) 
    {
        uint16_t word = (data[i] << 8) | data[i + 1];
        checksum ^= word;
    }
     printf("XORSUM=0x%02x\n", checksum); 
    return checksum;
}
 

 /************************************
¹¦ÄÜÃèÊö£º¼ì²â±¸·ÝÇøµÄÊý¾ÝÊÇ·ñÕýÈ·£¬È·ÈÏ³ÌÐòµÄÍêÕûÐÔ¡£
ÊäÈë²ÎÊý£ºÎÞ
Êä³ö·µ»Ø£ºÕýÈ··µ»Ø BOOL_TRUE£¬Ð£Ñé²»Í¨¹ý·µ»Ø BOOL_FALSE

*************************************/
boolean_en get_checksum_status_XOR( u16 sum, u32 size)
{
   
    if( (size<(u32)58*2048 && sum == bak_frash_checksum_XOR(size)))
    {
        return BOOL_TRUE;
    }
    else
    {
        return BOOL_FALSE;
    }
}
 

u32 sum32(u32 dat)
{
    u32 sum   = u32hh(dat);
    sum = sum + u32hl(dat);
    sum = sum + u32lh(dat);
    sum = sum + u32ll(dat);
    return(sum);
}

/************************************
¹¦ÄÜÃèÊö£º¼ÆËã³ÌÐòÐ´ÉÕºóµÄÐ£ÑéÖµ
ÊäÈë²ÎÊý£ºÊý¾ÝµÄ´óÐ¡£¬4×Ö½ÚÊý¡£
Êä³ö·µ»Ø£ºÐ£ÑéÖµ
*************************************/
u32 user_frash_checksum(u32 size)
{
    u32 tmp;
    u32 i,sum = 0;
    for(i=0; i<size; i++)
    {
        if(i<ADDR_CHECKSUM_OFFSET/4 || i>=(ADDR_SIZE_OFFSET+4)/4)
        {
            tmp = *((__IO u32 *)OTABAKROM_STARTADDR + i);   //Ð£Ñé±¸·ÝÇø
            sum += sum32(tmp);
        }
    }
      printf("check_sum=0x%x\n", sum);
    return(sum);  
}


/************************************
¹¦ÄÜÃèÊö£º¼ì²âÓ¦ÓÃÇøµÄÊý¾ÝÊÇ·ñÕýÈ·£¬È·ÈÏ³ÌÐòµÄÍêÕûÐÔ¡£
ÊäÈë²ÎÊý£ºÎÞ
Êä³ö·µ»Ø£ºÕýÈ··µ»Ø BOOL_TRUE£¬Ð£Ñé²»Í¨¹ý·µ»Ø BOOL_FALSE

*************************************/
boolean_en get_checksum_status(void)
{
    u32 sum, size;
    sum = *((__IO u32 *)OTABAKROM_STARTADDR + ADDR_CHECKSUM_OFFSET/4);   
    size = *((__IO u32 *)OTABAKROM_STARTADDR + ADDR_SIZE_OFFSET/4);
    size = size & 0xffffff;  //½«¸ß¶àÓàÎ»ÇåÁã
    if((sum==(u32)0x12345678) || (size<(u32)58*2048 && sum == user_frash_checksum(size/4)))
    { 
       return BOOL_TRUE;
    }
    else
    {
        return BOOL_FALSE;
    }
}


/************************************
¹¦ÄÜÃèÊö£º¹Ì¼þ×ªÒÆ´¦Àí
*************************************/
 
void  mcu_copy_firmware_machine(void)     
{  
    upload_ota_progress_fsm_process();
   switch(MCU_OTA_state)            
   {        
        case  MCU_OTA_STATE_IDLE:
            
                break;
               
        case  MCU_OTA_STATE_RESETING:
               //Æô¶¯×ªÒÆ¹Ì¼þµ½MCU 
                 
                sprintf(common_send_buff,"+QFLST: \"UFS:%s\"",firm_name_buffer );
                printf("firm_name_buffer3=%s\n",common_send_buff);
                send_AT_Command_machine_star("AT+QFLST\r\n",strlen("AT+QFLST\r\n"),common_send_buff,20,1);     // ÒªÌáÈ¡ÎÄ¼þ´óÐ¡     ²åÈë¹Ì¼þÃû×Ö·û´®
           //   send_AT_Command_machine_star("AT+QFLST\r\n",strlen("AT+QFLST\r\n"),"+QFLST: \"UFS:cat1.bin\"",20,1);     // ÒªÌáÈ¡ÎÄ¼þ´óÐ¡           //²»ÒªÉ¾³ý
             // send_AT_Command_machine_star("AT+QFLST\r\n",strlen("AT+QFLST\r\n"),"+QFLST: \"UFS:cat120250401162230.bin\"",20,1);     // ÒªÌáÈ¡ÎÄ¼þ´óÐ¡           //²»ÒªÉ¾³ý
                 printf("_STROE_FINISH_OTA\n "); //+QFLST: "UFS:cat1.bin",24136
                 MCU_OTA_state=MCU_OTA_STATE_GETFILESIZE;              
       break;      
 

       case MCU_OTA_STATE_GETFILESIZE:
           if(send_AT_Command_machine_finish()==TRUE)  
            {/*
                  if (readLine(stringBuf, &recvLength, 0))
                    {
                        //ÌáÈ¡ÎÄ¼þÃûºóÐø´¦Àí  
                        
                      text=strstr((const char *) stringBuf, "+QFLST: \"UFS:cat1.bin\",");
                          printf("_×Ö·ûÎ»ÖÃ=%s\n",text);
                       if(text!=NULL){   //ÌáÈ¡ÎÄ¼þ´óÐ¡ 
                          text+=23;
                            printf("______________firmware_size=%s\n",text);
                             while (*text != '\r') {
                           pLength = pLength * 10 + *text - '0';
                             text++;
                            }
                           firmware_size =pLength;
                            printf("______________firmware_size=%d\n",firmware_size);
                           pLength=0;
   
                       }
                       else
                       {
                           printf("______________firmware_size=Î´½øÈë\r\n");
                       
                       }

                    }*/
                printf("----MCU_OTA_state=%d,=%d\n",MCU_OTA_state,MCU_OTA_state==MCU_OTA_STATE_QFDWL_GET_FIRMWARE);
                static char common_send_buff_2[64]; 
                sprintf(common_send_buff,"AT+QFDWL=\"%s\"\r\n",firm_name_buffer );
                sprintf(common_send_buff_2,"AT+QFDWL=\"%s\"",firm_name_buffer );
                printf("firm_name_buffer4=%s\n",common_send_buff);             
                send_AT_Command_machine_star(common_send_buff,strlen(common_send_buff),common_send_buff_2,255,1); //+QFDWL: 152937,0a31 "CONNECT\r\n"  //???
            //  send_AT_Command_machine_star("AT+QFDWL=\"cat1.bin\"\r\n",strlen("AT+QFDWL=\"cat1.bin\"\r\n"),"AT+QFDWL=\"cat1.bin\"",255,1); //+QFDWL: 152937,0a31 "CONNECT\r\n"  //²»ÒªÉ¾³ý
            //  send_AT_Command_machine_star("AT+QFDWL=\"cat120250401162230.bin\"\r\n",strlen("AT+QFDWL=\"cat120250401162230.bin\"\r\n"),"AT+QFDWL=\"cat120250401162230.bin\"",255,1); //+QFDWL: 152937,0a31 "CONNECT\r\n"  //²»ÒªÉ¾³ý
                     MCU_OTA_state=MCU_OTA_STATE_QFDWL;
                }
                break;
                
          case  MCU_OTA_STATE_QFDWL :
              if(send_AT_Command_machine_finish()==TRUE)  
               { 
                    flushQueue(&usartRecvQueue);
                    memset(stringBuf,0x00,recvLength);//²»Çå¿Õ»á¶àºÄµãÊ±¼ä½ø³ö»º³å 
                    recvLength=0;//
                    printf("----MCU_OTA_state=%d,=%d\n",MCU_OTA_state,MCU_OTA_state==MCU_OTA_STATE_QFDWL_GET_FIRMWARE);
                    MCU_OTA_state=MCU_OTA_STATE_QFDWL_GET_FIRMWARE;
                    data_state=DATA_STATE_IDLE;//´ò¿ªÐòÁÐ¼ìË÷ 
               }
               break;
               
          case  MCU_OTA_STATE_QFDWL_GET_FIRMWARE :                              
                {                      
                       uint8  dat;
                       static u8 *recvURC;
                       static u32 leng_temp=0;  
                       static uint8 buf[2];
                       static u16 chec_size=0;
                       //    watchdog_feed_dog(); 
                       while (dequeue(&usartRecvQueue, &dat))       //+QFDWL: 152937,0a31   //2B 51 46 44 57 4C 3A 
                       {   
                              //  log_u32(1,  dat);
                                switch(data_state)
                                {
                                    
                                     
                                      case DATA_STATE_IDLE:
                                           if(dat=='+')//0x2B
                                           {  //  log_u32(1,  dat); 
                                               
                                                                                
                                             data_state=DATA_STATE_2B; 
                                           }
                                           else
                                           {
                                                data_state=DATA_STATE_IDLE; 
                                           }
                                           break;
                                      case DATA_STATE_2B:    
                                           if(dat=='Q')//0x51
                                           { //log_u32(1,  dat);
                                             data_state=DATA_STATE_51;  
                                           }
                                           else
                                           {
                                                data_state=DATA_STATE_IDLE; 
                                           }                          
                                           break;
                                      case DATA_STATE_51:
                                           if(dat=='F')//0x46
                                           {  //   log_u32(1,  dat);                                             
                                            data_state=DATA_STATE_46;
                                           }
                                           else
                                           {
                                            data_state=DATA_STATE_IDLE; 
                                           }                           
                                           break;
                                      case DATA_STATE_46:
                                           if(dat=='D')//0x44
                                           {   log_u32(1,  dat);
                                                data_state=DATA_STATE_44;  
                                           }
                                           else
                                           {
                                                data_state=DATA_STATE_IDLE; 
                                           }
                                           break;
                                          
                                      case DATA_STATE_44:
                                           if(dat=='W')
                                           { log_u32(1,  dat);
                                                data_state=DATA_STATE_57;  
                                           }                                                   
                                           else
                                           {
                                                data_state=DATA_STATE_IDLE; 
                                           }
                                           break;
                                      case DATA_STATE_57:
                                          
                                           if(dat=='L')
                                           {
                                                log_u32(1,  dat);
                                                data_state=DATA_STATE_4C;
                                           }
                                           else
                                           {
                                                data_state=DATA_STATE_IDLE; 
                                           }                          
                                           break;   
                                           
                                      case DATA_STATE_4C:
                                           if(dat==':')
                                           {
                                             //   log_u32(1,  dat);
                                                data_state=DATA_STATE_3A;
                                           }
                                           else
                                           {
                                                data_state=DATA_STATE_IDLE; 
                                           }                          
                                           break;
                                          
                                      case DATA_STATE_3A:  
                                          if(dat==0x20)//¿Õ¸ñ
                                           {
                                             //   log_u32(1,  dat);
                                                data_state=DATA_STATE_20;
                                           }
                                           else
                                           {
                                                data_state=DATA_STATE_IDLE; 
                                           }                          
                                           break;
                                       case DATA_STATE_20: 
                                            stringBuf[recvLength] =dat;
                                             ++recvLength;
                                             if(recvLength>21)  // Êý¾Ý¹ý³¤Ê±¿¼ÂÇ
                                              {
                                                  data_state=DATA_STATE_FINEISH; 
                                                 break; 
                                              }   
                                           
                                           if ((recvLength >= 2) && (stringBuf[recvLength - 2] == '\r') && (stringBuf[recvLength- 1] == '\n')) 
                                           {
                                                stringBuf[recvLength ]= 0;
                                                data_state=DATA_STATE_GET_SUM;   
                                               
                                           }
                                           break;
                                      case DATA_STATE_GET_SUM:
                                           recvURC=stringBuf;  
                                           //  watchdog_feed_dog(); 
                                           while (*recvURC != ',')
                                           {
                                            leng_temp =leng_temp * 10 + *recvURC - '0';
                                             recvURC++;
                                            }                         
                                            recvURC++;
                                           if( hexStrToByte((const char *)recvURC,4,buf, &chec_size) )   //4×Ö½Ú×Ö·ûchecsum×ª³ÉÁ½×Ö½Ú
                                           {   
                                              checsum_temp=buf[0]<<8|buf[1];//»ñÈ¡Ð£ÑéÖµ
                                              printf("recvURC=%d\n",chec_size);
                                              printf("recvURC=%02x\n",buf[0]);
                                              printf("recvURC=%02x\n",buf[1]);                                               
                                           }
                                            //ÌáÈ¡³¤¶ÈºÍÐ£ÑéÖµ  
                                            tihs_time_SERVER_PICK_SIZE=leng_temp;//±¾´Î·Ö¹Ì¼þ´óÐ¡
                                            if(leng_temp<SERVER_PICK_SIZE)//ÁãÍ·Ö¡000000000
                                            {
                                                last_server_big_pick=1; // ×îºóÒ»Ö¡±êÖ¾
                                                last_total_size=leng_temp;
                                                firmware_total_size+=leng_temp;
                                                SERVER_CHECSUM^=checsum_temp;
                                                 printf("total_temp=0x%08x\n",SERVER_CHECSUM); 
                                                if( get_checksum_status_XOR( SERVER_CHECSUM, firmware_total_size)  )
                                                { 
                                                    printf("OTA_OK___2\n");            
                                                }
                                            }  
                                            else if(leng_temp==SERVER_PICK_SIZE)//ÕûÖ¡
                                            {
                                               firmware_total_size+=SERVER_PICK_SIZE;
                                               SERVER_CHECSUM^=checsum_temp; 
                                              
                                                printf("total_temp2=0x%08x\n",SERVER_CHECSUM); 
                                               if( get_checksum_status_XOR( SERVER_CHECSUM, firmware_total_size)  )
                                               { 
                                                    printf("OTA_OK___1\n");
                                                   
                                               }
                                            }
                                            else
                                            {
                                             //´óÓÚSERVER_PICK_SIZE´íÎóÈçºÎÌø×ª£¿----------------//ÖØÐÂÏÂÔØ£¿---------------
                                                
                                            }
                                             leng_temp=0;
                                             memset(stringBuf,0x00,600);//²»Çå¿Õ»áÖØ¸´¶ÁÏàÍ¬µÄÄÚÈÝ
                                             recvLength=0;
                                             MCU_OTA_state= MCU_OTA_AT_QFLST;
                                             data_state=DATA_STATE_FINEISH; 
                                             break;    
                                           
                                      case DATA_STATE_FINEISH:
                                           break;
                               }                                          
                        }        
              }    
              break; 
               case MCU_OTA_AT_QFLST:

                  sprintf(common_send_buff,"AT+QFOPEN=\"%s\",2\r\n",firm_name_buffer );
                  printf("firm_name_buffer5=\"%s\"\n",common_send_buff);
                  send_AT_Command_machine_star(common_send_buff,strlen(common_send_buff),"+QFOPEN:",20,1);   //»ñÈ¡ÎÄ¼þÖ¸Õë+QFOPEN: 1
             // send_AT_Command_machine_star("AT+QFOPEN=\"cat1.bin\",2\r\n",strlen("AT+QFOPEN=\"cat1.bin\",2\r\n"),"+QFOPEN:",20,1);   //»ñÈ¡ÎÄ¼þÖ¸Õë+QFOPEN: 1
            //   send_AT_Command_machine_star("AT+QFOPEN=\"cat120250401162230.bin\",2\r\n",strlen("AT+QFOPEN=\"cat120250401162230.bin\",2\r\n"),"+QFOPEN:",20,1);   //»ñÈ¡ÎÄ¼þÖ¸Õë+QFOPEN: 1
                  pfile=0;//¹Ì¼þ×Ö½Ú³õÊ¼Î»ÖÃ
                  MCU_OTA_state=MCU_OTA_AT_QFOPEN;           
              break;
    
        case MCU_OTA_AT_QFOPEN:
             if(send_AT_Command_machine_finish()==TRUE )
             {
                 //·ÖÆ¬
                 static   char common_temp[32] ="AT+QFSEEK=1,%u,0\r\n";   

                 sprintf(common_send_buff,common_temp,pfile );                  
                 send_AT_Command_machine_star(common_send_buff,strlen(common_send_buff),"OK",20,1);   //ÉèÖÃÎÄ¼þÖ¸ÕëÎªÎÄ¼þµÄ³õÊ¼Î»ÖÃ¡£ 
                 MCU_OTA_state=MCU_OTA_AT_QFSEEK;               
             }  
            break;      
             
       case MCU_OTA_AT_QFSEEK:
             if(send_AT_Command_machine_finish()==TRUE)
             {   
               send_AT_Command_machine_star("AT+QFREAD=1,512\r\n",strlen("AT+QFREAD=1,512\r\n"),"CONNECT ",20,1);  //¶ÁÈ¡Êý¾Ý¡£"CONNECT 512\r\n"
               wait_data_timer =Timer_GetTickCount();
               MCU_OTA_state=MCU_OTA_AT_QFREAD;   
             }                 
             break;                       
        case MCU_OTA_AT_QFREAD:                        //¶ÁÈ¡Êý¾Ý¡£
             if(send_AT_Command_machine_finish()==TRUE)
             { 
                      if(have_get_pack_length==0)
                      { 
                          if( strstr((const char *) stringBuf, "CONNECT"))
                           {  
                                static  u16 pack_length_temp=0;
                                pack_buf=stringBuf;   //stringBufÄÚÈÝ£ºAT+QFREAD=1,512\r\nCONNECT 512\r\n
                             // printf("CONNECTpack_buf=%s\n",pack_buf);
                                pack_buf+=26;   
                                printf("CONNECTpack_buf=%s\n",pack_buf);
                                while (*pack_buf !='\r' )//ÕâÀïÒÔ\rÅÐ¶Ï½áÎ²ÓÐÊ±»áÊý¾Ý¹ý´ó£¬±ÈÈç512»á±ä³É51255£¬Ö»ºÃÓÃpack_length_temp>PICK_SIZEÀ´½ØÈ¡
                               {
                                    pack_length_temp =pack_length_temp * 10 + *pack_buf - '0';
                                    pack_buf++; 
                                 if(pack_length_temp>PICK_SIZE )
                                  {
                                      printf("__»ñÈ¡Öµ¹ý´ó=%d\r\n",pack_length_temp);
                                      pack_length_temp=0;
                                      break;
                                  }
                               } 
                               pack_length= pack_length_temp;
                               pack_length_temp=0;
                               have_get_pack_length=1;
                               printf("__»ñÈ¡µ½CONNECT=%d\r\n",pack_length);    
                          }
                          else
                          {
                              printf("__Î´»ñÈ¡µ½CONNECT_\n");  
                          } 
                       }
             
                 if (Timer_PassedDelay(wait_data_timer, 400))    //µÈ,400MS Êý¾Ý½ÓÊÕÍê ,Ì«ÉÙ½ÓÊÕ²»ÍêÕû»ò´íÎó
                 {    
                     MCU_OTA_state=MCU_OTA_MCU_GETDATA; 
                     have_get_pack_length=0;
                 }  
                 if (Timer_PassedDelay(wait_data_timer, 300)) 
                 {      //ÎÞÊý¾Ý³¬Ê±              *************´ý´¦ÀíËÀÑ­»·*********
                   //  MCU_OTA_state=MCU_OTA_AT_QFREAD_LOOP; 
                 }
             }  
             break; 
        case MCU_OTA_MCU_GETDATA:        //ÓÉ½ÓÊÕ´¦ÀíÇÐ»»
             if(OTA_DATA_IS_READY)       //ÊÕ²»µ½ÖØ·¢´ý×ö
             {
                OTA_DATA_IS_READY=0;
                printf("3MCU´æ´¢___________________\n");
                MCU_OTA_state=MCU_OTA_AT_QFREAD_LOOP;
               printf("tihs_time_SERVER_PICK_SIZE=%d\n",tihs_time_SERVER_PICK_SIZE);
             }
             else if (OTA_DATA_IS_finish)
             {
                  OTA_DATA_IS_finish=0;
                  MCU_OTA_state=MCU_OTA_AT_QFREAD_LOOP;
             }
             else if (Timer_PassedDelay(wait_data_timer, 5000))
             {
                  printf("MCU_OTA_MCU_GETDATA timeout, no firmware data received\n");
                  sys_data.sn = 3;
                  sys_data_store();
                  changea_to_MQTT_modle();
                  last_server_big_pick = 0;
                  MCU_OTA_state = MCU_OTA__COMPLETE;
             }
            break;
             
       case MCU_OTA_AT_QFREAD_LOOP:    
            if(pfile+PICK_SIZE<tihs_time_SERVER_PICK_SIZE)//Ð¡ÓÚ±¾´Î·ÖÆ¬Êý    //  ·Ö½çÏß  pfile UFS ÎÄ¼þÎ»ÖÃ
            {
                  pfile+=PICK_SIZE;
                  static   char common_temp[32] ="AT+QFSEEK=1,%u,0\r\n";   
                //  static   char common_send_buff[64];  
                  sprintf(common_send_buff,common_temp,pfile );                  
                  send_AT_Command_machine_star(common_send_buff,strlen(common_send_buff),"OK",20,1);   //ÉèÖÃÎÄ¼þÖ¸ÕëÎªÎÄ¼þµÄ³õÊ¼Î»ÖÃ¡£ 
                  MCU_OTA_state=MCU_OTA_AT_QFSEEK; 
            }
            else
            {
                printf("pfile=%u\n",pfile);
                send_AT_Command_machine_star("AT+QFCLOSE=1\r\n",strlen("AT+QFCLOSE=1\r\n"),"OK",20,1);  
                MCU_OTA_state=MCU_OTA_GET_REPORT_PROGRESS;
            }
            break;  
  
       case  MCU_OTA_GET_REPORT_PROGRESS:
            if(send_AT_Command_machine_finish()==TRUE)
            { 
                  ota_progress_report_statr();//¿ªÆôÉÏ±¨£¬ÔÙ½øÐÐÏÂÒ»²½
                  MCU_OTA_state=MCU_OTA_GET_BIG_PICK; //  
            }
            break;     
            
       case MCU_OTA_GET_BIG_PICK:
            if(ota_progress_report_is_finish())
            {
                 if( last_server_big_pick)//·þÎñÆ÷¹Ì¼þ×îºóÒ»Ö¡?Ê²Ã´Ê±ºòÇåÁã
                 {
                      server_big_pick_counter=0;
                      save_byete_counter=0;
                      pfile=0;//Ð¡Æ¬Ö¸ÕëÎ»ÖÃ¹éÁã
                      MCU_OTA_state=MCU_OTA_AT_QFCLOSE;
                  }
                  else  //
                  {
                      ++server_big_pick_counter;
                        pfile=0; //Ð¡Æ¬Ö¸ÕëÎ»ÖÃ¹éÁã
                        ota_connect_state=CONNECT_OTA_AT_QFDEL;   //È¥É¾³ýÄ£¿é¹Ì¼þ
                        MCU_OTA_state=MCU_OTA_STATE_IDLE;         //µÈ´ýÐÂµÄ·þÎñÆ÷¹Ì¼þÇÐÆ¬Æô¶¯
                  }   
             }
             break;      
             
          case MCU_OTA_AT_QFCLOSE :     //AT+QFCLOSE=1
              if( get_checksum_status_XOR( SERVER_CHECSUM, firmware_total_size)  )//´«Êä²ãÐ£Ñé                                      ---------- ´«Êä´íÎóÃ»ÓÐ·¢ÏÖ-------------
              { 
                     printf("---------------OTA_XOR_CHECK_OK\n");
                     if( get_checksum_status())  //  Ó¦ÓÃ²ã¹Ì¼þÍêÕûÐÔ×´Ì¬¶ÁÈ¡
                     {
                           printf("---------------OTA_SUM_CHECK_OK\n");
                           sys_data.sn=0xaa5555aa;//±ê¼ÇÓÐÐÂ¹Ì¼þ
                           sys_data_store();
                           MCU_OTA_state=MCU_OTA_MCU_FINISH;
                     }
                     else
                     {
                        printf("---------------OTA_SUM_CHECK_ERROR\n");
                        sys_data.sn=3;//±ê¼Ç¹Ì¼þÏÂÔØ´íÎó
                        sys_data_store();
                        changea_to_MQTT_modle();//ÇÐµ½MQTT
                        last_server_big_pick=0;//Õâ¸öÊ±ºòÇåÁã
                        MCU_OTA_state=MCU_OTA__COMPLETE;
                     }
              }
              else //Ð£Ñé´íÎó
              {
                    printf("------------OTA_XOR_CHECK_ERR\n");
                    sys_data.sn=3;//±ê¼Ç¹Ì¼þÏÂÔØ´íÎó
                    sys_data_store();
                    sbuff= ((u8*)(DATAROM_STARTADDR)) ;//´òÓ¡
                    printf_buf(sbuff,64);
                    sbuff= ((u8*)(BAKDATAROM_STARTADDR)) ;//´òÓ¡
                    printf_buf(sbuff,64);
                    changea_to_MQTT_modle();//ÇÐµ½MQTT
                    last_server_big_pick=0;//Õâ¸öÊ±ºòÇåÁã
                    MCU_OTA_state=MCU_OTA__COMPLETE;
              }
               break; 
              
          case   MCU_OTA_MCU_FINISH:
                    printf("---------OTA Íê±Ï------\n");
                    printf("---------ÏµÍ³ÖØÆô-------\n");
                    last_server_big_pick=0;//ÇåÁã±êÖ¾£¬·ÀÖ¹Ìø×ªÊ§°ÜºóÓ°ÏìÏÂ´ÎOTA
                    server_big_pick_counter=0;
                    save_byete_counter=0;
                    firmware_total_size=0;
                    SERVER_CHECSUM=0;
                    sbuff= ((u8*)(DATAROM_STARTADDR)) ;
                    printf_buf(sbuff,64);
                    sbuff= ((u8*)(DATAROM_STARTADDR)) ;
                    printf_buf(sbuff,64);
                    HAL_Delay(50);
                    //µ÷µ½bootÇø
                    extern void iap_jump2boot(void);  //-------------------Èç¹ûÃ»ÓÐ´òÓ¡Êý¾ÝÑÓÊ±µÄ»°»áÓ°ÏìBOOTµÄÌø×ª
                    MCU_OTA_state=MCU_OTA__COMPLETE;//Õâ¸öµØ·½ºÜÖØÒª£¬Ò»¶¨Òª·ÅÔÚiap_jump2boot()º¯ÊýÖ®Ç°£¬·ñÔòÌø×ªÊ§°Ü
                    iap_jump2boot();                   
      break;   
          
      case MCU_OTA__COMPLETE:
      break;  
      
      default:    
      break;    
    }
}

/**
*@brief   ´æ´¢OTAÄ£¿éÉÏ±¨µÄ¹Ì¼þÁ÷
*@param	  buf£ºÄ£¿éÉÏ±¨µÄ¹Ì¼þ
*@param	  lenth£º¹Ì¼þ³¤¶È
*@return  ÎÞ
*/
 void OTA_STROE_MCU(uint8 *buf,u16 lenth)
{                //½âÎö½á¹û
     uint16 dataLength;
      dataLength=lenth;
     if(dataLength==PICK_SIZE|| (last_server_big_pick && dataLength==(u16)(last_total_size%PICK_SIZE)))
      {  //À¹½Ø"OK\r\n"
            // printf("_________________________________save_byete_counterbingin=%u\n",save_byete_counter);
             flash_store(buf, dataLength, OTABAKROM_STARTADDR+save_byete_counter );  //-strlen("CONNECT 512\r\n")
             save_byete_counter+=dataLength;//ÀÛ¼Æ×Ö½Ú£¬µØÖ·¼Ó
              printf("_________________________________save_byete_counter=%u\n",save_byete_counter);
             MCU_OTA_state=MCU_OTA_AT_QFREAD_LOOP;
             sbuff= ((u8*)(OTABAKROM_STARTADDR+save_byete_counter)) ;
             // printf_buf(sbuff-256,64);
             printf("1MCU´æ´¢1\n");
             OTA_DATA_IS_READY=1;  //ÌáÈ¡payload³É¹¦
             if(dataLength==(u16)(firmware_total_size%PICK_SIZE) &&last_server_big_pick)  //Èç¹û×îºóÒ»Æ¬Îª0£¬»á²»»á³öBUG£¿
             {
                OTA_DATA_IS_finish=1;
             }
        }
}


