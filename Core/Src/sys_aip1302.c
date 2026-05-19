/*************************************************************
程序功能：外部RTC
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "sys_aip1302.h"
#include "aip1302.h"

RtcTime_t  apprtc_RtcTime ;

ds1302_t ds1302;

 u1t _HexToBcd(u1t hex)
{
  u1t bcd;
  bcd = (hex / 10) << 4;
  bcd |= hex % 10;
  return(bcd);
}
static u1t _BcdToHex(u1t BCD)
{
  u1t k1;
  u1t k2;
  k1 = ((BCD >> 4) & 0xf) * 10;
  k2 = BCD & 0x0f;
  return(k1 + k2);
}

/*
从年月日中计算出周, 0~6分别表示星期日~六, year:0000~9999年
*/
u1t GetWeek(u2t year, u1t mon, u1t day)
{
  s2t c;
  s2t y;  
  s2t x;
  if (mon <= 2)
  {
    mon += 12;
    year--;
  }
  c = year/100;
  y = year%100;
  /*
  W = [C/4] - 2C + y + [y/4] + [13 * (M+1) / 5] + d - 1．                (蔡勒公式)  
  */  
  x = (c/4) - (2*c) + y + (y / 4) + ((13 * (mon + 1)) / 5) + day - 1;
  if (x < 0)
  {
    x = x + 1316;   /* x = (((0-x)/7)+1)*7+x; 常规公式, 1316=((655*2)/7+1)*7   */    
  }
  return(x % 7);
}

static u1t _RtcWeekToDs1302Week(u2t year, u1t mon, u1t day)
{
  return (u1t)(GetWeek(year, mon, day) + 1U);
}

void SetTime(void)
{
    ds1302.year = _HexToBcd(apprtc_RtcTime.year-2000);
    ds1302.mon = _HexToBcd(apprtc_RtcTime.mon);
    ds1302.day = _HexToBcd(apprtc_RtcTime.day);
    apprtc_RtcTime.week = _RtcWeekToDs1302Week(apprtc_RtcTime.year, apprtc_RtcTime.mon, apprtc_RtcTime.day);
    ds1302.week = _HexToBcd(apprtc_RtcTime.week);
    ds1302.hour = _HexToBcd(apprtc_RtcTime.hour);
    ds1302.min = _HexToBcd(apprtc_RtcTime.min);
    ds1302.sec = _HexToBcd(apprtc_RtcTime.sec);
    printf_buf2("set time", (u8*)&ds1302, 7);
    Ds1302_write_time((u8*)&ds1302);
}


static u16 _timer=300;
static u8 _timer_for_charge=12;

void sys_aip1302_timer(void)
{
    if(_timer > 0)
    {
        --_timer;
    }
    if(_timer_for_charge > 0)
    {
        --_timer_for_charge;
        if(_timer_for_charge == 0)
        {
        }
    }
//    ++_timer;
//    if(_timer == 100)
//    {
//        _timer = 0;
//    }

}


u8 date_error_counter=0;
u8 time_error_counter=0;

void sys_aip1302_process(void)
{
    u8 tmp;
    RtcTime_t  rtc ;
    boolean_en ret_date = BOOL_TRUE;
    boolean_en ret_time = BOOL_TRUE;
    memset(&rtc, 0, sizeof(rtc));
    if(_timer == 0)
    {
        _timer=50;
//            apprtc_RtcTime.year = 2024;
//            apprtc_RtcTime.mon = 8;
//            apprtc_RtcTime.day = 10;
//            apprtc_RtcTime.hour =16;
//            apprtc_RtcTime.min = 50;
//            apprtc_RtcTime.sec = 3;
//            SetTime();
        Ds1302_read_time((u1t*)&ds1302);
        
        tmp = _BcdToHex(ds1302.year);
        
            
        if(tmp<=99)
        {
            rtc.year = tmp+2000;            
        }
        else
        {
            printf("---------------year_err------------");
            printf("rxdatetime1   y=%d,m=%d,d=%d,w=%d,h=%d,m=%d,s=%d\n", tmp,apprtc_RtcTime.mon,apprtc_RtcTime.day,apprtc_RtcTime.week,apprtc_RtcTime.hour,apprtc_RtcTime.min,apprtc_RtcTime.sec);
             ret_date = BOOL_FALSE;
        }
        
      
        if(ret_date)
        {
            tmp = _BcdToHex(ds1302.mon);
            if(tmp>=1 && tmp<=12)
            {
                rtc.mon = tmp;            
            }
            else
            {
                printf("---------------mon_err------------"); 
                printf("rxdatetime1   y=%d,m=%d,d=%d,w=%d,h=%d,m=%d,s=%d\n", apprtc_RtcTime.year,tmp,apprtc_RtcTime.day,apprtc_RtcTime.week,apprtc_RtcTime.hour,apprtc_RtcTime.min,apprtc_RtcTime.sec);
                ret_date = BOOL_FALSE;
            }
        }
        if(ret_date)
        {
            tmp = _BcdToHex(ds1302.day);
            if(tmp>=1 && tmp<=31)
            {
                rtc.day = tmp;            
            }
            else
            {
                printf("---------------day_err------------"); 
               printf("rxdatetime1   y=%d,m=%d,d=%d,w=%d,h=%d,m=%d,s=%d\n", apprtc_RtcTime.year,apprtc_RtcTime.mon,tmp,apprtc_RtcTime.week,apprtc_RtcTime.hour,apprtc_RtcTime.min,apprtc_RtcTime.sec); 
                ret_date = BOOL_FALSE;
            }
        }
        if(ret_date)
        {
            tmp = _BcdToHex(ds1302.week);
            if(tmp>=1 && tmp<=7)
            {
                rtc.week = tmp;            
            }
            else
            {
                ret_date = BOOL_FALSE;
                 printf("rxdatetime1   y=%d,m=%d,d=%d,w=%d,h=%d,m=%d,s=%d\n", apprtc_RtcTime.year,apprtc_RtcTime.mon,apprtc_RtcTime.day,tmp,apprtc_RtcTime.hour,apprtc_RtcTime.min,apprtc_RtcTime.sec);
            }
        }
        if(ret_time)
        {
            tmp = _BcdToHex(ds1302.hour);
            if(tmp<=23)
            {
                rtc.hour = tmp;            
            }
            else
            {
                printf("---------------hour_err------------ rtc.hour = %d\r\n",tmp);
                ret_time = BOOL_FALSE;
            }
        }
        if(ret_time)
        {
            tmp = _BcdToHex(ds1302.min);
            if(tmp<=59)
            {
                rtc.min = tmp;            
            }
            else
            {
                    printf("---------------min_err----- rtc.min = %d\r\n",tmp);
                ret_time = BOOL_FALSE;
            }
        }
        if(ret_time)
        {
            tmp = _BcdToHex(ds1302.sec);
            if(tmp<=59)
            {
                rtc.sec = tmp;            
            }
            else
            {
                printf("---------------sec_err------- rtc.sec = %d\r\n",tmp);
                ret_time = BOOL_FALSE;
            }
        }
        if(ret_date && ret_time)
        { //printf("rxdatetime");
           //  printf("rxdatetime1   y=%d,m=%d,d=%d,w=%d,h=%d,m=%d,s=%d\n", apprtc_RtcTime.year,apprtc_RtcTime.mon,apprtc_RtcTime.day,apprtc_RtcTime.week,apprtc_RtcTime.hour,apprtc_RtcTime.min,apprtc_RtcTime.sec);
            rtc.ready = BOOL_TRUE;
            apprtc_RtcTime = rtc;
            date_error_counter = 0;
            time_error_counter = 0;
         //   printf("rxdatetime2   y=%d,m=%d,d=%d,w=%d,h=%d,m=%d,s=%d\n", apprtc_RtcTime.year,apprtc_RtcTime.mon,apprtc_RtcTime.day,apprtc_RtcTime.week,apprtc_RtcTime.hour,apprtc_RtcTime.min,apprtc_RtcTime.sec);
        }

        if(ret_date==BOOL_FALSE)
        {
           
            apprtc_RtcTime.ready = BOOL_FALSE;
            if(date_error_counter<0xff)
            {
              
                ++date_error_counter;
            }
        }
        if(ret_time==BOOL_FALSE)
        {
            
            apprtc_RtcTime.ready = BOOL_FALSE;
            if(time_error_counter<0xff)
            {
          
                ++time_error_counter;
            }
        }
        if(date_error_counter>=10)
        {
            apprtc_RtcTime.year = 2020;
            apprtc_RtcTime.mon = 1;
            apprtc_RtcTime.day = 1;    
            SetTime();
            date_error_counter = 0;
        }
        if(time_error_counter>=10)
        {
            apprtc_RtcTime.hour =0;
            apprtc_RtcTime.min = 0;
            apprtc_RtcTime.sec = 0;
            SetTime();
            time_error_counter = 0;
        }
     // printf("/time   y=%d,m=%d,d=%d,w=%d,h=%d,m=%d,s=%d\n", apprtc_RtcTime.year,apprtc_RtcTime.mon,apprtc_RtcTime.day,apprtc_RtcTime.week,apprtc_RtcTime.hour,apprtc_RtcTime.min,apprtc_RtcTime.sec);
    }
    
    

    
    
    
    
    
    
}


void sys_aip1302_init(void)
{
    apprtc_RtcTime.ready = BOOL_FALSE;  //标记为未读取有效数据
}

