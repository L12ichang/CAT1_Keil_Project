/*************************************************************
程序功能：CAT.1智慧电源
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "hw_flash.h"
#include "hw_flash_page_writer.h"
#include "flash_allocation.h"
#include "stm32f1xx_hal.h"
u8 temp_buf_byte[FLASH_PAGE_SIZE] __attribute__ ((aligned(4)));
void memcpy_u32(u32 *target, u32 *source, u32 length)
{
    int i;
    for(i=0; i<length; i++)
    {
        target[i] = source[i];
    }
}
void memcpy_u8(u8 *target, const u8 *source, u32 length)
{
    int i;
    for(i=0; i<length; i++)
    {
        target[i] = source[i];
    }
}
// 从Flash读取函数. length单位是4字节
HAL_StatusTypeDef hw_flash_read(u32 address, u32 *data, u32 length)
{
    HAL_StatusTypeDef status = HAL_OK;

    for (u32 i = 0; i < length; i++)
    {
        //*(data + i) = *(__IO u32 *)(address + i * sizeof(u32));
        *(data + i) = *((__IO u32 *)address + i);
    }

    return status;
}

void hw_flash_read_bytes(u32 address, u8 *data, u32 length)
{
    u32 i;
    for (i = 0; i < length; i++)
    {
        *((u8 *)(data + i)) = *((u8 *)((u8 *)address + i));
    }
}



//----------------------------------------------------bakdata--------------------------------------------------------------------------
/************************************
功能描述：擦除flash. 擦除一页1046us
输入参数：无
输出返回：无
*************************************/
boolean_en user_flash_erase(u32 addr)
{
    FLASH_EraseInitTypeDef erase_init;
    u32 page_error = 0;
    HAL_StatusTypeDef status;
    HAL_FLASH_Unlock();
    // 擦除扇区
    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.PageAddress  = addr/FLASH_PAGE_SIZE;
    erase_init.NbPages = 1;
    //printf("erase sector=0x%x\n", erase_init.PageAddress-WRITE_START_ADDR);
    status = HAL_FLASHEx_Erase(&erase_init, &page_error);
    HAL_FLASH_Lock();
    return (boolean_en)(status==HAL_OK);
}


/************************************
功能描述：往flash里写数据。 写2048字节用时 23500us
输入参数：buf      待写入的数据缓存指针，size数据长度 单位是字节, 但必须是8的位数
输出返回：0 成功，1失败
*************************************/
boolean_en user_flash_write(u32 addr, u8* buf, u32 size)
{
    u32 i;
    boolean_en b=BOOL_TRUE;
    // 计算4字节对齐的长度
    if (size % 4 != 0) 
    {
        size = ((size / 4) + 1) * 4;
    }    
    HAL_FLASH_Unlock();
    for(i=0; i<size; i+=8)
    {           
        if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr + i, *((u64*)(buf+i))) != HAL_OK)
        {
            b = BOOL_FALSE;
        }
    }
    HAL_FLASH_Lock();
    return b;
}

/************************************
功能描述：对比数据是否正确
输入参数：buf      待对比的数据缓存指针，size数据长度 单位是字节
输出返回：正确 BOOL_TRUE， 错误 BOOL_FALSE
*************************************/
boolean_en user_flash_check(u32 addr, u8* buf, u16 size)
{
    u32 i;
    for (i = 0; i < size; i++)
    {
        if(*((u8 *)(buf + i)) != *((u8 *)((u8 *)addr + i)))
        {
            return(BOOL_FALSE);
        }
    }
    return(BOOL_TRUE);
}


//------------------------------------------------------------------------------------------------------------------------------

#if 0
// 写入Flash函数. length单位是4字节
HAL_StatusTypeDef hw_flash_write(u32 address, u32 *data, u32 length)
{
    HAL_StatusTypeDef status = HAL_OK;
    FLASH_EraseInitTypeDef erase_init;
    u32 page_error = 0;
//    if(address<WRITE_START_ADDR)
//    {
//        return HAL_ERROR;
//    }
    // 解锁Flash
    HAL_FLASH_Unlock();
    
    // 擦除扇区
    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.PageAddress = address;
    erase_init.NbPages = (length * sizeof(u32) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
    status = HAL_FLASHEx_Erase(&erase_init, &page_error);

    if (status == HAL_OK)
    {
        // 写入数据
        for (u32 i = 0; i < length; i++)
        {
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + i * sizeof(u32), *(data + i));
            if (status != HAL_OK)
            {
                break;
            }
        }
    }

    // 锁定Flash
    HAL_FLASH_Lock();

    return status;
}
#endif
extern void FLASH_PageErase(uint32_t PageAddress);

static boolean_en hw_flash_page_read(u32 page_addr, u8 *page_buffer, u32 page_size)
{
    hw_flash_read_bytes(page_addr, page_buffer, page_size);
    return BOOL_TRUE;
}

static boolean_en hw_flash_page_erase(u32 page_addr)
{
    FLASH_EraseInitTypeDef erase_init;
    u32 page_error = 0U;

    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.PageAddress = page_addr;
    erase_init.NbPages = 1U;
    return (HAL_FLASHEx_Erase(&erase_init, &page_error) == HAL_OK) ?
           BOOL_TRUE : BOOL_FALSE;
}

static boolean_en hw_flash_page_program(u32 page_addr,
                                        const u8 *page_buffer,
                                        u32 page_size)
{
    u32 i;

    if ((page_size % 4U) != 0U)
    {
        return BOOL_FALSE;
    }
    for (i = 0U; i < page_size; i += 4U)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              page_addr + i,
                              *((const u32 *)(page_buffer + i))) != HAL_OK)
        {
            return BOOL_FALSE;
        }
    }
    return BOOL_TRUE;
}

static boolean_en hw_flash_range_check(u32 flash_addr,
                                       const u8 *buffer,
                                       u32 length)
{
    if (length > 0xFFFFU)
    {
        return BOOL_FALSE;
    }
    return user_flash_check(flash_addr, (u8 *)buffer, (u16)length);
}

/*
功能描述：按页更新 Flash 并逐步返回擦除/编程错误
输入参数：flash_addr 起始地址；buffer 数据指针；length 字节数
输出返回：BOOL_TRUE 表示请求范围写入并回读成功，BOOL_FALSE 表示失败
注意：该接口仍会重写所在的完整擦除页，不能用于未经共享页审计的校准提交。
*/
boolean_en hw_flash_write_bytes_checked(uint32_t flash_addr, const u8 *buffer, uint32_t length)
{
    hw_flash_page_writer_context_st context;
    boolean_en result;

    context.page_size = FLASH_PAGE_SIZE;
    context.page_buffer = temp_buf_byte;
    context.read_page = hw_flash_page_read;
    context.erase_page = hw_flash_page_erase;
    context.program_page = hw_flash_page_program;
    context.check_range = hw_flash_range_check;

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return BOOL_FALSE;
    }
    result = hw_flash_write_bytes_paged(flash_addr, buffer, length, &context);
    HAL_FLASH_Lock();
    return result;
}

/*
功能描述：兼容旧调用方的按页写入入口
输入参数：flash_addr 起始地址；buffer 数据指针；length 字节数
输出返回：无
注意：新代码必须使用 hw_flash_write_bytes_checked() 检查底层错误。
*/
void hw_flash_write_bytes(uint32_t flash_addr, u8 *buffer, uint32_t length)
{
    (void)hw_flash_write_bytes_checked(flash_addr, buffer, length);
}







