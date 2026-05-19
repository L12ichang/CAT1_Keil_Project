#ifndef   HTTP_ACTIVE_H
#define   HTTP_ACTIVE_H


typedef enum
{
    HTTP_CONFIG_IDLE,
    HTTP_CONFIG_AT_START0,
     HTTP_CONFIG_AT_START,
    HTTP_CONFIG_AT_QHTTPCFG1,
    HTTP_CONFIG_AT_QHTTPCFG2,
    HTTP_CONFIG_AT_QHTTPURL_POST,//
    HTTP_CONFIG_AT_WAIT_CLOSE_HTTP,
    HTTP_CONFIG_AT_CLOSE_FINISH,
    
}http_congfig_state_en;

extern  http_congfig_state_en  http_congfig_state;



void  http_post_fsm(void);
void  http_congfig_fsm(void);
void  http_post(char* urlbuf,char* bodystr);



























#endif



