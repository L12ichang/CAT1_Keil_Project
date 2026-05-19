#ifndef _DS1302_H_
#define _DS1302_H_
#include "common.h"

typedef struct
{
  u8 sec : 7;         
  u8 ch  : 1;        /* ch=1时,停止计时 */
}ds1302_sec_st;
typedef union
{
  ds1302_sec_st sec;
  u8 u1;
}ds1302_sec_t;

typedef struct
{
  u8 hour : 6; 
  u8      : 1; 
  u8 mode : 1;      /* 0:24小时, 1:12小时 */
}ds1302_hour_st24;
typedef struct
{
  u8 hour : 5; 
  u8 am_pm: 1;      /* 0: AM, 1:PM */
  u8      : 1;
  u8 mode : 1;      /* 1:12小时, 0:24小时 */
}ds1302_hour_st12;

typedef union
{
  ds1302_hour_st24 hour24;
  ds1302_hour_st12 hour12;
  u8 u1;
}ds1302_hour_t;

typedef struct
{
  u8    : 7;  
  u8 wp : 1;      /* 0:正常读写, 1:写保护 */
}ds1302_control_st;

typedef union
{
  ds1302_control_st control;
  u8 u1;
}ds1302_control_t;

typedef struct
{
  u8 rs : 2;       /* RESISTOR SELECT */
  u8 ds : 2;       /* DIODE SELECT */
  u8 tcs: 4;       /* TRICKLE CHARGER SELECT '1010b' enable */
}ds1302_trickle_charger_st;

typedef union
{
  ds1302_trickle_charger_st control;
  u8 u1;
}ds1302_trickle_charger_t;

/* bcd */
typedef struct
{
  ds1302_sec_t sec;
  u8 min;
  ds1302_hour_t hour;
  u8 day;                  /* 1~31 */
  u8 mon;                  /* 1~12 */
  u8 week;                 /* 1~7 */
  u8 year;                 /* 00~99 */
  ds1302_control_t control;
  ds1302_trickle_charger_t trickle_charger;
  u8 clock_burst;
}ds1302_st; 
/* bcd */
typedef struct
{
  u8 sec;
  u8 min;
  u8 hour;
  u8 day;                  /* 1~31 */
  u8 mon;                  /* 1~12 */
  u8 week;                 /* 1~7 */
  u8 year;                 /* 00~99 */
  u8 control;
  u8 trickle_charger;
  u8 clock_burst;
}ds1302_t; 

extern u8 Ds1302_read_time(u8* ucCurtime);
extern u8 Ds1302_write_time(u8* pSecDa);
extern void Ds1302_init(void);
void Ds1302_set_charge(void);


#endif



