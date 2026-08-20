/*************************************************************
程序功能：Flash 分页读改写事务核心
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：黎长彩
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "hw_flash_page_writer.h"

static boolean_en hw_flash_page_writer_context_valid(
    const hw_flash_page_writer_context_st *context)
{
    if (context == NULL || context->page_size == 0U || context->page_buffer == NULL ||
        context->read_page == NULL || context->erase_page == NULL ||
        context->program_page == NULL || context->check_range == NULL)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

/************************************
功能描述：按擦除页执行带读改写、擦除、编程和回读检查的分页写入
输入参数：flash_addr 起始地址；buffer 源数据；length 字节数；context 页写入回调和缓存
输出返回：所有页完成且每页请求范围回读成功 BOOL_TRUE，否则 BOOL_FALSE
************************************/
boolean_en hw_flash_write_bytes_paged(
    u32 flash_addr,
    const u8 *buffer,
    u32 length,
    const hw_flash_page_writer_context_st *context)
{
    u32 current_addr;
    u32 remaining;
    u32 page_addr;
    u32 page_offset;
    u32 write_length;
    const u8 *write_buffer;
    u32 i;

    if (length == 0U)
    {
        return BOOL_TRUE;
    }
    if (buffer == NULL || hw_flash_page_writer_context_valid(context) != BOOL_TRUE ||
        flash_addr > (0xFFFFFFFFUL - (length - 1U)))
    {
        return BOOL_FALSE;
    }

    current_addr = flash_addr;
    remaining = length;
    write_buffer = buffer;
    while (remaining != 0U)
    {
        page_addr = current_addr - (current_addr % context->page_size);
        page_offset = current_addr - page_addr;
        write_length = context->page_size - page_offset;
        if (remaining < write_length)
        {
            write_length = remaining;
        }

        if ((page_offset != 0U || write_length != context->page_size) &&
            context->read_page(page_addr, context->page_buffer,
                               context->page_size) != BOOL_TRUE)
        {
            return BOOL_FALSE;
        }
        for (i = 0U; i < write_length; ++i)
        {
            context->page_buffer[page_offset + i] = write_buffer[i];
        }
        if (context->erase_page(page_addr) != BOOL_TRUE ||
            context->program_page(page_addr, context->page_buffer,
                                  context->page_size) != BOOL_TRUE ||
            context->check_range(current_addr, write_buffer, write_length) != BOOL_TRUE)
        {
            return BOOL_FALSE;
        }

        /* Keep address, remaining length, source and readback ranges in lockstep. */
        current_addr += write_length;
        remaining -= write_length;
        write_buffer += write_length;
    }
    return BOOL_TRUE;
}
