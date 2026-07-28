#include "ntc.h"
//#include "hw_adc.h"
#include "common.h"

//#include "hal/adc.h"
#include "adc.h"
#define NTC1_PULL_UP

#define ADC_MAX             4095//1024
#define R_PART              300//  //外置传感器是3k  上拉，缩小10倍
#define R_PART2             1000         //外置传感器是10k  上拉，缩小10倍
#define DEGREE_TEMP         10           // 10 倍
#define LIMIT_DOWN_TEMP     (s16)(-400)  //-50度
#define LIMIT_UP_TEMP       1250         //120度

 u8 flag_adc_ntc1;
 u8 flag_adc_ntc2;
static ntcTemp_t NtcTcmp(u32 rntc);

/*
 负温度系数NTC热敏电阻(25℃)电阻值(标准值)=100K B常数(25/85℃)=3950.根据公式的标准值。各厂商可能有实际的偏差

*/

/* -40c~125c 单位:10欧*/
static const u32 c_ntcTable[171]=
{
  /* -40 ~ -31 */
  322555 ,302333 ,283499 ,265948 ,249587 ,234329 ,220092 ,206804 ,194396 ,182804 ,
  /* -30 ~ -21 */
  171970 ,161842 ,152369 ,143505 ,135207 ,127438 ,120159 ,113338 ,106943 ,100946 ,
  /* -20 ~ -11 */
  95319 ,90037 ,85079 ,80421 ,76045 ,71932 ,68064 ,64427 ,61004 ,57782 ,
  /* -10 ~ -1 */
  54748 ,51890 ,49198 ,46660 ,44267 ,42011 ,39881 ,37872 ,35974 ,34183 ,
  /* 0 ~ 9 */
  32490 ,30890 ,29378 ,27948 ,26596 ,25316 ,24105 ,22958 ,21872 ,20844 ,
  /* 10 ~ 19 */
  19869 ,18945 ,18069 ,17238 ,16450 ,15702 ,14991 ,14317 ,13677 ,13069 ,
  /* 20 ~ 29 */
  12491 ,11941 ,11419 ,10922 ,10450 ,10000 ,9572 ,9165 ,8777 ,8407 ,
  /* 30 ~ 39 */
  8055 ,7720 ,7400 ,7095 ,6804 ,6527 ,6262 ,6009 ,5768 ,5538 ,
  /* 40 ~ 49 */
  5318 ,5108 ,4907 ,4716 ,4532 ,4357 ,4189 ,4029 ,3876 ,3729 ,
  /* 50 ~ 59 */
  3588 ,3454 ,3325 ,3201 ,3083 ,2970 ,2861 ,2757 ,2658 ,2562 ,
  /* 60 ~ 69 */
  2470 ,2382 ,2298 ,2217 ,2139 ,2064 ,1992 ,1923 ,1857 ,1794 ,
  /* 70 ~ 79 */
  1733 ,1674 ,1617 ,1563 ,1511 ,1461 ,1412 ,1366 ,1321 ,1278 ,
  /* 80 ~ 89 */
  1236 ,1197 ,1158 ,1121 ,1085 ,1051 ,1018 ,986 ,955 ,925 ,
  /* 90 ~ 99 */
  897 ,869 ,842 ,817 ,792 ,768 ,745 ,723 ,701 ,680 ,
  /* 100 ~ 109 */
  660 ,641 ,622 ,604 ,586 ,569 ,553 ,537 ,522 ,507 ,
  /* 110 ~ 119 */
  493 ,479 ,466 ,453 ,440 ,428 ,416 ,405 ,394 ,383 ,
  /* 120 ~ 125 */
  373 ,363 ,353 ,344 ,335 ,326
};

static s16  _ntcTemp;
u32  rPart;      /* 分压电阻 */
ntcTemp_t  Ntctemp;    
ntcTemp_t  Ntctemp2;    

enum ntc_state
{
	MATCH=0,   //等于返回0, c_ntcTable[index] == r
  JUST_GREATER_THAN,//刚好大于返回1, c_ntcTable[index] > r , c_ntcTable[index+1] < r
  GREATER_THAN_MAXIMUM,//大于最大值返回2,         r > c_ntcTable[0]
  LESS_THAN_THE_MINIMUM//小于最小值返回3,        r < c_ntcTable[xCardCount-1]	
} ;             

/* 二分法查找 */
enum ntc_state find_data(u32 r, u16* index)
{
  enum ntc_state state;
  u16 start_index, mid_index, end_index;
  u16 xCardCount=sizeof(c_ntcTable)/4;
  start_index = 0;
  end_index = xCardCount - 1;
  if (r>c_ntcTable[0])
  {
    *index = 0;
    state=GREATER_THAN_MAXIMUM;
  }
  else if (r<c_ntcTable[end_index])
  {
    *index = end_index;
    state=GREATER_THAN_MAXIMUM;
  }
  else
  {
    while (end_index >= start_index)
    {
      mid_index = ((u32)start_index + end_index) / 2;
      if (c_ntcTable[mid_index] < r)
      {
        if (mid_index > 0)
        {
          end_index = mid_index - 1;
        }
        else
        {
          break;
        }
      }
      else if (c_ntcTable[mid_index] > r)
      {
        start_index = mid_index + 1;
      }
      else
      {
        *index = mid_index;
        return(MATCH);
      }
    }
    *index = end_index;
    state=JUST_GREATER_THAN;
  }
  return state;
}

static ntcTemp_t NtcTcmp(u32 rntc)
{
  enum ntc_state state;
  u16 index;
  ntcTemp_t temp;
  state=find_data(rntc,&index);
  if (state==MATCH)
  {
    temp.Ntctemp = (s16)index*(s16)DEGREE_TEMP + LIMIT_DOWN_TEMP;
    temp.state = NTC_NORMAL;
    return (temp);
  }
  else if (state==GREATER_THAN_MAXIMUM)
  {
    temp.Ntctemp = LIMIT_DOWN_TEMP;
    temp.state = NTC_LOW;
    return (temp);
  }
  else if (state==LESS_THAN_THE_MINIMUM)
  {
    temp.Ntctemp = LIMIT_UP_TEMP;    
    temp.state = NTC_OVERFLOW;
    return (temp);
  }
  else
  {
    temp.Ntctemp = (s16)index*(s16)DEGREE_TEMP + LIMIT_DOWN_TEMP;
    temp.Ntctemp = temp.Ntctemp + (s16)(((c_ntcTable[index]-rntc)*DEGREE_TEMP)/(c_ntcTable[index] - c_ntcTable[index+1]));
  }
  if (_ntcTemp > temp.Ntctemp && temp.Ntctemp > LIMIT_DOWN_TEMP)
  {
    ++temp.Ntctemp;
  }
  _ntcTemp = temp.Ntctemp;
  temp.state = NTC_NORMAL;

  return (temp);
}

void NtcTempCalc1()
{
  u16 adc;
  u32 rNtc;
  rPart = R_PART;
  if (flag_adc_ntc1)
  {
    flag_adc_ntc1=0;
    
 adc=adc_average[ADC_CH08_NTC];// hal_adc_get_value(1);       //原adc=adc_get_value(ADC_CH9_NTC1);
    //printf("adc=%d\n",adc);
    if (adc==ADC_MAX)
    {
      --adc;
    }
#ifdef NTC1_PULL_UP    
    rNtc = ((u32)ADC_MAX*rPart)/adc - rPart;    
#else
    rNtc = ((u32)adc*rPart)/(ADC_MAX-adc);    
#endif
    Ntctemp = NtcTcmp(rNtc); 
    //  printf("temp=%d\n",Ntctemp.Ntctemp);
  }
}

//外置的
void NtcTempCalc2()
{
  u16 adc;
  u32 rNtc;
  rPart = R_PART2;
  if (flag_adc_ntc2)
  {
    flag_adc_ntc2=0;

   adc=ADC_Value1;// hal_adc_get_value(1); //adc_get_value(ADC_CH8_NTC2);
    if (adc==ADC_MAX)
    {
      --adc;
    }
#ifdef NTC1_PULL_UP    
    rNtc = ((u32)ADC_MAX*rPart)/adc - rPart;    
#else
    rNtc = ((u32)adc*rPart)/(ADC_MAX-adc);    
#endif
    Ntctemp2 = NtcTcmp(rNtc);    
      //printf("temp2=%d\n",Ntctemp2.Ntctemp);
  }
}




static u8  _timer = 0;
void ntc_timer(void)
{
    ++_timer;
    if(_timer == 100)
    {
        _timer = 0;
        flag_adc_ntc1 = 1;
        flag_adc_ntc2 = 1;
    }
}

