
#ifndef HW_FLASH_H
#define HW_FLASH_H

#include "common.h"
#include "flash_address_assignment.h"

extern void hw_flash_test(void);


// 函数原型
extern void hw_flash_write(u32 address, u32 *data, u32 length);
extern HAL_StatusTypeDef hw_flash_read(u32 address, u32 *data, u32 length);
extern void hw_flash_write_bytes(uint32_t flash_addr, u8 *buffer, uint32_t length);
extern boolean_en hw_flash_write_bytes_checked(uint32_t flash_addr, const u8 *buffer, uint32_t length);
/* Program an already-erased, word-aligned range without erasing its page. */
extern boolean_en hw_flash_program_bytes_no_erase_checked(
    uint32_t flash_addr,
    const u8 *buffer,
    uint32_t length);
extern boolean_en hw_flash_erase_page_checked(uint32_t page_address);
extern void hw_flash_read_bytes(u32 address, u8 *data, u32 length);
extern boolean_en user_flash_check(u32 addr, u8* buf, u16 size);


#endif
