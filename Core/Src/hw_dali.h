#ifndef HW_DALI_H
#define HW_DALI_H

#include "common.h"



#if HARDWARE_VERSION == HARDWARE_VERSION_1
#define DALI_OUT_ENABLE                  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_SET);	
#define DALI_OUT_DISABLE                 HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_RESET);	

#elif HARDWARE_VERSION == HARDWARE_VERSION_2
#define DALI_OUT_ENABLE                  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);	
#define DALI_OUT_DISABLE                 HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);	
#endif

#define TX_LOW                          HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_SET);	
#define TX_HIGH                         HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_RESET);


#define ONE_TE_TIME_STANDARD                 (u32)416    //US              
//#define ONE_TE_TIME                 ((u32)416 * SYS_BASE_FREQUENCY_MHZ)  // 19968     一个TE是0.416667ms。   
//#define ONE_TE_TIME                 ((u32)104 * SYS_BASE_FREQUENCY_MHZ)  //4倍
//#define ONE_TE_TIME                 ((u32)2000)   //10倍速度
//#define ONE_TE_TIME                 ((u32)3333)   //6倍速度
//#define ONE_TE_TIME                 ((u32)4000)   //4992 5倍
#define ONE_TE_TIME                 (u32)custom_te                 

#define STOP_TIME                   ((u32)5 * ONE_TE_TIME) //2个停止位  ,末位是0时4TE，末位是1时5TE
#define SETTING_TIME_MIN            ((u32)7 * ONE_TE_TIME)  //7TE   2.916667ms      



//
//#define ONE_TE_TIME_MIN             (u32)(ONE_TE_TIME*217/256)    //*0.85
//#define ONE_TE_TIME_MAX             (u32)(ONE_TE_TIME*294/256)    //*1.15 
//
//#define TWO_TE_TIME_MIN             (u32)(ONE_TE_TIME*2*217/256)    //*0.85
//#define TWO_TE_TIME_MAX             (u32)(ONE_TE_TIME*2*294/256)    //*1.15


#define ONE_TE_TIME_MIN             (u32)(ONE_TE_TIME*204/256)    //*0.8
#define ONE_TE_TIME_MAX             (u32)(ONE_TE_TIME*307/256)    //*1.2 

#define TWO_TE_TIME_MIN             (u32)(ONE_TE_TIME*2*204/256)    //*0.8
#define TWO_TE_TIME_MAX             (u32)(ONE_TE_TIME*2*307/256)    //*1.2


//以下参数在新唐编程时通信出问题，恢复上面的参数
//#define ONE_TE_TIME_MIN             (u32)(ONE_TE_TIME*154/256)    //*0.6
//#define ONE_TE_TIME_MAX             (u32)(ONE_TE_TIME*359/256)    //*1.4 
//
//#define TWO_TE_TIME_MIN             (u32)(ONE_TE_TIME*(2*154)/256)    //*0.6
//#define TWO_TE_TIME_MAX             (u32)(ONE_TE_TIME*(2*359)/256)    //*1.4


#define SETTING_TIME                ((u32)22 * ONE_TE_TIME)  //22TE   439296 9.152ms      








extern u32 custom_te;
extern u16 custom_te_us;
extern boolean_en iap_dali; //BOOL_TRUE是iap时的非标准dali时序，BOOL_FALSE是标准的dali时序

/************************************
功能描述：有高脉冲输入，下降沿触发中断
输入参数：无
输出返回：无
*************************************/
extern void hw_dali_rx(u8 status, u32 t);


/************************************
功能描述：主循环调用
输入参数：无
输出返回：无
*************************************/
extern void hw_dali_process(void);


extern void hw_dali_timer(void);


/************************************
功能描述：初始化
输入参数：无
输出返回：无
*************************************/
extern void hw_dali_init(void);


    
/************************************
功能描述：发送过程中定时器中断，一个位2个TE时间，每个TE也中断一次
输入参数：无
输出返回：无
*************************************/
extern void hw_dali_tx_handle(void);


/************************************
功能描述：发送一帧数据
输入参数：buf是要发送的数据，size字节个数, iap，BOOL_TRUE是iap时的非标准dali时序，BOOL_FALSE是标准的dali时序, base_time true时为发送前先发80ms的时间基准
输出返回：无
*************************************/
extern void hw_dali_tx(u8* buf, u16 size, boolean_en iap, boolean_en base_time);


/************************************
功能描述：dali在发送数据吗
输入参数：无
输出返回：是返回 BOOL_TRUE ，不是返回 BOOL_FALSE
*************************************/
extern boolean_en hw_dali_is_txing(void);


/************************************
功能描述：发送80ms脉冲为从机提供时间基准
输入参数：无
输出返回：是返回 BOOL_TRUE ，不是返回 BOOL_FALSE
*************************************/
extern void hw_dali_tx_80ms(void);
extern void hw_dali_tx_80ms_only(void);


/************************************
功能描述：发送一帧数据, 新唐方案协议
输入参数：buf是要发送的数据，size字节个数
输出返回：无
*************************************/
extern void hw_dali_tx_old(u8* buf, u16 size);


/************************************
功能描述：发送一帧数据
输入参数：buf是要发送的数据，size字节个数
输出返回：无
*************************************/
extern void hw_dali_tx_iap_header(u8* buf, u16 size, u8 off_time, u16 header_time);

#endif

