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
#include "data_backup.h"
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
    fac_128_data_default();//工厂128字节默认值
}

boolean_en flash_store(u8* buf, u16 size, u32 addr_main)
{
    hw_flash_write_bytes(addr_main, buf, size);
    if (user_flash_check(addr_main, buf, size) != BOOL_TRUE)
    {
        printf("flash_store: verify fail addr=0x%08x size=%u\n", (unsigned int)addr_main, (unsigned int)size);
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}
boolean_en data_store_data(u8* buf, u16 size, u32 addr_main)
{
    if(size>6*1024||addr_main+size>APROM_STARTADDR)
    {
      return BOOL_FALSE;
    }
    if(memcmp((const void *)addr_main, buf, size) == 0)
    {
      return BOOL_TRUE;
    }
    return (hw_flash_update_bytes_checked(addr_main, buf, size) == HAL_OK) ?
           BOOL_TRUE : BOOL_FALSE;
}

boolean_en data_load_data(u8* buf, u16 size, u32 addr_main)
{
  hw_flash_read_bytes( addr_main,buf, size);
  return BOOL_TRUE;
}



/************************************
功能描述：从flash加载用户数据
输入参数：     无
输出返回：无
*************************************/
void sys_data_load(void)
{ 
   #if  1
       
         if(data_load_data((u8*)&sys_data, sizeof(sys_data), DATAROM_STARTADDR)==BOOL_TRUE)
        {
             if( struct_data_check((u8*)&sys_data,sizeof(sys_data)))//主区校验OK
            {
                printf("主区数据\n");
                log_u32(10, 0);
                factory_user_load_data();   
               
            }
            else   
            {
                   data_load_data((u8*)&sys_data, sizeof(sys_data), BAKDATAROM_STARTADDR) ; 
                  if( struct_data_check((u8*)&sys_data,sizeof(sys_data)))//校验备份区
                  {
                      printf("备份区数据\n");
                     factory_user_load_data(); 
                  }
                  else   //备份区也错，就默认
                  {
                    sys_data_default(); 
                  }
                   
            }
        }
        
        else
        {
           //printf("default2\n");
             log_u32(10, 1);

           
        }
      #else
         if(data_load_data((u8*)&sys_data, sizeof(sys_data), DATAROM_STARTADDR)==BOOL_FALSE)
        {
               //printf("default\n");
                log_u32(10, 0);
               sys_data_default();
        } 
        else
        {
              //printf("default2\n");
             log_u32(10, 1);
            if(sys_data.mac==0xffffffff)
            {
               sys_data.mac =0x80;      
            } 
            factory_user_load_data();    
        }       

      #endif

}


/************************************
功能描述：把用户数据保存到flash
输入参数：无
输出返回：无
*************************************/
boolean_en sys_data_store_checked(void)
{
    #if 1 
    //新算法：写数据同时写主区副区 
   //读数据校验主区OK导出数据，若NG导副区然后写主副两区，若主副两区都NG按默认配置！
       struct_data_checksum_make((u8*)&sys_data,sizeof(sys_data));
    if(data_store_data((u8*)&sys_data, sizeof(sys_data), DATAROM_STARTADDR) == BOOL_FALSE)
    {
        printf("sys_data_store fail\n");
        return BOOL_FALSE;
    }
     if(data_store_data((u8*)&sys_data, sizeof(sys_data), BAKDATAROM_STARTADDR) == BOOL_FALSE)  //数据备份
    {
        printf("sys_data_store_bakrom_fail\n");
        return BOOL_FALSE;
    }
   #else     //旧启用备份功能会导致掉电数据异常
        if(data_backup_store_data((u8*)&sys_data, sizeof(sys_data), DATAROM_STARTADDR,BAKDATAROM_STARTADDR,DATA_BACKUP_SYS_DATA) == BOOL_FALSE)
    {
        printf("sys_data_store fail\n");
        return BOOL_FALSE;
    }
   #endif
    return BOOL_TRUE;
}

void sys_data_store(void)
{
    (void)sys_data_store_checked();
}

