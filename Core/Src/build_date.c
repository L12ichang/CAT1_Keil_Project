/*************************************************************
程序功能：编译日期和时间
开发环境：keil 5.37
芯片型号：STM32F103RBT6
开发人员：梁庆能
单位名称：广东东菱电源科技有限公司
编辑日期：2023.7.1
*************************************************************/

#include "build_date.h"

/*

printf("%s_%s_%d_%d\n", __DATE__, __TIME__, sizeof(__DATE__),sizeof(__TIME__));

输出结果为：

Jan 13 2017_17:24:11_12_9

*/

unsigned char const DataStr[]=__DATE__;//长12字节，最后一个字节是结束符0。
unsigned char const TimeStr[]=__TIME__;//长9字节 ，最后一个字节是结束符0。

//返回字符串的长度。包含结束符0, 21字节,  返回：date_time 为 Jun 17 2022 13:48:30 格式

unsigned char build_date_time_to_buff(unsigned char* date_time)
{
  unsigned char i,j=0;
  for (i=0; i<sizeof(__DATE__)-1; i++)
  {
    date_time[j++]=DataStr[i];
  }
  date_time[j++]=' ';
  for (i=0; i<sizeof(__TIME__); i++)
  {
    date_time[j++]=TimeStr[i];
  }
  return (j);
}

