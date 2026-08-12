
#ifndef FLASH_ALLOCATION_H
#define FLASH_ALLOCATION_H

#include "common.h"
#include "flash_address_assignment.h"



#define WRITE_START_ADDR  CAT1_FLASH_LEGACY_PROGRAMMER_START  //旧编程器数据区，不属于校准槽
#define WRITE_END_ADDR    (CAT1_FLASH_LEGACY_PROGRAMMER_END-1U)

//8K给编程器工厂和用户，0.5K*2给系统参数

#define FLASH_ADDR_USERBLOCK_DATA            WRITE_START_ADDR   //UserBlock 共1K
#define FLASH_ADDR_FACTORY_USER_DATA        (WRITE_START_ADDR+1024)   //前128字节是工厂参数，紧跟着是128字节用户参数。一个型号预留三个通道所以768 * 8 共 6K
#define FLASH_ADDR_PROGRAMMER_DATA          (FLASH_ADDR_FACTORY_USER_DATA+1024*6)  //编程器参数1024 共1K
#define ONE_DEVICE_SIZE                     768          //前128字节是工厂参数，紧跟着是128字节用户参数,一个型号预留三个通道.所以一个驱动器占用768字节。




#endif

