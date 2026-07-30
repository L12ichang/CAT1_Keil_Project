/*************************************************************
程序功能：hw_gateway
开发环境：keil 5.37 
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
/* 当前CAT.1 AT收发状态机仍依赖本模块；仅保留4G/MQTT链路相关逻辑。 */
#include "hw_gateway.h"
#include "NbDriver.h"
#include "nb_at_legacy_adapter.h"
#include "TcpClient.h"
#include "Portable.h"
#include "type.h"
#define TIMEOUT         3               //30 MS内没有收再到数据认为收到了一帧
#define LOGIN_IDLE      0
#define LOGIN_START     1
#define LOGIN_RECV      2
#define LOGIN_REQ       3
#define LOGIN_END       4
extern   boolean_en  _4g_reset_finish(void) ;
u8 login_all_state=LOGIN_IDLE;
u8 lonin_index=0;
uint32 DeviceId;//DEVICE_ID ;
GATEWAY_STATE  gateway_state;
gateway_rx_state_en  gateway_rx_state=GATEWAY_RX_STATE_NONE;//230924
u8  login_success_flag =0; //用于登录各ID标记//230924
static  u8 _timer_for_packet_rx;
static  u16 rx_count;
static  boolean_en gateway_rx_ready = BOOL_FALSE;
static  u16 timedown=300;
static  uint16 recvLength = 0;//数据接收长度
 u16  loginfirst_timeout_set;
 uint16 login_timeout=0;
 u8 online=0;
void hw_gateway_timer(void)
{
    if(_timer_for_packet_rx > 0)
    {
        --_timer_for_packet_rx;
        if(_timer_for_packet_rx == 0)
        {
            gateway_rx_ready  = BOOL_TRUE;
            recvLength= rx_count;
            printf_buf(stringBuf,recvLength); 
            rx_count=0;
        }
     }
      if(timedown)
     {
     --timedown;
     }
     if(  login_timeout)
     {
         --login_timeout;
     }
     if(loginfirst_timeout_set)
     {
        --loginfirst_timeout_set;
     }
}


void gateway_rx(u8 dat)//接收一帧数据
{
    
    if(rx_count<RECV_BUF_LENGTH)
    {
     stringBuf[rx_count]=dat;     
      ++rx_count;

    }
    _timer_for_packet_rx = TIMEOUT;
    
}

void longin_sucess(void)
{
 login_success_flag =1;
}
void longin_sucess_flag_clear(void)
{
 login_success_flag =0;
}


/************************************
功能描述：定时器  10 ms
输入参数：无
输出返回：无
*************************************/
#define TIMEOUT_PACK       10    //超时用来分包
static u16  _timer_for_net_rx = 0;//230924

void rx1_state_timer(void)
{
    if(_timer_for_net_rx > 0)
    {
        --_timer_for_net_rx;
        if(_timer_for_net_rx == 0)
        {
            gateway_rx_state=GATEWAY_RX_STATE_NONE;         
        }
    } 

  
}

/**
*@brief   232接收
*@return  无
*/
void queue_out_process(void)
{
    u8 dat;
     while (nb_at_legacy_adapter_read_byte(&dat) == BOOL_TRUE)
     {
         if(gateway_rx_state==GATEWAY_RX_STATE_NONE)
        {  
            if(dat == 0x50)
            { 
                printf(" rx50\n");
                _timer_for_net_rx = 30;//TIMEOUT;
                gateway_rx_state = GATEWAY_RX_STATE_NET; 
                gateway_rx(dat);                  
            } 
         }
        else if(gateway_rx_state==GATEWAY_RX_STATE_NET)   
        { 
          _timer_for_net_rx = TIMEOUT_PACK  ;
          gateway_rx(dat);  
            
        }

     }
    
}


/*  //485网关暂时不删
void login_all_driver()
{
    switch(login_all_state)
    {
        case LOGIN_IDLE :
          if(gateway_state==GATEWAY_STATE_LOGIN_ALL_DRIVER)
          { printf("total_number_of_drives=%d\n",total_number_of_drives);
                printf(" login_START\n");
                login_all_state=LOGIN_START;
          
          }
          break;
        
    case  LOGIN_START:
        
          DeviceId=driver_id_scan[lonin_index];
          onNBEvent(NB_EVENT_CONNECTED, 0, 0);//发登录指令
          login_timeout=30;//集中登录超时处理？
           login_all_state=LOGIN_RECV;
        
          break;
    
    case LOGIN_RECV:
         if(login_timeout==0)
         {  printf(" login_timeout\n");
             if(lonin_index<total_number_of_drives)
               {     printf(" total_number_of_drives=%d\n",total_number_of_drives);
                   printf(" login_timeout_contiu\n");
                 ++lonin_index;
                 login_all_state=LOGIN_START;
               }
               else
               {  
                   printf(" login_teimoutEND\n");
                   lonin_index=0;
                   login_all_state= LOGIN_END;
               }
           return;
           }
        if(  gateway_rx_ready  == BOOL_TRUE)
          {
              printf(" login_connected\n");
              gateway_rx_ready=BOOL_FALSE;
      
              login_all_state= LOGIN_REQ;
              //异步处理待加时限设置 --------------------------------------------------------------------------->
              onNBEvent(NB_EVENT_DATA, stringBuf, recvLength); // 解析数据                                         //
                                                                                                                   //
          }                                                                                                        //
          
          break;                                                                                                  //
    case  LOGIN_REQ :                                                                                             //
           
           if(login_success_flag)//异步处理待加时限,防止返回结果进入下一个消息确认的时间窗口 <------------------- //
           {  
               printf(" login_REQ\n");
               login_success_flag=0;
               if(lonin_index<total_number_of_drives)
               {
                 printf(" login_contiu\n");
                 ++lonin_index;
                 login_all_state=LOGIN_START;
               }
               else
               {  
                   printf(" login_END\n");
                   lonin_index=0;
                   login_all_state= LOGIN_END;
               }
               
           }
           break;
           
    case  LOGIN_END:
           {  
               
              total_number_of_drives=0;
           }
     break;
    }
    

}

*/
void recive_flag_MQTT(void)
{ 
    printf("收到用户协议_gateway_state=%d",gateway_state);
    gateway_rx_ready  = BOOL_TRUE;
}


void set_gateway_state_idle(void)
{
    gateway_state=GATEWAY_STATE_IDLE ;
}
void hw_gateway_process(void)//主程序调用-----------------------------------------------------------------------------
{
    static u8 cnt_connect=0;
    static u8 cnt_reset=0;
   #if 0
      queue_out_process();  
    #else
     nbModuleProcess(); 
    #endif
    

        switch(gateway_state)
        {
                 case GATEWAY_STATE_POWER_DOWN:
                //-------------------------------------------------JSON这里了不要     
                 /*
                      if(timedown==0)//上电3秒后开始登录
                      {  // pwr_on();
                       
                           if( _4G_configModule_machine_finish()==BOOL_TRUE)//配置服务器连接成功
                           {
                                 printf(" login_timeout\n");
                                 gateway_state=GATEWAY_STATE_CONNECTING;
                           }
                      }
                */
              //-------------------------------------------------JSON这里了不要        
                        break; 
                      
                 case  GATEWAY_STATE_NOT_CONNECT:

                        break;
                 case  GATEWAY_STATE_CONNECTING:
                        printf(" loging\n");
                
                        gateway_state = GATEWAY_STATE_CONNECTED;
                 
                        onNBEvent(NB_EVENT_CONNECTED, 0, 0);  //发送登录请求
                        loginfirst_timeout_set=6000;
                        break;

                 case GATEWAY_STATE_CONNECTED:
                       if(  gateway_rx_ready  == BOOL_TRUE)
                      {
                          printf(" logined\n");
                          gateway_rx_ready=BOOL_FALSE;
                     
                   
                     
                      //待加接收超时设置
                        onNBEvent(NB_EVENT_DATA, stringBuf, recvLength); // 解析数据,查看是否登录成功
#ifdef    _4G_CAT_1                   
                          //++++++++++++++++++++++++
                           if(login_success_flag)
                          {
                             printf(" scan\n");
                             login_success_flag=0;
                              online=1;
                             gateway_state=GATEWAY_STATE_CYCLIC_SCAN_AND_REAPORT_DRIVER_DATA; 
                           //++++++++++++++++++++++++ 
#else      
                            gateway_state= GATEWAY_STATE_LOGIN_WITH_ONE_ID;                          
#endif                      
                          
                           break;   
                         }
                      }
                       else if(loginfirst_timeout_set==0)  //登录超时
                      {
                          online=0;
                          if(++cnt_connect>3)
                          { 
                              printf(" resetNbModule\n");
                              cnt_connect=0;
                              
                              if(++cnt_reset<2)
                              {
                                   _4G_configModule_machine_star();//重上电  配置模块
                                    gateway_state=GATEWAY_STATE_POWER_DOWN;//重置状态
                              }
                              else
                              {
                                cnt_reset=0;
                              
                                // 多次连接不成功系统重启 
                                printf(" ----------iap_jump2boot()------------\n");   
//                                gateway_state=  GATEWAY_STATE_IDLE;        //调试关闭
//                                extern void soft_reset(void); 
//                               soft_reset();  
                             
                              }
                          }
                          else
                          { 
                            printf(" CONNECTING\n");
                          
                            gateway_state=GATEWAY_STATE_CONNECTING;
                              
                              
                          }
                         // gateway_state=GATEWAY_STATE_CYCLIC_SCAN_AND_REAPORT_DRIVER_DATA;  
                      }
                 
                    break;   
                  
              case  GATEWAY_STATE_LOGIN_WITH_ONE_ID :       
                    if(login_success_flag)
                    {
                    
                        login_success_flag=0;
#ifdef  _4G_CAT_1                      
                     gateway_state=GATEWAY_STATE_CYCLIC_SCAN_AND_REAPORT_DRIVER_DATA;                   
#else                      
                     gateway_state=GATEWAY_STATE_SCAN_ALL_DRIVER  ;
#endif
                        
                   }

                    break;
                  
            case  GATEWAY_STATE_SCAN_ALL_DRIVER :
              //  若使用网关请取消屏蔽   if( scan_state==SCAN_END)
                   { 
                      printf(" scan_end\n");
                      gateway_state=GATEWAY_STATE_LOGIN_ALL_DRIVER;
                   }
                   break;        

            case   GATEWAY_STATE_LOGIN_ALL_DRIVER:
                    if( login_all_state== LOGIN_END)
                    {  
                       printf(" login_all\n");
                       gateway_state=GATEWAY_STATE_CYCLIC_SCAN_AND_REAPORT_DRIVER_DATA;                   
                    }
                     
                 
                    break;
            
            case  GATEWAY_STATE_CYCLIC_SCAN_AND_REAPORT_DRIVER_DATA :
                  if(  gateway_rx_ready  ==  BOOL_TRUE)
                  {
                      printf(" login_connected\n");
                      gateway_rx_ready=BOOL_FALSE; 
              
                 printf("app_mqtt_rx+%s\n",stringBuf);
                          //待加接收超时设置
                    //-------------------------------------------------JSON这里了不要   
                        onNBEvent(NB_EVENT_DATA, stringBuf, recvLength); // 原解析数据
                    //-------------------------------------------------JSON这里了不要      
                  }
            
                   break;
           case   GATEWAY_STATE_IDLE :
               break;
            default :   
                break;
           }
    // login_all_driver();

}



