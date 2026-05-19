#ifndef FIRMWAREUPDATER_H
#define FIRMWAREUPDATER_H

#define FIRMWARE_HEAD_LENGTH 13

/**
*@brief   延时函数
*@param	  dly：延时时间
*@return  无
*/
void flashDelay(uint16 dly);

/**
*@brief   获取固件长度
*@return  固件长度
*/
uint16 getFirmwareLength(void);

/**
*@brief   初始化固件升级，格式化固件升级区
*@return  无
*/
void initFirmwareUpdate(void);

/**
*@brief   校验固件头部信息
*@return  1：固件头部信息正确；其它：固件头部信息不正确
*/
uint8 checkFirmwareHead(uint8 *buf);

/**
*@brief   保存固件数据到flash
*@param	  buf：固件数据
*@param	  length：数据长度
*@return  0：保存失败；1：保存成功
*/
uint8 saveFirmwareData(uint8 *buf, uint16 length);

/**
*@brief   校验固件内容是否完整
*@return  1：校验成功；其它：校验失败
*/
uint8 validateFirmware(void);

/**
*@brief   设置新固件标志，把标志位和固件长度保存到新固件分区的最后6个字节
*@param   length：固件长度
*@return  无
*/
void setNewFirmwareFlag(uint16 length);

#endif
