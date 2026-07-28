#ifndef OTA_H_
#define OTA_H_
#include "common.h"

#define OTA_LOCAL_FIRMWARE_NAME "cat1.bin"

typedef enum
{
    CONNECT_OTA_STATE_IDLE,
    CONNECT_OTA_RESETING,
    CONNECT_OTA_READY,
    CONNECT_OTA_AT_QHTTPCFG_CINFIG,
    CONNECT_OTA_AT_QMTDISC,
    CONNECT_OTA_AT_QMTT_CLOSE,
    CONNECT_OTA_AT_RAW_QICLOSE,
    CONNECT_OTA_AT_RAW_QIOPEN,
    CONNECT_OTA_AT_RAW_QISEND_QUERY,
    CONNECT_OTA_AT_RAW_QISEND,
    CONNECT_OTA_AT_RAW_QISEND_DATA,
    CONNECT_OTA_AT_RAW_WAIT_SEND_OK,
    CONNECT_OTA_AT_RAW_WAIT_DATA,
    CONNECT_OTA_AT_RAW_QIRD,
    CONNECT_OTA_AT_RAW_QIRD_DATA,
    CONNECT_OTA_AT_RAW_QIRD_TRAILER,
    CONNECT_OTA_AT_RAW_CLOSE,
    CONNECT_OTA_AT_QHTTPCFG_contextid,
    CONNECT_OTA_AT_QHTTPCFG_responseheader,
    CONNECT_OTA_AT_QHTTPCFG_rspout_auto,
    CONNECT_OTA_AT_QHTTPCFG_requestheader,
    CONNECT_OTA_AT_QHTTPCFG_header_close,
    CONNECT_OTA_AT_QHTTPURL,
    CONNECT_OTA_AT_QHTTPURL_WAIT_OK,
    CONNECT_OTA_AT_QFDEL,
    CONNECT_OTA_AT_QFLDS,
    CONNECT_OTA_AT_QHTTPGET,
    CONNECT_OTA_AT_QHTTPGET_HEADER,
    CONNECT_OTA_AT_QHTTPGET_WAIT,
    CONNECT_OTA_AT_QHTTPREAD,
    CONNECT_OTA_AT_QHTTPREAD_WAIT_CONNECT,
    CONNECT_OTA_AT_QHTTPREAD_STREAM,
    CONNECT_OTA_AT_QHTTPREAD_TRAILER,
    CONNECT_OTA_AT_STREAM_VERIFY,
    //下载固件到UFS
    CONNECT_OTA_AT_QHTTPREADFILE,                  //通过 UART/USB 读取 HTTP(S)服务器响应信息<file_name>[,<wait_time>]
    CONNECT_OTA_AT_QHTTPREADFILE_STROE_WAIT,
    CONNECT_OTA_AT_QHTTPREADFILE_QFLST_DIAG,
    CONNECT_OTA_AT_QHTTPSTOP_CLEANUP,
    CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH,     //+QHTTPREADFILE: 0

} CONNECT_OTA_state_en;





typedef enum 
{ 

    //下载固件到MCU
    MCU_OTA_STATE_IDLE,
    MCU_OTA_STATE_RESETING,
    MCU_OTA_AT_QFLST,                          //列出存储媒介中的文件信息+QFLST: "UFS:gateway.hex",152937   //+QFLST: <filename>,<file_size>
    MCU_OTA_STATE_GETFILESIZE,
    MCU_OTA_STATE_QFDWL,
    MCU_OTA_STATE_QFDWL_GET_FIRMWARE,
    MCU_OTA_AT_QFOPEN,                         //打开文件获取文件指针。+QFOPEN: 1
    MCU_OTA_AT_QFSEEK,                         //设置文件指针为文件的初始位置。
    MCU_OTA_AT_QFREAD,                         //AT+QFREAD=1,10 //读取数据。   MCU 10  -><Read Data>     OK 
    MCU_OTA_MCU_GETDATA,   
    MCU_OTA_AT_QFREAD_LOOP,                    //循环读取
    MCU_OTA_GET_BIG_PICK,                   //获取服务器大片
    MCU_OTA_AT_QFCLOSE,                        //AT+QFCLOSE=1 //关闭文件。
    MCU_OTA_MCU_FINISH,                        //已下载到MCU
    MCU_OTA__COMPLETE,

} MCU_OTA_state_en;
extern    MCU_OTA_state_en  MCU_OTA_state;
void _4G_OTA_machine(void);
void  mcu_copy_firmware_machine(void)  ;
void  _4G_OTA_machine_star(void) ;
void  _4G_OTA_machine_contextid(void);
boolean_en  _4G_OTA_machine_finish(void);
boolean_en  _4G_OTA_machine_getdata(void);
boolean_en  mcu_copy_firmware_finish(void);
boolean_en  mcu_copy_firmware_getdata(void);
void mcu_copy_firmware_machine(void)  ;
void  mcu_copy_firmware_star(void) ;
void mcu_copy_firmware_set_readloop( void);
extern uint16 pack_length;
extern u32 congfig_delay_timer;
extern char firm_name_buffer[256];
/**
*@brief   解析OTA模块上报的URC 
*@param	  buf：模块上报的URC
*@return  无
*/

 void OTA_STROE_MCU(uint8 *buf,u16 lenth) ;


#endif
