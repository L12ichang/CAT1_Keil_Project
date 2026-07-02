#ifndef   HTTP_ACTIVE_H
#define   HTTP_ACTIVE_H


typedef enum
{
    HTTP_CONFIG_IDLE,
    HTTP_CONFIG_AT_START0,
     HTTP_CONFIG_AT_START,
    HTTP_CONFIG_AT_QHTTPCFG1,
    HTTP_CONFIG_AT_QHTTPCFG2,
    HTTP_CONFIG_AT_QHTTPCFG_REQUESTHEADER,
    HTTP_CONFIG_AT_QHTTPURL_POST,//
    HTTP_CONFIG_AT_WAIT_CLOSE_HTTP,
    HTTP_CONFIG_AT_CLOSE_FINISH,
    
}http_congfig_state_en;

#ifndef HTTP_ACTIVE_LEGACY_IMPLEMENTATION
static inline void http_post_fsm(void)
{
}

static inline void http_congfig_fsm(void)
{
}

static inline void http_post(char* urlbuf, char* bodystr)
{
    (void)urlbuf;
    (void)bodystr;
}
#endif








#endif



