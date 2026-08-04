#ifndef HW_FLASH_PAGE_WRITER_H
#define HW_FLASH_PAGE_WRITER_H

#include "type.h"

#define HW_FLASH_PAGE_WRITER_VERSION 1U

typedef boolean_en (*hw_flash_page_read_fn)(u32 page_addr,
                                            u8 *page_buffer,
                                            u32 page_size);
typedef boolean_en (*hw_flash_page_erase_fn)(u32 page_addr);
typedef boolean_en (*hw_flash_page_program_fn)(u32 page_addr,
                                               const u8 *page_buffer,
                                               u32 page_size);
typedef boolean_en (*hw_flash_range_check_fn)(u32 flash_addr,
                                              const u8 *buffer,
                                              u32 length);

typedef struct
{
    u32 page_size;
    u8 *page_buffer;
    hw_flash_page_read_fn read_page;
    hw_flash_page_erase_fn erase_page;
    hw_flash_page_program_fn program_page;
    hw_flash_range_check_fn check_range;
} hw_flash_page_writer_context_st;

/************************************
功能描述：按擦除页执行带读改写、擦除、编程和回读检查的分页写入
输入参数：flash_addr 起始地址；buffer 源数据；length 字节数；context 页写入回调和缓存
输出返回：所有页完成且每页请求范围回读成功 BOOL_TRUE，否则 BOOL_FALSE
注意：每次翻页都会推进当前源指针，不能使用原始 buffer 重复合并下一页。
************************************/
extern boolean_en hw_flash_write_bytes_paged(
    u32 flash_addr,
    const u8 *buffer,
    u32 length,
    const hw_flash_page_writer_context_st *context);

#endif
