#ifndef __CRC16_MODBUS_H__
#define __CRC16_MODBUS_H__
#include "common.h"


extern unsigned short crc16_modbus_get(unsigned char *buf, int len);
extern boolean_en crc16_modbus_check(u8 *buf, u16 length);
extern void crc16_modbus_make(u8 *buf, u16 length);

#endif

