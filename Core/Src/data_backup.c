/*************************************************************
程序功能：参数的数据备份，两个扇区备份同一套数据。数据最后3个字节做标记，最后两个字节是CRC校验，倒数第3个字节是序号每写一次加1，到达0xff加1变0。
开发环境：keil 5.36 c51 v9.60
芯片型号：
开发人员：梁庆能
单位名称：广东东菱电源科技有限公司
创建日期：2024.7.10
*************************************************************/
#include "data_backup.h"
#include "hw_flash.h"
//#include "user_rom_and_foctory_rom.h"
#include "sys_data.h"

#define STATUS_BOTH_ERROR         0x00 //主区与备份区都校验出错
#define STATUS_ONLY_MAIN_OK       0x01 //主区校验正确，备份区校验出错
#define STATUS_ONLY_BACKUP_OK     0x02 //主区校验出错，备份区校验正确
#define STATUS_BOTH_OK            0x03 //主区校验正确，备份区校验正确
#define USER_FLASH_ERASE(addr)                      user_flash_erase(addr)
#define USER_FLASH_WRITE(buf, addr_main, size)      user_flash_write(addr_main, buf, size)
#define USER_FLASH_CHECK(buf, addr_main, size)      user_flash_check(addr_main, buf, size)
#define USER_FLASH_READ(buf, addr_backup, size)     hw_flash_read_bytes(addr_backup, buf, size)

typedef enum
{
    DATA_BACKUP_INDEX_MAIN = 0,//主区最新，或者一样新
    DATA_BACKUP_INDEX_BACKUP   //备份区最新
}data_backup_index_st;


typedef struct 
{
    u8 main:    1;  //b0  主区校验正确是1，否则是0
    u8 backup:  1;  //b1 备份区校验正确是1，否则是0
    u8 :  6;
}data_backup_status_st;
typedef union
{
    data_backup_status_st st;
    u8 byte;
}data_backup_status_un;

extern boolt struct_data_check(void* dat, u16 size);
extern void struct_data_checksum_make(void* dat, u16 size);
extern u8 xdata rom_buf[FLASH_PAGE_SIZE];

static data_backup_status_un xdata _status[DATA_BACKUP_USER_TOTAL]; 
static data_backup_index_st xdata _who_is_newest[DATA_BACKUP_USER_TOTAL]; 
static boolean_en flag_read_recover = BOOL_FALSE; //上电1秒后校对数据时才允许写
static boolean_en flag_startup_read = BOOL_FALSE; //启动时只校对一次

static u8 xdata _timer_for_corrent = 100;
boolean_en flag_write_flash_integrity = BOOL_FALSE; //为了防止程序跑飞保证整个程序执行的完整性。flag_write_flash_integrity为1时才允许写

/************************************
功能描述：写参数
输入参数：addr flash所在地址，buf数据指针，size写入的字节数，包括3个标记字节, addr_main 主区的地址，addr_backup 备份区的地址
输出返回：无
*************************************/
boolean_en data_backup_store_data(u8* buf, u16 size, u32 addr_main, u32 addr_backup, data_backup_user_en user)
{
    boolean_en b = BOOL_TRUE;
    u32 t,t2;
     #include "sys_tick.h"
     t= sys_tick_get_tick();
    //u32 system_tick;
    //log_u16(2, user);
    //log_u16(3, addr_main);
    //log_u16(4, size);
    ++buf[size-3];
    struct_data_checksum_make((void*)buf, size);
//    printf("size=%d,",size);
//    printf("addr_main=%d,",addr_main);
//    printf("addr_backup=%d,",addr_backup);
//    printf("user=%d,\n",(u16)user);
    if(_status[user].byte == STATUS_BOTH_ERROR)
    {
        //两个区校验都错
        b = USER_FLASH_ERASE(addr_main);
        if(b == BOOL_TRUE)
        {
            b = USER_FLASH_WRITE(buf, addr_main, size);
            if(b == BOOL_TRUE && USER_FLASH_CHECK(buf, addr_main, size))
            {
                //log_u16(1, 0);
                //保证第一块安全的前提下写另外一块
                _status[user].st.main = 1;
                _who_is_newest[user] = DATA_BACKUP_INDEX_MAIN;
                b = USER_FLASH_ERASE(addr_backup);
                if(b == BOOL_TRUE)
                {
                    b = USER_FLASH_WRITE(buf, addr_backup, size);
                    if(b == BOOL_TRUE && USER_FLASH_CHECK(buf, addr_backup, size))
                    {
                        //log_u16(1, 2);
                        _status[user].st.backup = 1;
                    }
                    else
                    {
                        _status[user].st.backup = 0;
                        //log_u16(1, 3);
                    }
                }
            }
            else
            {
                //log_u16(1, 1);
                _status[user].st.main = 0;
            }
        }
    }
    else if(_status[user].byte == STATUS_ONLY_BACKUP_OK || (_status[user].byte == STATUS_BOTH_OK && _who_is_newest[user] == DATA_BACKUP_INDEX_BACKUP))
    {
        //只有备份区的数据是对的，写主区。如果两个校验都对，写旧版本的主区。
        b = USER_FLASH_ERASE(addr_main);
        if(b == BOOL_TRUE)
        {
            b = USER_FLASH_WRITE(buf, addr_main, size);
            if(b == BOOL_TRUE && USER_FLASH_CHECK(buf, addr_main, size))
            {
               // printf("write ok\n");
                //保证第一块安全的前提下写另外一块
                _status[user].st.main = 1;
                _who_is_newest[user] = DATA_BACKUP_INDEX_MAIN;
                b = USER_FLASH_ERASE(addr_backup);
                if(b == BOOL_TRUE)
                {
                    b = USER_FLASH_WRITE(buf, addr_backup, size);
                    if(b == BOOL_TRUE && USER_FLASH_CHECK(buf, addr_backup, size))
                    {
                        _status[user].st.backup = 1;
                        //log_u16(1, 5);
                    }
                    else
                    {
                        //log_u16(1, 6);
                        _status[user].st.backup = 0;
                    }
                }
            }
            else
            {
                //log_u16(1, 4);
                _status[user].st.main = 0;
            }
        }
       // printf("2\n");
    }
    else if(_status[user].byte == STATUS_ONLY_MAIN_OK || (_status[user].byte == STATUS_BOTH_OK && _who_is_newest[user] == DATA_BACKUP_INDEX_MAIN))
    {
        //只有主区的数据是对的，写备份区。如果两个校验都对，旧版本的备份区先写。
        b = USER_FLASH_ERASE(addr_backup);
        if(b == BOOL_TRUE)
        {
            b = USER_FLASH_WRITE(buf, addr_backup, size);
            if(b == BOOL_TRUE && USER_FLASH_CHECK(buf, addr_backup, size))
            {
              // printf("write ok2\n");
               //保证第一块安全的前提下写另外一块
                _status[user].st.backup = 1;
                _who_is_newest[user] = DATA_BACKUP_INDEX_BACKUP;
                b = USER_FLASH_ERASE(addr_main);
                if(b == BOOL_TRUE)
                {
                    b = USER_FLASH_WRITE(buf, addr_main, size);
                    if(b == BOOL_TRUE && USER_FLASH_CHECK(buf, addr_main, size))
                    {
                        _status[user].st.main = 1;
                        //log_u16(1, 8);
                    }
                    else
                    {
                        _status[user].st.main = 0;
                        //log_u16(1, 9);
                    }
                }
            }
            else
            {
                //log_u16(1, 7);
                _status[user].st.backup = 0;
            }
        }        
    }
     t2= sys_tick_get_tick();
    log_u32(100, t2-t);
    return (b);
}




/************************************
功能描述：读参数，
输入参数：addr flash所在地址，buf数据指针，size读取的字节数，包括3个标记字节, addr_main 主区的地址，addr_backup 备份区的地址
输出返回：无
*************************************/
boolean_en data_backup_load_data(u8* buf, u16 size, u32 addr_main, u32 addr_backup, data_backup_user_en user)
{
    u16 xdata sn;
    u16 xdata sn_bak;
   // u16 i;
//    printf("size2=%d,",size);
//    printf("addr_main2=%d,",addr_main);
//    printf("addr_backup2=%d,",addr_backup);
//    printf("user2=%d,\n",(u16)user);
    data_backup_load_data_lab:
    USER_FLASH_READ(buf, addr_backup, size);
    if(struct_data_check((void*)buf,size))
    {
        sn_bak = buf[size-3];
       // printf("sn_bak=%x\n",(u16)sn_bak);
        _status[user].st.backup = 1;
    }
    else
    {
        _status[user].st.backup = 0;
    }
    USER_FLASH_READ(buf, addr_main, size);
    if(struct_data_check((void*)buf,size))
    {
        sn = buf[size-3];
        //printf("sn=%x\n",(u16)sn);
        _status[user].st.main = 1;
    }
    else
    {
        _status[user].st.main = 0;
    }
    
    // printf("byte=%x\n",(u16)_status[user].byte);
    if(_status[user].byte == STATUS_BOTH_ERROR)
    {
        //两个区校验出错，采用默认值
        return BOOL_FALSE;
    }
    else if(_status[user].byte == STATUS_ONLY_BACKUP_OK)
    {
        //只有备份区的数据是对的。重新读出来并重新较验
        USER_FLASH_READ(buf, addr_backup, size);
        if(!(struct_data_check((void*)buf,size)))
        {
            goto data_backup_load_data_lab;
        }
        else
        {
            //  数据更新到主区, 下次就不用读3次
            if(flag_read_recover)
            {
                //log_u16(16, 1);
                USER_FLASH_ERASE(addr_main);
                USER_FLASH_WRITE(buf, addr_main, size);
            }
        }
    }
    else if(_status[user].byte == STATUS_ONLY_MAIN_OK)
    {
        //只有主区的数据是对的。主区的数据已经在 buf 并校验成功
        //数据更新到备份区, 保持两个区数据完整
        if(flag_read_recover)
        {
            //log_u16(16, 3);
            USER_FLASH_ERASE(addr_backup);
            USER_FLASH_WRITE(buf, addr_backup, size);
        }
    }
    else if(_status[user].byte == STATUS_BOTH_OK)
    {
        //两个区的数据都是对的。
        if(sn-sn_bak < (u16)0x7ff)  //这个地方----------------------
        {            
            //主区的数据更新一点或者一样新。主区的数据已经在 buf 
            _who_is_newest[user] = DATA_BACKUP_INDEX_MAIN;
            if(sn != sn_bak)
            {
                //主区比备份区要新，数据更新到备份区, 保持两个区数据完整
                if(flag_read_recover)
                {
                    //log_u16(16, 4);
                    USER_FLASH_ERASE(addr_backup);
                    USER_FLASH_WRITE(buf, addr_backup, size);
                }
            }
        }
        else
        {
            //备份区的数据更新一点
            _who_is_newest[user] = DATA_BACKUP_INDEX_BACKUP;
            USER_FLASH_READ(buf, addr_backup, size);            
            if(!(struct_data_check((void*)buf,size)))
            {
                goto data_backup_load_data_lab;
            }
            else
            {
                //数据更新到主区, 下次就不用读3次
                if(flag_read_recover)
                {
                    //log_u16(16, 2);
                    USER_FLASH_ERASE(addr_main);
                    USER_FLASH_WRITE(buf, addr_main, size);
                }
            }
        }
        //printf("_who_is_newest=%x\n",(u16)_who_is_newest[user]);
    }   

    
    //log_u16(20, user);
    //log_u16(19, _who_is_newest[user]);
    //log_u16(18, _status[user].byte);
    return BOOL_TRUE;
}


void data_backup_timer(void)
{
    if(_timer_for_corrent > 0)
    {
        --_timer_for_corrent;
    }
}


void data_backup_process(void)
{
    if(flag_startup_read==BOOL_FALSE && _timer_for_corrent==BOOL_FALSE)
    {
        flag_startup_read = BOOL_TRUE; 
        flag_read_recover = BOOL_TRUE;
        flag_write_flash_integrity = BOOL_TRUE;
        if(_status[DATA_BACKUP_SYS_DATA].byte == STATUS_ONLY_MAIN_OK || _status[DATA_BACKUP_SYS_DATA].byte == STATUS_ONLY_BACKUP_OK)
        {
           // printf("reread3\n");
           data_backup_load_data((u8*)&sys_data, sizeof(sys_data), FLASH_ADDR_SYS_DATA, FLASH_ADDR_SYS_DATA_BACKUP, DATA_BACKUP_SYS_DATA);
        }
        else if(_status[DATA_BACKUP_SYS_DATA].byte == STATUS_BOTH_ERROR)
        {
            //printf("sys_data_default\n");
            sys_data_default();//这个地方要补充
            printf("save1\n");
            sys_data_store();
        }
        flag_read_recover = BOOL_FALSE;
        flag_write_flash_integrity = BOOL_FALSE;
    }
}





