#ifndef SYS_DATA_H
#define SYS_DATA_H

#include "common.h"


/*STM32F103CBT6
++++++++++++++++++++++++++++++++ 0x8000000
+                              +
+     bootloader (12K)         +
+                              +
++++++++++++++++++++++++++++++++ 0x8003000 
+                              +
+                              +
+                              +
+                              +
+                              +
+    app partition (56K)       +
+                              +
+                              +
+                              +
+                              +
+                              +
++++++++++++++++++++++++++++++++ 0x8011000
+                              +
+                              +
+                              +
+                              +
+        firmware (58K)        +
+                              +
+                              +
+                              +
+                              +
+                              + 
++++++++++++++++++++++++++++++++ 0x801F800
+       (BAKROM FIRM_FLAG)     +
+       (BAKROM CHECKSUM )     +     |
+       (BAKROM SIZE    )      +sysydata(2K)
+     firmwareinfo(16BYTE)     +  
+     firmwareinfo(16BYTE)     + |
++++++++++++++++++++++++++++++++ 0x801FFFF
*/





/*HK32F103CCT6A
++++++++++++++++++++++++++++++++ 0x8000000
+                              +
+     bootloader (20K)         +
+                              +
++++++++++++++++++++++++++++++++ 0x8005000   20K
+       (BAKROM FIRM_FLAG)     +
+       (BAKROM CHECKSUM )     +     |
+         (BAKROM SIZE)        +  sysydata(12K)
+     firmwareinfo(16BYTE)     +  
+     firmwareinfo(16BYTE)     +     |
++++++++++++++++++++++++++++++++ 0x8008000 (20+12k)
+                              +
+    ADDR_CHECKSUM 0x200       +
+    ADDR_SIZE     0x204       +
+                              +
+                              +
+    app partition (56*2K)     +
+                              +
+                              +
+                              +
+                              +
+                              +
++++++++++++++++++++++++++++++++ 0x8024000(20+12+116)
+                              +
+                              +
+                              +
+                              +
+        firmware (56*2K)      +
+                              +
+                              +
+                              +
+                              +
+                              +  
++++++++++++++++++++++++++++++++ 0x803FFFF(256K)
*/
                                 
#define   BOOTROM_STARTADDR               (u32)0x8000000  
#define   DATAROM_STARTADDR               (u32)0x8005000  //参数区
#define   BAKDATAROM_STARTADDR            (u32)0x8006800  //数据备份区
#define   APROM_STARTADDR                 (u32)0x8008000 
#define   APROM_SAFE_ENDADDR              (u32)0x8024000
#define   OTABAKROM_STARTADDR             (u32)0x8024000    //( BOOTROM_STARTADDR+ROM_OFFSET)                  
#define   OTABAKROM_ENDADDR               (u32)0x803FFFF     //        


#define   ADDR_CHECKSUM_OFFSET            0x200
#define   ADDR_SIZE_OFFSET                0x204
#define   ADDR_TYPE_OFFSET                0x208


// keil C Compiler:v5
//const u32 prog_checksum __attribute__((at(APROM_STARTADDR+ADDR_CHECKSUM_OFFSET))) = 0x12345678;  //程序校验和
//const u32 prog_length   __attribute__((at(APROM_STARTADDR+ADDR_SIZE_OFFSET))) = 0x89abcdef;      //程序长度
//const u16 device_type   __attribute__((at(APROM_STARTADDR+ADDR_TYPE_OFFSET))) = 0x0007;         //设备类型
// keil C Compiler:v6
// const u32 prog_checksum __attribute__((section(".ARM.__at_0x200"))) = 0x12345678;  //程序校验和
// const u32 prog_length   __attribute__((section(".ARM.__at_0x204"))) = 0x89abcdef;  //程序长度
// const u16 device_type   __attribute__((section(".ARM.__at_0x208"))) = 0x0001;         //设备类型
//保留地址空间


typedef struct
{
    u8 type:2;
    u8 on_off:1;    
}plan_type_st;




typedef struct
{
    u8 year;
    u8 mon;
    u8 day;
    u8 week;
    u8 hour;
    u8 min;
    plan_type_st plan_type;
    u16 keep_time;  //持续时间
    u8 keep_power;
    u16 standby_time;
    u8 standby_power;
    u16 env_lux;
    boolean_en flag_plan;//定时计划生效标志
    u16 end_time;
}plan_st;


typedef struct
{
    u8 total;
    plan_st plan[7];
    u8 active_index;
}all_plan_st;

typedef struct
{
    u8 enable;
    s8 temp_protect;
    u8 power;
    s8 temp_recovery;
    u8 power_recovery;
}temp_protect_st;


typedef struct
{
    u16 voltage_high; //0.1V
    u16 voltage_low; //0.1v
    u16 current_high; //mA
    u16 current_low;
}alarm_threshold_st;


typedef struct
{
    u32 sn;                 //SN号
    u32 checksum1;          //校验和
    u32 firmware_len;       //固件长度
    u32 bak_verson;         //备份版本
    u32 mac; 
    u32 ota_enable; 
  //OTA使能及相关标志
    u8  fa_Parambuf[128];
    u32 couter;
    u32 ac_EnergyP;     //累积电能 0.01Wh
    u8 lamp_power;  //0-100
    u8 day; //统计天数
    u16 today_Energy; //0.1wh
    all_plan_st all_plan; //定时计划
    temp_protect_st temp_protect;
    u32  setcur;   //设定电流 mA
    u32  hwmaxcur;  //硬件最大电流 mA
 /*-------------  保留空间  --------------- */
    char openid[33];
    char  token[33]; 
    
    
    
 /*-------------  保留空间  --------------- */   
  /* 系统数据管理 */
   u16 usedata_sn;       //使用数据序列号
   u16 checksum;         //系统数据校验和

}sys_data_st;

#define SYS_DATA_ST_EXPECTED_SIZE         408
typedef char sys_data_st_size_must_remain_408[(sizeof(sys_data_st) == SYS_DATA_ST_EXPECTED_SIZE) ? 1 : -1];

extern sys_data_st  sys_data  __attribute__ ((aligned(4)));

/************************************
功能描述：从Flash加载系统数据
输入参数：无
输出返回：无
*************************************/
extern void sys_data_load(void);


/************************************
功能描述：保存系统数据到Flash
输入参数：无
输出返回：无
*************************************/
extern void sys_data_store(void);
extern boolean_en sys_data_store_checked(void);
extern void sys_data_default(void);
extern boolean_en flash_store(u8* buf, u16 size, u32 addr_main);
extern boolean_en data_store_data(u8* buf, u16 size, u32 addr_main);

#endif



