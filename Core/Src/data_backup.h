#ifndef DATA_BACKUP_H
#define DATA_BACKUP_H

#include "common.h"

typedef enum
{
    DATA_BACKUP_SYS_DATA=0,
    DATA_BACKUP_USER_TOTAL
}data_backup_user_en;

/************************************
功能描述：写参数
输入参数：addr flash所在地址，buf数据指针，size写入的字节数，包括3个标记字节, addr_main 主区的地址，addr_backup 备份区的地址
输出返回：无
*************************************/
extern boolean_en data_backup_store_data(u8* buf, u16 size, u32 addr_main, u32 addr_backup, data_backup_user_en user);

/************************************
功能描述：读参数，
输入参数：addr flash所在地址，buf数据指针，size读取的字节数，包括3个标记字节, addr_main 主区的地址，addr_backup 备份区的地址
输出返回：无
*************************************/
extern boolean_en data_backup_load_data(u8* buf, u16 size, u32 addr_main, u32 addr_backup, data_backup_user_en user);


extern void data_backup_timer(void);

extern void data_backup_process(void);

#endif

