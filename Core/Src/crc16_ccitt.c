/*************************************************************
程序功能：CRC CCITT
开发环境：keil 5.37
芯片型号：STM32F103RBT6
开发人员：梁庆能
单位名称：广东东菱电源科技有限公司
编辑日期：2023.7.1
*************************************************************/
#include "crc16_ccitt.h"

//len不包括CRC本身，执行完成后会在后面自动添加2字节的CRC
u16 crc16_ccitt_get(u8* ptr, u8 len)
{
  u8 i;
  u16 crc = 0;
  while (len-- != 0)
  {
    for (i = 0x80; i != 0; i /= 2)
    {
      if ((crc & 0x8000) != 0)
      {
        crc *= 2; 
        crc ^= 0x1021;
      }   // 余式CRC乘以2再求CRC  
      else
      {
        crc *= 2;
      }
      if ((*ptr & i) != 0)
      {
        crc ^= 0x1021;
      }                // 再加上本位的CRC 
    }
    ptr++;
  }
  return(crc);
}


//len不包括CRC本身，执行完成后会在后面自动添加2字节的CRC
void crc16_ccitt_make(u8* ptr, u8 len)
{
  u8 i;
  u16 crc = 0;
  while (len-- != 0)
  {
    for (i = 0x80; i != 0; i /= 2)
    {
      if ((crc & 0x8000) != 0)
      {
        crc *= 2; 
        crc ^= 0x1021;
      }   // 余式CRC乘以2再求CRC  
      else
      {
        crc *= 2;
      }
      if ((*ptr & i) != 0)
      {
        crc ^= 0x1021;
      }                // 再加上本位的CRC 
    }
    ptr++;
  }
  *ptr = u16l(crc);  
  ptr++;
  *ptr = u16h(crc);  
}

//len包括CRC本身
boolean_en crc16_ccitt_check(u8* ptr, u16 len)
{
  u16 crc ;
  u16 i;
  crc = 0;
  while (len-- != 0)
  {
    for (i = 0x80; i != 0; i /= 2)
    {
      if ((crc & 0x8000) != 0)
      {
        crc *= 2; 
        crc ^= 0x1021;
      }   // 余式CRC乘以2再求CRC  
      else
      {
        crc *= 2;
      }
      if ((*ptr & i) != 0)
      {
        crc ^= 0x1021;
      }                // 再加上本位的CRC 
    }
    ptr++;
  }
  return ((boolean_en)(crc==0));

}



