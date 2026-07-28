/*************************************************************
程序功能：OTA相关跳转地址/自校验地址
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2023.7.1
*************************************************************/
#include "for_iap.h"
#include "sys_tick.h"

#define SPECIALID           127
#define BROADCASTID         255
#define CMD_IAP             0x96

// keil v5
//const u32 prog_checksum __attribute__((at(0X200))) = 0x12345678;  //程序检验使用
//const u32 prog_length   __attribute__((at(0X204))) = 0x89abcdef;  //程序长度，单位是指令数。即4个字节为单位
// keil C Compiler:v5
//const u32 prog_checksum __attribute__((at(APROM_STARTADDR+ADDR_CHECKSUM_OFFSET))) = 0x12345678;  //程序检验使用
//const u32 prog_length   __attribute__((at(APROM_STARTADDR+ADDR_SIZE_OFFSET))) = 0x89abcdef;  //程序长度，单位是指令数。即4个字节为单位
//const u16 device_type   __attribute__((at(APROM_STARTADDR+ADDR_TYPE_OFFSET))) = 0x0007;         //设备类型。要与boot程序一致。

//keil v6
#if APROM_OFFSET_ADDR == 0x08008000
const u32 prog_checksum __attribute__((used, section(".ARM.__at_0x8008200"))) = 0x12345678;  //程序检验使用, 原生是0x12345678。IAP后是具体的实际值
const u32 prog_length   __attribute__((used, section(".ARM.__at_0x8008204"))) = 0x89abcdef;  //程序长度，单位是指令数。即4个字节为单位
const u16 device_type   __attribute__((used, section(".ARM.__at_0x8008208"))) = 0x0003;         //设备类型。要与boot程序一致。
#else 
const u32 prog_checksum __attribute__((used, section(".ARM.__at_0x8000200"))) = 0x12345678;  //程序检验使用, 原生是0x12345678。IAP后是具体的实际值
const u32 prog_length   __attribute__((used, section(".ARM.__at_0x8000204"))) = 0x89abcdef;  //程序长度，单位是指令数。即4个字节为单位
const u16 device_type   __attribute__((used, section(".ARM.__at_0x8000208"))) = 0x0003;         //设备类型。要与boot程序一致。
#endif


typedef         void (*pFunction)(void);     
pFunction        Jump_To_Boot;
u32              JumpAddress;

/************************************
功能描述：程序从app区跳转到boot区
输入参数：无
输出返回：无
*************************************/
void iap_jump2boot(void)
{ 
	__disable_irq();
    SysTick->LOAD = 0xa55aa5;
	__set_FAULTMASK(1);
	NVIC_SystemReset();
}

void soft_reset(void)
{
  JumpAddress = *(__IO u32*) (BOOT_OFFSET_ADDR + 4);                  //
  Jump_To_Boot = (pFunction) JumpAddress;                             //
  __set_MSP(*(__IO u32*) BOOT_OFFSET_ADDR);                           //
  SysTick->LOAD = 0xa55aa5;
  Jump_To_Boot();
}


