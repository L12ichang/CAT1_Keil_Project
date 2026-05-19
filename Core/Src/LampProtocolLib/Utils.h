#ifndef CODE_UTILS_H
#define CODE_UTILS_H
#include "common.h"

typedef struct
{
    uint8 Hours;
    uint8 Minutes;
    uint8 Seconds;
} TimeType;

typedef struct 
{
    uint8 WeekDay;
    uint8 Month;
    uint8 Date;
    uint8 Year;
} DateType;

/**
*@brief   转换Hex字符串为hex数组
*@param	  pInHexString：输入hex字符串
*@param	  nInLen：输入hex字符串长度
*@param	  pOut：输出hex数组
*@param	  nOutLen：输出hex数组长度
*@return  转换成功：返回1，转换失败：返回0
*/
uint8 hexStrToByte(const char *pInHexString,
                   uint16 nInLen, uint8 *pOut, uint16 *nOutLen);

/**
*@brief   转换hex数组为Hex字符串
*@param	  source：输入的hex数组
*@param	  dest：输出的hex字符串
*@param	  sourceLen：输入的hex数组长度
*@return  无
*/
void byteToHexStr(const uint8 *source, char *dest, int sourceLen);

/**
*@brief   计算crc16-modbus校验和
*@param	  DataSource：待计算的数组
*@param	  DataLen：数组长度
*@return  crc计算结果
*/
unsigned short getCRC16(unsigned char *DataSource, unsigned short DataLen);

/**
*@brief   初始化crc16-modbus算法
*@return  无
*/
void initCalcCrc16(void);

/**
*@brief   计算crc16-modbus校验码，该方法用于计算长数组的校验码
*@param	  DataSource：待计算的数组
*@param	  DataLen：数组长度
*@return  无
*/
void calcCrc16(unsigned char *DataSource, unsigned short DataLen);

/**
*@brief   返回crc16-modbus校验码，该方法用于计算长数组的校验码
*@return  crc计算结果
*/
unsigned short getCrc16Result(void);


#endif //CODE_UTILS_H
