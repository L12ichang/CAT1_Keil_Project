/*************************************************************
程序功能：保存在flash里的系统数据。
开发环境：keil 5.36
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/

#include "sys_data.h"
#include "hw_flash.h"
#include "factory_user_data.h"
#include "sys_persistent_storage.h"
#include "zk_property.h"
#include "zk_work_plan.h"
sys_data_st sys_data  __attribute__ ((aligned(4)))={  .temp_protect.temp_protect=100,.temp_protect.enable=BOOL_TRUE};

#define FLAG_RESET_DATA   0x1113



/************************************
功能描述：数据校验
输入参数：     dat 被校验数据变量 , size是数据的结构体大小单位字节，包括校验字节在内。最后两个字节是校验结果，高字节在前
输出返回：正确返回true，错误返回false
*************************************/
boolt struct_data_check(void* dat, u16 size)
{
  u16 tmp=0xa5;//解决全0的bug
  u16 tmp2;
  u16 i;
  for (i=0; i<size-2;i++)
  {
    tmp+=*((u8*)dat+i);
  }
  tmp2=*((u8*)dat+i);
  ++i;
  tmp2=tmp2*256+*((u8*)dat+i);
  if (tmp==tmp2)
  {
    return (true);
  }
  else
  {
    return (false);
  }
}


/************************************
功能描述：生成校验，校验字节写到最后两个字节，高字节在前
输入参数：     dat 被校验数据变量 , size是数据的结构体大小单位字节，包括校验字节在内。
输出返回：无
*************************************/
void struct_data_checksum_make(void* dat, u16 size)
{
  u16 tmp=0xa5;//解决全0的bug
  u16 i;
  for (i=0; i<size-2;i++)
  {
    tmp+=*((u8*)dat+i);
  }  
  *((u8*)dat+i)=tmp/256;
  ++i;
  *((u8*)dat+i)=tmp%256;
}



/************************************
功能描述：设置为预置值。
输入参数：     无
输出返回：无
*************************************/
void sys_data_default(void)
{
    sys_data.mac=0x80;
    sys_data.lamp_power=100U;
    fac_128_data_default();//工厂128字节默认值
}

boolean_en flash_store(u8* buf, u16 size, u32 addr_main)
{
    if (buf == NULL || size == 0U ||
        addr_main < CAT1_FLASH_OTA_BACKUP_START ||
        addr_main > CAT1_FLASH_OTA_BACKUP_END ||
        (u32)size > CAT1_FLASH_OTA_BACKUP_END - addr_main)
    {
        return BOOL_FALSE;
    }
    hw_flash_write_bytes(addr_main, buf, size);
    if (user_flash_check(addr_main, buf, size) != BOOL_TRUE)
    {
        printf("flash_store: verify fail addr=0x%08x size=%u\n", (unsigned int)addr_main, (unsigned int)size);
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}
/************************************
功能描述：从flash加载用户数据
输入参数：     无
输出返回：无
*************************************/
void sys_data_load(void)
{
    u8 factory_and_device[SYS_PERSISTENT_CONFIG_FACTORY_LENGTH +
                          SYS_PERSISTENT_CONFIG_DEVICE_LENGTH];

    memset(&sys_data, 0, sizeof(sys_data));
    /* lamp_power is a RAM-only derating percentage; boot always starts at 100%. */
    sys_data.lamp_power = 100U;
    if (sys_persistent_config_read_section(
            SYS_PERSISTENT_CONFIG_FACTORY_OFFSET,
            factory_and_device,
            sizeof(factory_and_device),
            NULL) == BOOL_TRUE)
    {
        memcpy(sys_data.fa_Parambuf,
               factory_and_device,
               SYS_PERSISTENT_CONFIG_FACTORY_LENGTH);
        sys_data.mac = sys_persistent_get_u32_le(
            factory_and_device + SYS_PERSISTENT_CONFIG_FACTORY_LENGTH);
        factory_user_load_data();
        return;
    }

    /* Development V3 policy: defaults + one bounded 12 KiB format, no migration. */
    sys_data.mac = 0x80U;
    memset(sys_data.fa_Parambuf, 0xFF, sizeof(sys_data.fa_Parambuf));
    factory_user_load_data();
    if (sys_persistent_layout_initialize_with_defaults(
            sys_data.fa_Parambuf,
            sys_data.mac,
            zk_device_config_persistent_defaults,
            zk_work_plan_persistent_defaults,
            sys_persistent_ota_flag_is_set()) != BOOL_TRUE)
    {
        /* OTA-pending or Flash failure: defaults remain RAM-only and fail closed. */
        log_u32(10, 1);
    }
}


/************************************
功能描述：把用户数据保存到flash
输入参数：无
输出返回：无
*************************************/
boolean_en sys_data_store_checked(void)
{
    u8 factory_and_device[SYS_PERSISTENT_CONFIG_FACTORY_LENGTH +
                          SYS_PERSISTENT_CONFIG_DEVICE_LENGTH];

    memcpy(factory_and_device,
           sys_data.fa_Parambuf,
           SYS_PERSISTENT_CONFIG_FACTORY_LENGTH);
    sys_persistent_put_u32_le(
        factory_and_device + SYS_PERSISTENT_CONFIG_FACTORY_LENGTH,
        sys_data.mac);
    if (sys_persistent_config_update_section(
            SYS_PERSISTENT_CONFIG_FACTORY_OFFSET,
            factory_and_device,
            sizeof(factory_and_device),
            NULL) != BOOL_TRUE)
    {
        printf("sys_data_store fail\n");
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

void sys_data_store(void)
{
    (void)sys_data_store_checked();
}

