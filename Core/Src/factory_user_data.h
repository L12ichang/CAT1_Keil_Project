#ifndef FACTORY_DATA_H
#define FACTORY_DATA_H

#include "common.h"

extern u8  factory_user_buff[128];
extern u16 SET_OUTCUR_temp;
extern u16 HWMAX_OUTCUR_temp;
extern u16 OUTPUT_CUR_SENSOR_temp;
extern u16 OP_PWM_OFFSET_temp;

#define FACTORY_OUTCUR_MAX_MA 10000U
#define FACTORY_OUTPUT_CUR_SENSOR_MAX_MOHM 1000U
#define FACTORY_PWM_OFFSET_MAX_EXCLUSIVE 1000U

typedef struct
{
    u8 hour;
    u8 min;
    u8 dim_lever;
}timer_dim_struct_en;

typedef struct
{
        u8 sec;
        u8 min;
        u8 hour;
        u8 week ; 
        u8 day;  
        u8 mon;  
        u16 year;   
}Server_TM_Time_t; 


/*工厂区参数 */
#define SID                  (*((u8*)(factory_user_buff+0x04)))      //产品系列
#define MID                  (*((u8*)(factory_user_buff+0x05)))      //产品型号  //1: 60W   2: 75W   3:100W   4:150W  5: 200W  6: 240W
#define DRV_VERSION          (*((u8*)(factory_user_buff+0x06)))      //驱动器版本
#define Protocol_version     (*((u8*)(factory_user_buff+0x07)))      //协议版本号	0是初始旧协议，1是新协议

#define  SET_OUTCUR          SET_OUTCUR_temp                         // 额定电流，需要大小端转换  
#define  HWMAX_OUTCUR        HWMAX_OUTCUR_temp                       // 硬件最大电流，需要大小端转换      
#define  OUTPUT_CUR_SENSOR    OUTPUT_CUR_SENSOR_temp                  //输出电流传感器  单位毫欧
#define  OP_PWM_OFFSET        OP_PWM_OFFSET_temp                      // 光耦延迟补偿 单位千分之一
/*     大小端转换在c文件
       u16h(  SET_OUTCUR_temp) = (*((u8*)(factory_user_buff+0x10))); 
       u16l(  SET_OUTCUR_temp) = (*((u8*)(factory_user_buff+0x11))); 
       u16h(  HWMAX_OUTCUR_temp)= (*((u8*)(factory_user_buff+0x12))) ;
       u16l(  HWMAX_OUTCUR_temp)= (*((u8*)(factory_user_buff+0x13))) ;
       u16h(  OUTPUT_CUR_SENSOR_temp)= (*((u8*)(factory_user_buff+0x14))) ;
       u16l(  OUTPUT_CUR_SENSOR_temp)= (*((u8*)(factory_user_buff+0x15))) ;
       u16h( OP_PWM_OFFSET_temp)      =(*((u8*)(factory_user_buff+0x16))) ;
       u16l( OP_PWM_OFFSET_temp)      =(*((u8*)(factory_user_buff+0x17))) ;
*/
#define INNRE_TEMP_PRO_EN    (*((u8*)(factory_user_buff+0x18)))      //过温使能     
#define INNRE_TEMP_PRO       (*((s8*)(factory_user_buff+0x19)))      //过温保护值 
#define CX                   (*((u8*)(factory_user_buff+0x1e)))      //单位uf放大了100倍，使用时要除以100，默认0.68uf值为68



#define STAR_TIME            (*((Server_TM_Time_t*)(factory_user_buff+0x30)))      //离线调光起始时间
#define END_TIME             (*((Server_TM_Time_t*)(factory_user_buff+0x37)))      //离线调光结束时间
#define DAY_LOOP_EN          (*((u8*)(factory_user_buff+0x3e)))   //日调光使能
#define SCHEDULE_SIZE        (*((u8*)(factory_user_buff+0x3f)))   //单日调光动作数
#define SEVER_TIMER_DIM      ((timer_dim_struct_en *)(factory_user_buff+0x40))

/************************************
功能描述：加载flash里的工厂参数和用户参数到 factory_user_buff
输入参数：无
输出返回：无
*************************************/
extern void factory_user_load_data(void);

extern void fac_128_data_default(void);


#endif



