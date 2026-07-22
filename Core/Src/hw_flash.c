/*************************************************************
程序功能：CAT.1智慧电源
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "hw_flash.h"
#include "flash_allocation.h"
#include "stm32f1xx_hal.h"
u8 temp_buf_byte[FLASH_PAGE_SIZE] __attribute__ ((aligned(4)));
/* The checked RMW path must never build recovery data by reading a page after
 * erase/program has already failed.  Keep the exact pre-erase image apart
 * from temp_buf_byte, which holds the requested new page. */
static u8 hw_flash_original_page[FLASH_PAGE_SIZE] __attribute__ ((aligned(4)));
static volatile boolean_en hw_flash_checked_fault = BOOL_FALSE;
void memcpy_u32(u32 *target, u32 *source, u32 length)
{
    int i;
    for(i=0; i<length; i++)
    {
        target[i] = source[i];
    }
}
void memcpy_u8(u8 *target, u8 *source, u32 length)
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

/*
flash_addr 起始地址
buffer u8指针
length 1字节为单位
*/
void hw_flash_write_bytes(uint32_t flash_addr, u8 *buffer, uint32_t length)
{
    uint32_t start_sector = flash_addr / FLASH_PAGE_SIZE;
    uint32_t end_sector = (flash_addr + length  - 1) / FLASH_PAGE_SIZE;
    uint32_t sector;
    FLASH_EraseInitTypeDef erase_init;
    u32 page_error = 0;
    uint32_t sector_addr;
    uint32_t sector_offset;
    uint32_t write_length;

    HAL_FLASH_Unlock();

    for (sector = start_sector; sector <= end_sector; sector++) 
    {
        sector_addr = sector * FLASH_PAGE_SIZE;    //所在的扇区号
        sector_offset = flash_addr - sector_addr;  //扇区内的偏移地址
        write_length = FLASH_PAGE_SIZE - sector_offset; //需要数据更新的长度，字节为单位

        if (length < write_length ) 
        {
            write_length = length;
        }
        // Read the sector data into the temp_buf
        //memcpy(temp_buf, (void *)sector_addr, FLASH_PAGE_SIZE);
        if(!(sector_offset==0 && write_length==FLASH_PAGE_SIZE))
        {
            hw_flash_read_bytes(sector_addr, temp_buf_byte, FLASH_PAGE_SIZE);
        }
        // Update the temp_buf with the data to be written
        memcpy_u8(temp_buf_byte + sector_offset, buffer, write_length);
        // Erase the sector
        //FLASH_PageErase(sector_addr);

        // 擦除扇区
        erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
        erase_init.PageAddress = sector_addr;
        erase_init.NbPages = 1;
        

        HAL_FLASHEx_Erase(&erase_init, &page_error);
        // Write the updated temp_buf data back to the sector
        for (uint32_t i = 0; i < FLASH_PAGE_SIZE; i+=4) 
        {
          HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, sector_addr + i, *((u32*)(temp_buf_byte+i)));
        }

        // Update the buffer pointer and remaining length
        buffer += write_length;
        length -= write_length;
        flash_addr += write_length;
    }

    HAL_FLASH_Lock();

}

static HAL_StatusTypeDef hw_flash_program_full_page_checked(
    u32 sector_addr,
    const u8 *page_image)
{
    FLASH_EraseInitTypeDef erase_init;
    HAL_StatusTypeDef status;
    u32 page_error;
    u32 i;

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK)
    {
        return status;
    }
    page_error = 0U;
    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.PageAddress = sector_addr;
    erase_init.NbPages = 1U;
    status = HAL_FLASHEx_Erase(&erase_init, &page_error);
    if (status == HAL_OK)
    {
        for (i = 0U; i < FLASH_PAGE_SIZE; i += 4U)
        {
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                       sector_addr + i,
                                       *((u32 *)(page_image + i)));
            if (status != HAL_OK)
            {
                break;
            }
        }
    }
    (void)HAL_FLASH_Lock();
    if (status != HAL_OK)
    {
        return status;
    }
    return (user_flash_check(sector_addr, (u8 *)page_image,
                             FLASH_PAGE_SIZE) == BOOL_TRUE) ?
           HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef hw_flash_update_bytes_checked(uint32_t flash_addr,
                                                const u8 *buffer,
                                                uint32_t length)
{
    HAL_StatusTypeDef status;
    HAL_StatusTypeDef restore_status;
    u32 sector_addr;
    u32 sector_offset;
    u8 restore_attempt;

    if (buffer == NULL || length == 0U)
    {
        return HAL_ERROR;
    }
    sector_addr = flash_addr - (flash_addr % FLASH_PAGE_SIZE);
    sector_offset = flash_addr - sector_addr;
    if ((sector_offset + length) > FLASH_PAGE_SIZE)
    {
        return HAL_ERROR;
    }

    hw_flash_read_bytes(sector_addr, hw_flash_original_page, FLASH_PAGE_SIZE);
    memcpy_u8(temp_buf_byte, hw_flash_original_page, FLASH_PAGE_SIZE);
    memcpy_u8(temp_buf_byte + sector_offset, (u8 *)buffer, length);
    if (memcmp(temp_buf_byte, hw_flash_original_page, FLASH_PAGE_SIZE) == 0)
    {
        return HAL_OK;
    }

    status = hw_flash_program_full_page_checked(sector_addr, temp_buf_byte);
    if (status == HAL_OK)
    {
        return HAL_OK;
    }

    /* Never report a command successful after any erase/program/readback
     * fault.  Best-effort restoration uses only the independent pre-erase
     * image; it deliberately does not read or merge the damaged page. */
    hw_flash_checked_fault = BOOL_TRUE;
    restore_status = HAL_ERROR;
    for (restore_attempt = 0U; restore_attempt < 2U; ++restore_attempt)
    {
        restore_status = hw_flash_program_full_page_checked(
            sector_addr, hw_flash_original_page);
        if (restore_status == HAL_OK)
        {
            break;
        }
    }
    (void)restore_status;
    return status;
}

boolean_en hw_flash_update_fault_latched(void)
{
    return hw_flash_checked_fault;
}

/*
 * Program already-erased aligned words without erasing the containing page.
 * This is intentionally separate from the page read/modify/write helper so a
 * record validity marker can be committed as the final atomic Flash step.
 */
HAL_StatusTypeDef hw_flash_program_bytes_checked(uint32_t flash_addr,
                                                 const u8 *buffer,
                                                 uint32_t length)
{
    HAL_StatusTypeDef status;
    u32 offset;
    u32 current_word;
    u32 requested_word;

    if (buffer == NULL || length == 0U ||
        (flash_addr & 3U) != 0U || (length & 3U) != 0U)
    {
        return HAL_ERROR;
    }

    for (offset = 0U; offset < length; offset += 4U)
    {
        current_word = *((volatile u32 *)(flash_addr + offset));
        memcpy(&requested_word, buffer + offset, sizeof(requested_word));
        if ((current_word & requested_word) != requested_word)
        {
            return HAL_ERROR;
        }
    }

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK)
    {
        return status;
    }
    for (offset = 0U; offset < length; offset += 4U)
    {
        memcpy(&requested_word, buffer + offset, sizeof(requested_word));
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                   flash_addr + offset,
                                   requested_word);
        if (status != HAL_OK)
        {
            break;
        }
    }
    (void)HAL_FLASH_Lock();
    if (status != HAL_OK)
    {
        return status;
    }
    return (user_flash_check(flash_addr, (u8 *)buffer, (u16)length) == BOOL_TRUE) ?
           HAL_OK : HAL_ERROR;
}







