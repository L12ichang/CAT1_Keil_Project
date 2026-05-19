#ifndef _OFFLINE_TIME_CONTROLLED_DIMMING_H  
#define _OFFLINE_TIME_CONTROLLED_DIMMING_H  
#include "common.h"
void offline_timer(void);
extern boolean_en power_status ; //开灯
extern void Work_offline_dimming_process(void);  //离线模式与服务器日同步
#endif

