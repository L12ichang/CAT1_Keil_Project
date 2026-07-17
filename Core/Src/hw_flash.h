
#ifndef HW_FLASH_H
#define HW_FLASH_H

#include "common.h"

#define FLASH_ADDR_SYS_DATA             0x8005000
#define FLASH_ADDR_SYS_DATA_BACKUP      0x8006800   //开始地址0x200，占用 512, 系统参数

extern void hw_flash_test(void);


// 函数原型
extern void hw_flash_write(u32 address, u32 *data, u32 length);
extern HAL_StatusTypeDef hw_flash_read(u32 address, u32 *data, u32 length);
extern void hw_flash_write_bytes(uint32_t flash_addr, u8 *buffer, uint32_t length);
extern HAL_StatusTypeDef hw_flash_update_bytes_checked(uint32_t flash_addr,
                                                       const u8 *buffer,
                                                       uint32_t length);
extern HAL_StatusTypeDef hw_flash_program_bytes_checked(uint32_t flash_addr,
                                                        const u8 *buffer,
                                                        uint32_t length);
extern void hw_flash_read_bytes(u32 address, u8 *data, u32 length);
extern boolean_en user_flash_check(u32 addr, u8* buf, u16 size);
extern boolean_en user_flash_erase(u32 addr);
extern boolean_en user_flash_write(u32 addr, u8* buf, u32 size);


#endif
