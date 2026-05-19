#ifndef __CRC16_CCITT_H__
#define __CRC16_CCITT_H__
#include "common.h"


//len不包括CRC本身，执行完成后会在后面自动添加2字节的CRC
extern void crc16_ccitt_make(u8* ptr, u8 len);

//len括CRC本身
extern boolean_en crc16_ccitt_check(u8* ptr, u16 len);


extern u16 crc16_ccitt_get(u8* ptr, u8 len);

#endif

