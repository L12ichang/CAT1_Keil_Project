/*************************************************************
程序功能：外部RTC
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include"aip1302.h"
#include"sys_tick.h"



# if 0
//实测高电平1.5us，低电平1.9us
#define PULSE_DELAY        do{  __asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");__asm("nop");}while(0U);
#define T_IO_OUTPUT_MODE   do { GPIO_InitTypeDef GPIO_InitStruct = {0};__HAL_RCC_GPIOB_CLK_ENABLE(); GPIO_InitStruct.Pin =GPIO_PIN_14; GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP; HAL_GPIO_Init(GPIOB, &GPIO_InitStruct); } while(0U) ;/* 设置为输出模式 */         
#define T_IO_INPUT_MODE    do { GPIO_InitTypeDef GPIO_InitStruct = {0};__HAL_RCC_GPIOB_CLK_ENABLE();GPIO_InitStruct.Pin =GPIO_PIN_14; GPIO_InitStruct.Mode = GPIO_MODE_INPUT; HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);} while(0U) ; /* 设置为输入模式 */         
#define SET_DATA_HIGH        GPIOB->BSRR = GPIO_Pin_14      /* 内置40k下拉电阻 */
#define SET_DATA_LOW         GPIOB->BRR = GPIO_Pin_14
#define DATA_HIGH            ((GPIOB->IDR & GPIO_Pin_14) != (uint32_t)0)
#define T_IO_PULL_SET       //o_1302_io = 1;                 /* 设置上拉,并设为高电平 */ 
#define SET_CLK_HIGH         GPIOB->BSRR = GPIO_Pin_13        /* 内置40k下拉电阻,上升沿锁存数据,下降沿输出数据 */
#define SET_CLK_LOW         GPIOB->BRR = GPIO_Pin_13
#define RESET_EFFECTIVE      GPIOB->BRR  = GPIO_Pin_12      /* 内置40k下拉电阻，读写操作时需要把它拉高 */
#define RESET_INVALID        GPIOB->BSRR = GPIO_Pin_12 
# else



void _PULSE_DELAY(void)  
{
    do{
            for(uint8 i=0;i<4;i++)
            {
                __asm("nop");
                __asm("nop");
                __asm("nop");
                __asm("nop");
                __asm("nop");
                __asm("nop");
                __asm("nop");
                __asm("nop");
                __asm("nop");
            }
   }while(0U);
}
#define PULSE_DELAY   _PULSE_DELAY()


 
void T_IO_OUTPUT_INIT(void)
{
     GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin   =    GPIO_PIN_13;
    GPIO_InitStruct.Mode  =    GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  =    GPIO_PULLUP;    
    GPIO_InitStruct.Speed =    GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);  
    
}

void T_IO_INPUT_INIT(void)
{

     GPIO_InitTypeDef GPIO_InitStruct = {0};
   __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin   =    GPIO_PIN_13;
    GPIO_InitStruct.Mode  =    GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  =    GPIO_PULLDOWN;//调试完后恢复GPIO_PULLUP,因为可以看输出转为输入时的悬空拉高自恢复高电平波形
    GPIO_InitStruct.Speed =    GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct); 
 
   
}
void T_IO_PULL_SET_INTI()
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE(); 
    GPIO_InitStruct.Pin   =    GPIO_PIN_13;
    GPIO_InitStruct.Pull = GPIO_PULLUP; 
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}


/*GPIO Configuration
PB14    ------>  T_CLK
PB13    ------>  T-I/O
PB12    ------>  T-CE
*/

#define T_IO_OUTPUT_MODE     T_IO_OUTPUT_INIT();
#define T_IO_INPUT_MODE      T_IO_INPUT_INIT() ;

#define SET_DATA_HIGH        do{ HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);}while (0U)               
#define SET_DATA_LOW         do{ HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);}while(0U)       
#define DATA_HIGH            ((GPIOB->IDR & GPIO_Pin_13) != (uint32_t)0)
#define T_IO_PULL_SET        do {GPIO_InitTypeDef GPIO_InitStruct = {0}; __HAL_RCC_GPIOB_CLK_ENABLE();   GPIO_InitStruct.Pull = GPIO_PULLUP; HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET); } while(0U);
#define T_IO_NOPULL_SET      do {GPIO_InitTypeDef GPIO_InitStruct = {0}; __HAL_RCC_GPIOB_CLK_ENABLE();   GPIO_InitStruct.Pull = GPIO_NOPULL; HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET); } while(0U);
#define RESET_EFFECTIVE      do {  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);} while(0U);   
#define RESET_INVALID        do {  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);}   while(0U);   
#define SET_CLK_HIGH         do {  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);}   while(0U);
#define SET_CLK_LOW          do {  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);} while(0U);

#endif


/******************************************************************** 
*
* 名称: Ds1302_inputByte
* 说明: 
* 功能: 往DS1302写入1Byte数据
* 调用:
* 输入: ucDa 写入的数据 
* 返回值: 无
***********************************************************************/
void Ds1302_inputByte(u8 ucDa)
{
  u8 i;
//  T_IO_OUTPUT_MODE;
  T_IO_OUTPUT_INIT();
  for (i = 8; i > 0; i--)
  {
    if (ucDa & 0x01)
    { 
      SET_DATA_HIGH;
    }
    else
    {
      SET_DATA_LOW;      
    }
    PULSE_DELAY;          /* T_DC 50ns */    
    SET_CLK_HIGH;         /* 时钟上升沿锁存 */
    PULSE_DELAY;          /* (T_CH) 250ns */
    SET_CLK_LOW;
    /* T_CL 250ns */
    ucDa >>= 1;
  }
    PULSE_DELAY; 
 
}
/*********************************************************************** 
*
* 名称: u8 Ds1302_outputByte
* 说明: 
* 功能: 从DS1302读取1Byte数据
* 调用: 
* 输入: 
* 返回值: ACC
***********************************************************************/
u8 Ds1302_outputByte(void)
{
  u8 i;
  u8 ACC = 0;
  T_IO_INPUT_INIT() ;//配置耗时5.7uS  
  T_IO_PULL_SET_INTI();//配置耗时5.7uS      
  for (i = 0; i < 8; i++)
  {
    ACC >>= 1;
     if (DATA_HIGH)//if (((GPIOB->IDR & GPIO_Pin_14) != (uint32_t)0))//
    {
      ACC |= 0x80;
    }
    else
    {
      ACC &= 0x7f;
    }
    PULSE_DELAY; 
    SET_CLK_HIGH;
    PULSE_DELAY;          /* (T_CH) 250ns */
    SET_CLK_LOW;          /* 下降沿输出数据 */
    PULSE_DELAY;        //WDJ++20250430 不延时会在下降沿读到高电平
    /* T_CDD 200ns    */
    /* T_CL 250ns     */
  } 
  PULSE_DELAY;
  return(ACC);
}

/******************************************************************** 
*
* 名称: Ds1302_write_byte
* 说明: 先写地址，后写命令/数据
* 功能: 往DS1302写入数据
* 调用: Ds1302_inputByte() 
* 输入: ucAddr: DS1302地址, ucDa: 要写的数据
* 返回值: 无
***********************************************************************/
void Ds1302_write_byte(u8 ucAddr, u8 ucDa)
{
  RESET_EFFECTIVE;
  SET_CLK_LOW;
  PULSE_DELAY ;//WDJ //WDJ 确保CLK 为低电平时CE为高电平
  RESET_INVALID;
  Ds1302_inputByte(ucAddr); /* 地址，命令 */
  Ds1302_inputByte(ucDa);   /* 写1Byte数据*/
  SET_CLK_LOW;//WDJ改，传送完成后时钟电平归0   原SET_CLK_HIGH;// 
  RESET_EFFECTIVE;
} 
/******************************************************************** 
*
* 名称: Ds1302_read_byte
* 说明: 先写地址，后读命令/数据
* 功能: 读取DS1302某地址的数据
* 调用: Ds1302_inputByte() , Ds1302_outputByte()
* 输入: ucAddr: DS1302地址
* 返回值: ucDa :读取的数据
***********************************************************************/
u8 Ds1302_read_byte(u8 ucAddr)
{
  u8 ucDa;
   __disable_irq();
  RESET_EFFECTIVE;
  SET_CLK_LOW;
  PULSE_DELAY ;//WDJ 确保CLK 为低电平时CE为高电平
  RESET_INVALID;
  Ds1302_inputByte(ucAddr);   /* 地址，命令 */
  ucDa = Ds1302_outputByte(); /* 读1Byte数据 */
  SET_CLK_LOW;//WDJ改，传送完成后时钟电平归0  原SET_CLK_HIGH;//
  RESET_EFFECTIVE;
  __enable_irq();
  return(ucDa);
    
}
/******************************************************************** 
*
* 名称: Ds1302_burst_write
* 说明: 先写地址，后写数据(时钟多字节方式)
* 功能: 往DS1302写入时钟数据(多字节方式)
* 调用: Ds1302_inputByte() 
* 输入: pSecDa: 时钟数据地址 格式为: 秒 分 时 日 月 星期 年 控制
* 8Byte (BCD码) 1B 1B 1B 1B 1B 1B 1B 1B
* 返回值: 无
***********************************************************************/
void Ds1302_burst_write(u8* pSecDa)
{
  u8 i;
  Ds1302_write_byte(0x8e, 0x00);  /* 控制命令,WP=0,写操作?*/
  RESET_EFFECTIVE;
  SET_CLK_LOW;
  RESET_INVALID;
  Ds1302_inputByte(0xbe);         /* 0xbe:时钟多字节写命令 */
  for (i = 8; i > 0; i--)         /*8Byte = 7Byte 时钟数据 + 1Byte 控制*/
  {
    Ds1302_inputByte(*pSecDa);    /* 写1Byte数据*/
    pSecDa++;
  }
  SET_CLK_HIGH;
  RESET_EFFECTIVE;
} 
/******************************************************************** 
*
* 名称: Ds1302_burst_read
* 说明: 先写地址，后读命令/数据(时钟多字节方式)
* 功能: 读取DS1302时钟数据
* 调用: Ds1302_inputByte() , Ds1302_outputByte()
* 输入: pSecDa: 时钟数据地址 格式为: 秒 分 时 日 月 星期 年 
* 7Byte (BCD码) 1B 1B 1B 1B 1B 1B 1B
* 返回值: ucDa :读取的数据
***********************************************************************/
void Ds1302_burst_read(u8* pSecDa)
{
  u8 i;
  RESET_EFFECTIVE;
  SET_CLK_LOW;
  RESET_INVALID;
  Ds1302_inputByte(0xbf);           /* 0xbf:时钟多字节读命令 */
  for (i = 8; i > 0; i--)
  {
    *pSecDa = Ds1302_outputByte();  /* 读1Byte数据 */
    pSecDa++;
  }
  SET_CLK_HIGH;
  RESET_EFFECTIVE;
}
/******************************************************************** 
*
* 名称: Ds1302_burst_write_reg
* 说明: 先写地址，后写数据(寄存器多字节方式)
* 功能: 往DS1302寄存器数写入数据(多字节方式)
* 调用: Ds1302_inputByte() 
* 输入: pReDa: 寄存器数据地址
* 返回值: 无
***********************************************************************/
void Ds1302_burst_write_reg(u8* pReDa)
{
  u8 i;
  Ds1302_write_byte(0x8e, 0x00);  /* 控制命令,WP=0,写操作?*/
  RESET_EFFECTIVE;
  SET_CLK_LOW;
  RESET_INVALID;
  Ds1302_inputByte(0xfe);         /* 0xbe:时钟多字节写命令 */
  for (i = 31; i > 0; i--)        /*31Byte 寄存器数据 */
  {
    Ds1302_inputByte(*pReDa);     /* 写1Byte数据*/
    pReDa++;
  }
  SET_CLK_HIGH;
  RESET_EFFECTIVE;
} 
/******************************************************************** 
*
* 名称: uc_BurstR1302R
* 说明: 先写地址，后读命令/数据(寄存器多字节方式)
* 功能: 读取DS1302寄存器数据
* 调用: Ds1302_inputByte() , Ds1302_outputByte()
* 输入: pReDa: 寄存器数据地址
* 返回值: 无
***********************************************************************/
void Ds1302_burst_read_reg(u8* pReDa)
{
  u8 i;
  RESET_EFFECTIVE;
  SET_CLK_LOW;
  RESET_INVALID;
  Ds1302_inputByte(0xff);         /* 0xbf:时钟多字节读命令 */
  for (i = 31; i > 0; i--)        /*31Byte 寄存器数据 */
  {
    *pReDa = Ds1302_outputByte(); /* 读1Byte数据 */
    pReDa++;
  }
  SET_CLK_HIGH;
  RESET_EFFECTIVE;
}
/******************************************************************** 
*
* 名称: Ds1302_write_time
* 说明: 
* 功能: 设置初始时间
* 调用: Ds1302_write_byte() 
* 输入: pSecDa: 初始时间地址。初始时间格式为: 秒 分 时 日 月 星期 年 
* 7Byte (BCD码) 1B 1B 1B 1B 1B 1B 1B
* 返回值: 无
***********************************************************************/
u8 Ds1302_write_time(u8* pSecDa)
{
  u8 i;
  u8 ucAddr = 0x80; 
  Ds1302_write_byte(0x8e, 0x00);        /* 控制命令,WP=0,写操作?*/
  for (i = 7; i > 0; i--)
  {    
    Ds1302_write_byte(ucAddr, *pSecDa); /* 秒 分 时 日 月 星期 年 */
    pSecDa++;
    ucAddr += 2;
  }
  Ds1302_write_byte(0x8e, 0x80);      /* 控制命令,WP=1,写保护?*/
    return (true);
}
/******************************************************************** 
*
* 名称: Ds1302_read_time
* 说明: 
* 功能: 读取DS1302当前时间
* 调用: Ds1302_read_byte() 
* 输入: ucCurtime: 保存当前时间地址。当前时间格式为: 秒 分 时 日 月 星期 年 
* 7Byte (BCD码) 1B 1B 1B 1B 1B 1B 1B
* 返回值: 无
***********************************************************************/
u8 Ds1302_read_time(u8* ucCurtime)
{
  u8 i;
  u8 ucAddr = 0x81;
  static u8 _counter=0;
  for (i = 0; i < 7; i++)
  {
     ucCurtime[i] = Ds1302_read_byte(ucAddr);  /*格式为: 秒 分 时 星期 日 月 年 */
    // printf("XXXucCurtime[%d] =%d\n",i,ucCurtime[i] ) ;
     ucAddr += 2;
  }
  if (ucCurtime[0]&0x80)
  {
    /* 如果 clock halt 意外生效, 重新启动振荡器 */
    if (++_counter >= 10)
    {
      _counter = 0;
      ucCurtime[0] &= 0x7f;   
      Ds1302_write_time(ucCurtime);
    }    
  }
  else
  {
    _counter = 0;
  }
  return (true);
} 


void Ds1302_set_charge(void)
{
  Ds1302_write_byte(0x8e, 0x00);        /* 控制命令,WP=0,写操作?*/
  Ds1302_write_byte(0x90, 0xa5);       
  Ds1302_write_byte(0x8e, 0x80);        /* 控制命令,WP=1,写保护?*/

}

void Ds1302_init(void)
{
   
GPIO_InitTypeDef GPIO_InitStruct = {0};
__HAL_RCC_GPIOB_CLK_ENABLE();
GPIO_InitStruct.Pin   =    GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14;
GPIO_InitStruct.Mode  =    GPIO_MODE_OUTPUT_PP;
GPIO_InitStruct.Pull  =    GPIO_PULLDOWN;
GPIO_InitStruct.Speed =    GPIO_SPEED_FREQ_HIGH;
HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);  
     
}


