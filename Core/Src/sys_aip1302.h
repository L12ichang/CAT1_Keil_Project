

#ifndef _SYS_DS1302_H_
#define _SYS_DS1302_H_
#include "common.h"

#define RTC_NO_READY        0xff

/* hex */
typedef struct
{
    u8 sec;
    u8 min;
    u8 hour;
    u8 week ;
    u8 day;
    u8 mon;
    u16 year;
    boolean_en ready;
}RtcTime_t; 

extern RtcTime_t  apprtc_RtcTime ;

extern void sys_aip1302_timer(void);
extern void sys_aip1302_process(void);
void sys_aip1302_init(void);
extern u1t GetWeek(u2t year, u1t mon, u1t day);
extern void SetTime(void);

#endif
