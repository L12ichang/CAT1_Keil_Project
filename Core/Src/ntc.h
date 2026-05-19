#ifndef __NTC_H__
#define __NTC_H__
typedef enum
{
  NTC_NORMAL,
  NTC_LOW,
  NTC_OVERFLOW,  
}ntcState_t;

typedef struct
{
  ntcState_t state;
  signed short int  Ntctemp;    /* 放大10倍 */  
}ntcTemp_t;
extern void ntc_timer(void);

extern ntcTemp_t  Ntctemp;    
extern ntcTemp_t  Ntctemp2;    
extern void NtcTempCalc(void);
extern void NtcTempCalc1(void);
extern void NtcTempCalc2(void);

#endif


