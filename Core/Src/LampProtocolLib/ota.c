/*************************************************************
程序功能：CAT.1智慧电源OTA固件更新
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：黎长彩
单位名称：广东东菱电源科技有限公司
编辑日期：2026.5.4
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
extern void soft_reset(void); //存储失败系统复位
static   uint16 recvLength = 0;//数据接收长度
static   u8 *sbuff;      //打印用
#define  PICK_SIZE                 512   //片固件大小    
#define  SERVER_PICK_SIZE          20480 //分固件大小
static u32 tihs_time_SERVER_PICK_SIZE=0;//本次分件大小
static u32 last_total_size=0;           //最后服务器固件切片大小
static u32 firmware_total_size=0;       //总固件大小
static u32 save_byete_counter=0;        //累计存储字节数
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
static u32  pfile=0;//固件字节指针位置
extern QUEUE  usartRecvQueue;//串口数据接收队列
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
DATA_STATE_20,   //空格
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
功能描述：启动硬件复位
*************************************/
void  _4G_OTA_machine_star(void)
{
       ota_connect_state=CONNECT_OTA_RESETING;
       server_big_pick_counter=0;
       POWERED_DOWN_read_count=0;
       http_get_timer=0;
}
/************************************
功能描述：启动服务器固件下载配置
*************************************/
void  _4G_OTA_machine_contextid(void) 
    {   

  // send_AT_Command_machine_star("AT+QHTTPCFG=\"contextid\",1 \r\n", strlen("AT+QHTTPCFG=\"contextid\",1 \r\n"), "OK\r\n", 20, 0);
     congfig_delay_timer=Timer_GetTickCount();
     ota_connect_state=CONNECT_OTA_AT_QHTTPCFG_CINFIG;    
       
    } 
/************************************
功能描述：查询固件下载完否
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
功能描述：固件下载处理
*************************************/
void _4G_OTA_machine(void) 
{  

     switch(ota_connect_state)
    {
      case CONNECT_OTA_STATE_IDLE:
            break;
       case CONNECT_OTA_RESETING:
            resetNbModule();//模块复位
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
                            printf("__________________模块重上电___________________\n");
                           break;
                     }
                    else if( strstr((const char *) stringBuf, "RDY"))
                    {
                              congfig_delay_timer=Timer_GetTickCount();
                              ota_connect_state=CONNECT_OTA_AT_QHTTPCFG_CINFIG; 
                    }
                    else
                    {     //未读到
                         if(POWERED_DOWN_read_count<2) 
                         { //重读                   
                                ota_connect_state= CONNECT_OTA_READY;
                          break;
                         }
                         else
                         {//直接走下去
                             POWERED_DOWN_read_count=0;
                         }
                      }
                   }
              }
       
             break;
        case CONNECT_OTA_AT_QHTTPCFG_CINFIG:
             if(Timer_PassedDelay(congfig_delay_timer,400))  //OTA 切过来的时候要等300mS 以上
             {
                   send_AT_Command_machine_star("AT+QMTCLOSE=0\r\n",strlen("AT+QMTCLOSE=0\r\n"),"OK", 5, 1);//+QMTCLOSE: <client_idx>,<result>+QMTCLOSE: 0,0
                   ota_connect_state= CONNECT_OTA_AT_QMTT_CLOSE;
             }
             break;
             
         case CONNECT_OTA_AT_QMTT_CLOSE:
              if(send_AT_Command_machine_finish()==TRUE)   
              {    
                    //关闭QMT，切到OTA模式
                    set_gateway_state_idle() ;//置位通信静默状态  
                    SET_NB_STAT_EPOWER_DOWN();//重置URC处理状态 
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
                // send_AT_Command_machine_star("http://47.120.15.220:888/downloads/cat1.bin\r\n", strlen("http://47.120.15.220:888/downloads/cat1.bin\r\n"), "OK",20, 0);      //不要删除
                //  send_AT_Command_machine_star("http://47.120.15.220:888/downloads/cat120250401162230.bin\r\n", strlen("http://47.120.15.220:888/downloads/cat120250401162230.bin\r\n"), "OK",20, 0);   
                   ota_connect_state=CONNECT_OTA_AT_QFDEL;   
             }                 
              break;                
               
          case CONNECT_OTA_AT_QFDEL :
              if(send_AT_Command_machine_finish()==TRUE)    
              {
                   send_AT_Command_machine_star("AT+QFDEL=\"*\"\r\n",strlen("AT+QFDEL=\"*\"\r\n"), "OK",20, 0);//下载前清除旧固件,腾出空间
                   ota_connect_state=CONNECT_OTA_AT_QHTTPGET;  
              }  
              //此处没有 break; 不要加 break  
             
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
                send_AT_Command_machine_star(common_send_buff,strlen(common_send_buff),"OK",100, 1);//收到+QHTTPREADFILE: 0  需要500ms
             // send_AT_Command_machine_star("AT+QHTTPREADFILE=\"UFS:cat1.bin\",80 \r\n",strlen("AT+QHTTPREADFILE=\"UFS:cat1.bin\",80 \r\n"),"OK",100, 1);//收到+QHTTPREADFILE: 0  需要500ms   //不要删除
             // send_AT_Command_machine_star("AT+QHTTPREADFILE=\"UFS:cat120250401162230.bin\",80 \r\n",strlen("AT+QHTTPREADFILE=\"UFS:cat120250401162230.bin\",80 \r\n"),"OK",100, 1);//收到+QHTTPREADFILE: 0  需要500ms   //不要删除
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
                      // printf_buf(stringBuf,recvLength);//这里会输出固件信息
                        if( strstr((const char *) stringBuf, "+QHTTPREADFILE: 0"))
                        {  //调试完后改为“+QHTTPREADFILE: 0 ”
                                ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH; 
                                mcu_copy_firmware_star();
                                set_gateway_state_idle();//置位通信静默状态
                                printf("_____________通信模块收到固件__________\n");
                                printf("_______________MCU开始OTA_______________n");
                                break;
                        }
                        else
                        {
                          memset(stringBuf,0x00,recvLength);//不清空会重复读相同的内容
                          recvLength=0;
                        }    
                     }          
                } // 等待成功储存响应消息<80S
                else
                {
                     printf("__________________模块OTA存储失败___________________\n");
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
功能描述：启动进度上报
*************************************/ 
 void ota_progress_report_statr(void)
 {
   ota_progress_state=OTA_PROGRESS_REPORT_CONFIG;
 }
 
/************************************
功能描述：查询进度上报完否
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
功能描述：上传固件进度
注意：    只能在OTA关闭时上传
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
                OTA_ENABLE=0;//关闭OTA，切到MQTT
                 _4G_configModule_star_from_onestate( CONNECT_CONFIG_AT_qmtping) ;//从某一个状态开始启动,发送MQTT开启指令   
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
                  wait_openota_timer =Timer_GetTickCount();//获取时间点，进度上报需要耗时100mS，
                  ota_progress_state=OTA_PROGRESS_AT_REPORT_CLOSE;               
               }
                 break;
            
        case   OTA_PROGRESS_AT_REPORT_CLOSE:  
               if (Timer_PassedDelay(wait_openota_timer, 200))    //等200MS，数据发完；正常进度上报耗时约100mS，时间不够时 AT+QMTCLOSE=0 可能插入到 PPPP 数据中
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
功能描述：转移固件成功查询
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
功能描述：接收固件状态查询
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
功能描述：开始转移固件
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
功能描述：异或计算校验和+
*************************************/
 #include <stdint.h>
// 计算 16 位校验和
 u16 bak_frash_checksum_XOR(u32 size) 
{
    u16 checksum = 0;
    u8 *data;
    data=( u8 *)(OTABAKROM_STARTADDR );
    // 如果数据长度为奇数
    if (size % 2 != 0)
    {
        // 将最后一个字符设置为高 8 位,低 8 位设置为 0
        checksum = data[size - 1] << 8;
        size--;
    }

    // 执行 16 位 XOR 运算
    for (u32 i = 0; i < size; i += 2) 
    {
        uint16_t word = (data[i] << 8) | data[i + 1];
        checksum ^= word;
    }
     printf("XORSUM=0x%02x\n", checksum); 
    return checksum;
}
 

 /************************************
功能描述：检测备份区的数据是否正确，确认程序的完整性。
输入参数：无
输出返回：正确返回 BOOL_TRUE，校验不通过返回 BOOL_FALSE

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
功能描述：计算程序写烧后的校验值
输入参数：数据的大小，4字节数。
输出返回：校验值
*************************************/
u32 user_frash_checksum(u32 size)
{
    u32 tmp;
    u32 i,sum = 0;
    for(i=0; i<size; i++)
    {
        if(i<ADDR_CHECKSUM_OFFSET/4 || i>=(ADDR_SIZE_OFFSET+4)/4)
        {
            tmp = *((__IO u32 *)OTABAKROM_STARTADDR + i);   //校验备份区
            sum += sum32(tmp);
        }
    }
      printf("check_sum=0x%x\n", sum);
    return(sum);  
}


/************************************
功能描述：检测应用区的数据是否正确，确认程序的完整性。
输入参数：无
输出返回：正确返回 BOOL_TRUE，校验不通过返回 BOOL_FALSE

*************************************/
boolean_en get_checksum_status(void)
{
    u32 sum, size;
    sum = *((__IO u32 *)OTABAKROM_STARTADDR + ADDR_CHECKSUM_OFFSET/4);   
    size = *((__IO u32 *)OTABAKROM_STARTADDR + ADDR_SIZE_OFFSET/4);
    size = size & 0xffffff;  //将高多余位清零
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
功能描述：固件转移处理
*************************************/
 
void  mcu_copy_firmware_machine(void)     
{  
    upload_ota_progress_fsm_process();
   switch(MCU_OTA_state)            
   {        
        case  MCU_OTA_STATE_IDLE:
            
                break;
               
        case  MCU_OTA_STATE_RESETING:
               //启动转移固件到MCU 
                 
                sprintf(common_send_buff,"+QFLST: \"UFS:%s\"",firm_name_buffer );
                printf("firm_name_buffer3=%s\n",common_send_buff);
                send_AT_Command_machine_star("AT+QFLST\r\n",strlen("AT+QFLST\r\n"),common_send_buff,20,1);     // 要提取文件大小     插入固件名字符串
           //   send_AT_Command_machine_star("AT+QFLST\r\n",strlen("AT+QFLST\r\n"),"+QFLST: \"UFS:cat1.bin\"",20,1);     // 要提取文件大小           //不要删除
             // send_AT_Command_machine_star("AT+QFLST\r\n",strlen("AT+QFLST\r\n"),"+QFLST: \"UFS:cat120250401162230.bin\"",20,1);     // 要提取文件大小           //不要删除
                 printf("_STROE_FINISH_OTA\n "); //+QFLST: "UFS:cat1.bin",24136
                 MCU_OTA_state=MCU_OTA_STATE_GETFILESIZE;              
       break;      
 

       case MCU_OTA_STATE_GETFILESIZE:
           if(send_AT_Command_machine_finish()==TRUE)  
            {/*
                  if (readLine(stringBuf, &recvLength, 0))
                    {
                        //提取文件名后续处理  
                        
                      text=strstr((const char *) stringBuf, "+QFLST: \"UFS:cat1.bin\",");
                          printf("_字符位置=%s\n",text);
                       if(text!=NULL){   //提取文件大小 
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
                           printf("______________firmware_size=未进入\r\n");
                       
                       }

                    }*/
                printf("----MCU_OTA_state=%d,=%d\n",MCU_OTA_state,MCU_OTA_state==MCU_OTA_STATE_QFDWL_GET_FIRMWARE);
                static char common_send_buff_2[64]; 
                sprintf(common_send_buff,"AT+QFDWL=\"%s\"\r\n",firm_name_buffer );
                sprintf(common_send_buff_2,"AT+QFDWL=\"%s\"",firm_name_buffer );
                printf("firm_name_buffer4=%s\n",common_send_buff);             
                send_AT_Command_machine_star(common_send_buff,strlen(common_send_buff),common_send_buff_2,255,1); //+QFDWL: 152937,0a31 "CONNECT\r\n"  //???
            //  send_AT_Command_machine_star("AT+QFDWL=\"cat1.bin\"\r\n",strlen("AT+QFDWL=\"cat1.bin\"\r\n"),"AT+QFDWL=\"cat1.bin\"",255,1); //+QFDWL: 152937,0a31 "CONNECT\r\n"  //不要删除
            //  send_AT_Command_machine_star("AT+QFDWL=\"cat120250401162230.bin\"\r\n",strlen("AT+QFDWL=\"cat120250401162230.bin\"\r\n"),"AT+QFDWL=\"cat120250401162230.bin\"",255,1); //+QFDWL: 152937,0a31 "CONNECT\r\n"  //不要删除
                     MCU_OTA_state=MCU_OTA_STATE_QFDWL;
                }
                break;
                
          case  MCU_OTA_STATE_QFDWL :
              if(send_AT_Command_machine_finish()==TRUE)  
               { 
                    flushQueue(&usartRecvQueue);
                    memset(stringBuf,0x00,recvLength);//不清空会多耗点时间进出缓冲 
                    recvLength=0;//
                    printf("----MCU_OTA_state=%d,=%d\n",MCU_OTA_state,MCU_OTA_state==MCU_OTA_STATE_QFDWL_GET_FIRMWARE);
                    MCU_OTA_state=MCU_OTA_STATE_QFDWL_GET_FIRMWARE;
                    data_state=DATA_STATE_IDLE;//打开序列检索 
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
                                          if(dat==0x20)//空格
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
                                             if(recvLength>21)  // 数据过长时考虑
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
                                           if( hexStrToByte((const char *)recvURC,4,buf, &chec_size) )   //4字节字符checsum转成两字节
                                           {   
                                              checsum_temp=buf[0]<<8|buf[1];//获取校验值
                                              printf("recvURC=%d\n",chec_size);
                                              printf("recvURC=%02x\n",buf[0]);
                                              printf("recvURC=%02x\n",buf[1]);                                               
                                           }
                                            //提取长度和校验值  
                                            tihs_time_SERVER_PICK_SIZE=leng_temp;//本次分固件大小
                                            if(leng_temp<SERVER_PICK_SIZE)//零头帧000000000
                                            {
                                                last_server_big_pick=1; // 最后一帧标志
                                                last_total_size=leng_temp;
                                                firmware_total_size+=leng_temp;
                                                SERVER_CHECSUM^=checsum_temp;
                                                 printf("total_temp=0x%08x\n",SERVER_CHECSUM); 
                                                if( get_checksum_status_XOR( SERVER_CHECSUM, firmware_total_size)  )
                                                { 
                                                    printf("OTA_OK___2\n");            
                                                }
                                            }  
                                            else if(leng_temp==SERVER_PICK_SIZE)//整帧
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
                                             //大于SERVER_PICK_SIZE错误如何跳转？----------------//重新下载？---------------
                                                
                                            }
                                             leng_temp=0;
                                             memset(stringBuf,0x00,600);//不清空会重复读相同的内容
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
                  send_AT_Command_machine_star(common_send_buff,strlen(common_send_buff),"+QFOPEN:",20,1);   //获取文件指针+QFOPEN: 1
             // send_AT_Command_machine_star("AT+QFOPEN=\"cat1.bin\",2\r\n",strlen("AT+QFOPEN=\"cat1.bin\",2\r\n"),"+QFOPEN:",20,1);   //获取文件指针+QFOPEN: 1
            //   send_AT_Command_machine_star("AT+QFOPEN=\"cat120250401162230.bin\",2\r\n",strlen("AT+QFOPEN=\"cat120250401162230.bin\",2\r\n"),"+QFOPEN:",20,1);   //获取文件指针+QFOPEN: 1
                  pfile=0;//固件字节初始位置
                  MCU_OTA_state=MCU_OTA_AT_QFOPEN;           
              break;
    
        case MCU_OTA_AT_QFOPEN:
             if(send_AT_Command_machine_finish()==TRUE )
             {
                 //分片
                 static   char common_temp[32] ="AT+QFSEEK=1,%u,0\r\n";   

                 sprintf(common_send_buff,common_temp,pfile );                  
                 send_AT_Command_machine_star(common_send_buff,strlen(common_send_buff),"OK",20,1);   //设置文件指针为文件的初始位置。 
                 MCU_OTA_state=MCU_OTA_AT_QFSEEK;               
             }  
            break;      
             
       case MCU_OTA_AT_QFSEEK:
             if(send_AT_Command_machine_finish()==TRUE)
             {   
               send_AT_Command_machine_star("AT+QFREAD=1,512\r\n",strlen("AT+QFREAD=1,512\r\n"),"CONNECT ",20,1);  //读取数据。"CONNECT 512\r\n"
               wait_data_timer =Timer_GetTickCount();
               MCU_OTA_state=MCU_OTA_AT_QFREAD;   
             }                 
             break;                       
        case MCU_OTA_AT_QFREAD:                        //读取数据。
             if(send_AT_Command_machine_finish()==TRUE)
             { 
                      if(have_get_pack_length==0)
                      { 
                          if( strstr((const char *) stringBuf, "CONNECT"))
                           {  
                                static  u16 pack_length_temp=0;
                                pack_buf=stringBuf;   //stringBuf内容：AT+QFREAD=1,512\r\nCONNECT 512\r\n
                             // printf("CONNECTpack_buf=%s\n",pack_buf);
                                pack_buf+=26;   
                                printf("CONNECTpack_buf=%s\n",pack_buf);
                                while (*pack_buf !='\r' )//这里以\r判断结尾有时会数据过大，比如512会变成51255，只好用pack_length_temp>PICK_SIZE来截取
                               {
                                    pack_length_temp =pack_length_temp * 10 + *pack_buf - '0';
                                    pack_buf++; 
                                 if(pack_length_temp>PICK_SIZE )
                                  {
                                      printf("__获取值过大=%d\r\n",pack_length_temp);
                                      pack_length_temp=0;
                                      break;
                                  }
                               } 
                               pack_length= pack_length_temp;
                               pack_length_temp=0;
                               have_get_pack_length=1;
                               printf("__获取到CONNECT=%d\r\n",pack_length);    
                          }
                          else
                          {
                              printf("__未获取到CONNECT_\n");  
                          } 
                       }
             
                 if (Timer_PassedDelay(wait_data_timer, 400))    //等,400MS 数据接收完 ,太少接收不完整或错误
                 {    
                     MCU_OTA_state=MCU_OTA_MCU_GETDATA; 
                     have_get_pack_length=0;
                 }  
                 if (Timer_PassedDelay(wait_data_timer, 300)) 
                 {      //无数据超时              *************待处理死循环*********
                   //  MCU_OTA_state=MCU_OTA_AT_QFREAD_LOOP; 
                 }
             }  
             break; 
        case MCU_OTA_MCU_GETDATA:        //由接收处理切换
             if(OTA_DATA_IS_READY)       //收不到重发待做
             {
                OTA_DATA_IS_READY=0;
                printf("3MCU存储___________________\n");
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
            if(pfile+PICK_SIZE<tihs_time_SERVER_PICK_SIZE)//小于本次分片数    //  分界线  pfile UFS 文件位置
            {
                  pfile+=PICK_SIZE;
                  static   char common_temp[32] ="AT+QFSEEK=1,%u,0\r\n";   
                //  static   char common_send_buff[64];  
                  sprintf(common_send_buff,common_temp,pfile );                  
                  send_AT_Command_machine_star(common_send_buff,strlen(common_send_buff),"OK",20,1);   //设置文件指针为文件的初始位置。 
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
                  ota_progress_report_statr();//开启上报，再进行下一步
                  MCU_OTA_state=MCU_OTA_GET_BIG_PICK; //  
            }
            break;     
            
       case MCU_OTA_GET_BIG_PICK:
            if(ota_progress_report_is_finish())
            {
                 if( last_server_big_pick)//服务器固件最后一帧?什么时候清零
                 {
                      server_big_pick_counter=0;
                      save_byete_counter=0;
                      pfile=0;//小片指针位置归零
                      MCU_OTA_state=MCU_OTA_AT_QFCLOSE;
                  }
                  else  //
                  {
                      ++server_big_pick_counter;
                        pfile=0; //小片指针位置归零
                        ota_connect_state=CONNECT_OTA_AT_QFDEL;   //去删除模块固件
                        MCU_OTA_state=MCU_OTA_STATE_IDLE;         //等待新的服务器固件切片启动
                  }   
             }
             break;      
             
          case MCU_OTA_AT_QFCLOSE :     //AT+QFCLOSE=1
              if( get_checksum_status_XOR( SERVER_CHECSUM, firmware_total_size)  )//传输层校验                                      ---------- 传输错误没有发现-------------
              { 
                     printf("---------------OTA_XOR_CHECK_OK\n");
                     if( get_checksum_status())  //  应用层固件完整性状态读取
                     {
                           printf("---------------OTA_SUM_CHECK_OK\n");
                           sys_data.sn=0xaa5555aa;//标记有新固件
                           sys_data_store();
                           MCU_OTA_state=MCU_OTA_MCU_FINISH;
                     }
                     else
                     {
                        printf("---------------OTA_SUM_CHECK_ERROR\n");
                        sys_data.sn=3;//标记固件下载错误
                        sys_data_store();
                        changea_to_MQTT_modle();//切到MQTT
                        last_server_big_pick=0;//这个时候清零
                        MCU_OTA_state=MCU_OTA__COMPLETE;
                     }
              }
              else //校验错误
              {
                    printf("------------OTA_XOR_CHECK_ERR\n");
                    sys_data.sn=3;//标记固件下载错误
                    sys_data_store();
                    sbuff= ((u8*)(DATAROM_STARTADDR)) ;//打印
                    printf_buf(sbuff,64);
                    sbuff= ((u8*)(BAKDATAROM_STARTADDR)) ;//打印
                    printf_buf(sbuff,64);
                    changea_to_MQTT_modle();//切到MQTT
                    last_server_big_pick=0;//这个时候清零
                    MCU_OTA_state=MCU_OTA__COMPLETE;
              }
               break; 
              
          case   MCU_OTA_MCU_FINISH:
                    printf("---------OTA 完毕------\n");
                    printf("---------系统重启-------\n");
                    last_server_big_pick=0;//清零标志，防止跳转失败后影响下次OTA
                    server_big_pick_counter=0;
                    save_byete_counter=0;
                    firmware_total_size=0;
                    SERVER_CHECSUM=0;
                    sbuff= ((u8*)(DATAROM_STARTADDR)) ;
                    printf_buf(sbuff,64);
                    sbuff= ((u8*)(DATAROM_STARTADDR)) ;
                    printf_buf(sbuff,64);
                    HAL_Delay(50);
                    //调到boot区
                    extern void iap_jump2boot(void);  //-------------------如果没有打印数据延时的话会影响BOOT的跳转
                    MCU_OTA_state=MCU_OTA__COMPLETE;//这个地方很重要，一定要放在iap_jump2boot()函数之前，否则跳转失败
                    iap_jump2boot();                   
      break;   
          
      case MCU_OTA__COMPLETE:
      break;  
      
      default:    
      break;    
    }
}

/**
*@brief   存储OTA模块上报的固件流
*@param	  buf：模块上报的固件
*@param	  lenth：固件长度
*@return  无
*/
 void OTA_STROE_MCU(uint8 *buf,u16 lenth)
{                //解析结果
     uint16 dataLength;
      dataLength=lenth;
     if(dataLength==PICK_SIZE|| (last_server_big_pick && dataLength==(u16)(last_total_size%PICK_SIZE)))
      {  //拦截"OK\r\n"
            // printf("_________________________________save_byete_counterbingin=%u\n",save_byete_counter);
             flash_store(buf, dataLength, OTABAKROM_STARTADDR+save_byete_counter );  //-strlen("CONNECT 512\r\n")
             save_byete_counter+=dataLength;//累计字节，地址加
              printf("_________________________________save_byete_counter=%u\n",save_byete_counter);
             MCU_OTA_state=MCU_OTA_AT_QFREAD_LOOP;
             sbuff= ((u8*)(OTABAKROM_STARTADDR+save_byete_counter)) ;
             // printf_buf(sbuff-256,64);
             printf("1MCU存储1\n");
             OTA_DATA_IS_READY=1;  //提取payload成功
             if(dataLength==(u16)(firmware_total_size%PICK_SIZE) &&last_server_big_pick)  //如果最后一片为0，会不会出BUG？
             {
                OTA_DATA_IS_finish=1;
             }
        }
}


