/*************************************************************
ç¨åºåè½ï¼CAT.1æºæ
§çµæºOTAåºä»¶æ´æ°
å¼åç¯å¢ï¼keil 5.37
è¯çåå·ï¼STM32F103CBT6/HK32F103CCT6A
å¼åäººåï¼é»é¿å½?
åä½åç§°ï¼å¹¿ä¸ä¸è±çµæºç§ææéå
¬å¸
ç¼è¾æ¥æï¼?026.5.4
*************************************************************/
#include "ota.h"
#include "NbDriver.h"
#include "Portable.h"
#include "hw_gateway.h"
#include "common.h"
#include "sys_data.h"
#include "for_iap.h"
#include "ota_config.h"
#include "hw_flash.h"
#include "Queue.h"
#include "mqtt_zk_protocol.h"
#include "watchdog.h"

boolean_en get_checksum_status(void);
extern void soft_reset(void); //å­å¨å¤±è´¥ç³»ç»å¤ä½
static   uint16 recvLength = 0;//æ°æ®æ¥æ¶é¿åº¦
#if OTA_USE_QHTTPREADFILE_UFS && !OTA_DEBUG_DOWNLOAD_ONLY
static   u8 *sbuff;
#endif
#define  PICK_SIZE                 512
#define  SERVER_PICK_SIZE          20480
#if OTA_USE_QHTTPREADFILE_UFS && !OTA_DEBUG_DOWNLOAD_ONLY
static u32 tihs_time_SERVER_PICK_SIZE=0;
static u32 last_total_size=0;
static u32 firmware_total_size=0;
static u32 save_byete_counter=0;
#endif
static u32 server_big_pick_counter=0;//
extern  void SET_NB_STAT_EPOWER_DOWN(void);
extern   void upload_ota_progress_bar(u32 id ,u32 progress_bar   );
extern void set_gateway_state_idle(void) ;
static u8 POWERED_DOWN_read_count=0;
static u32 http_get_timer=0;
#if OTA_USE_QHTTPREADFILE_UFS && !OTA_DEBUG_DOWNLOAD_ONLY
static u32 wait_data_timer=0;
#endif
static CONNECT_OTA_state_en ota_connect_state=CONNECT_OTA_STATE_IDLE;
 MCU_OTA_state_en  MCU_OTA_state=MCU_OTA_STATE_IDLE;
#if OTA_USE_QHTTPREADFILE_UFS && !OTA_DEBUG_DOWNLOAD_ONLY
static u8 OTA_DATA_IS_READY=0;
static u8 OTA_DATA_IS_finish=0;
static u32  pfile=0;//åºä»¶å­èæéä½ç½®
#endif
extern QUEUE  usartRecvQueue;//ä¸²å£æ°æ®æ¥æ¶éå
extern volatile uint32 usart_queue_drop_count;
static u8 last_server_big_pick=0;
#if OTA_USE_QHTTPREADFILE_UFS && !OTA_DEBUG_DOWNLOAD_ONLY
static u16 SERVER_CHECSUM=0;
static u16 checsum_temp=0;
#endif
#if !OTA_RAW_TCP_STREAM_DEBUG
static int ota_http_err_code=-1;
static int ota_http_status_code=0;
#endif
static u32 ota_http_content_length=0;
static u8 ota_http_content_length_present=0;
static int ota_last_readfile_err_code=0;
#if OTA_USE_QHTTPREADFILE_UFS
static u8 ota_readfile_command_ok_logged=0;
static u8 ota_diag_qflst_found=0;
static u32 ota_diag_qflst_size=0;
static u8 ota_diag_is_success_path=0;
static u8 ota_qflds_found=0;
static u32 ota_qflds_free_size=0;
static u32 ota_qflds_total_size=0;
static u8 ota_qflds_command_sent=0;
#endif
static u8 ota_stream_page_buf[FLASH_PAGE_SIZE];
static u8 ota_stream_program_buf[FLASH_PAGE_SIZE];
static u32 ota_stream_expected_size=0;
static u8 ota_stream_expected_known=0;
static u32 ota_stream_received=0;
static u32 ota_stream_flushed=0;
static u16 ota_stream_page_pos=0;
static u32 ota_stream_program_addr=0;
static u16 ota_stream_program_len=0;
static u8 ota_stream_program_pending=0;
static u8 ota_stream_backup_erased=0;
static u8 ota_stream_finished=0;
static u8 ota_stream_flash_error=0;
static u8 ota_stream_header_checked=0;
static u8 ota_stream_header_valid=0;
static u32 ota_stream_header_size=0;
static u32 ota_stream_header_checksum=0;
static u16 ota_stream_header_device_type=0;
static u32 ota_stream_last_progress_log=0;
#if !OTA_RAW_TCP_STREAM_DEBUG
static u32 ota_stream_last_rx_tick=0;
#endif
#if !OTA_RAW_TCP_STREAM_DEBUG
static u32 ota_stream_last_wait_log=0;
#endif
static u32 ota_stream_start_drop_count=0;
typedef enum
{
    OTA_RAW_HTTP_HEADER,
    OTA_RAW_HTTP_BODY,
    OTA_RAW_HTTP_CHUNK_SIZE,
    OTA_RAW_HTTP_CHUNK_DATA,
    OTA_RAW_HTTP_CHUNK_CR,
    OTA_RAW_HTTP_CHUNK_LF,
    OTA_RAW_HTTP_CHUNK_TRAILER,
    OTA_RAW_HTTP_DONE,
    OTA_RAW_HTTP_ERROR
} ota_raw_http_state_en;
typedef enum
{
    OTA_RAW_PROMPT_WAITING,
    OTA_RAW_PROMPT_OK,
    OTA_RAW_PROMPT_ERROR,
    OTA_RAW_PROMPT_TIMEOUT
} ota_raw_prompt_result_en;
static char ota_raw_host[80];
static char ota_raw_path[128];
static u16 ota_raw_port=80;
static char ota_raw_http_line[OTA_RAW_HTTP_LINE_MAX];
static u16 ota_raw_http_line_pos=0;
static ota_raw_http_state_en ota_raw_http_state=OTA_RAW_HTTP_HEADER;
static u8 ota_raw_http_chunked=0;
static u8 ota_raw_http_content_length_present=0;
static u32 ota_raw_http_content_length=0;
static u32 ota_raw_http_body_received=0;
static u32 ota_raw_http_chunk_remaining=0;
static u32 ota_raw_http_chunk_size=0;
static u8 ota_raw_http_chunk_ext=0;
static u8 ota_raw_http_header_last4[4];
static u8 ota_raw_http_header_last_pos=0;
static int ota_raw_http_status_code=0;
static u8 ota_raw_http_status_present=0;
static u8 ota_raw_body_done=0;
static u8 ota_raw_socket_closed=0;
static u8 ota_raw_close_success=0;
static u32 ota_raw_total_timer=0;
static u32 ota_raw_idle_timer=0;
static char ota_raw_qisend_line[80];
static u16 ota_raw_qisend_line_pos=0;
static u8 ota_raw_prompt_active=0;
static u32 ota_raw_prompt_start_tick=0;
static u32 ota_raw_prompt_rx_bytes=0;
static u8 ota_raw_prompt_tail[OTA_RAW_QISEND_PROMPT_TAIL_LEN];
static u8 ota_raw_prompt_tail_count=0;
static u8 ota_raw_prompt_tail_pos=0;
static u16 ota_raw_qird_remaining=0;
static u8 ota_raw_qird_zero_count=0;
static boolean_en ota_stream_set_expected_size(u32 size, const char *source);
static boolean_en ota_stream_write_byte(u8 dat);
uint16 pack_length=0;
#if OTA_USE_QHTTPREADFILE_UFS && !OTA_DEBUG_DOWNLOAD_ONLY
static  uint8 *pack_buf;
static u8  have_get_pack_length=0;
#endif
static u32 wait_openota_timer=0;
u32 congfig_delay_timer=0;
#if OTA_USE_QHTTPREADFILE_UFS && !OTA_DEBUG_DOWNLOAD_ONLY
typedef enum
{//+QFDWL:
DATA_STATE_IDLE,
DATA_STATE_2B,   //+
DATA_STATE_51,   //Q
DATA_STATE_46,   //F
DATA_STATE_44,   //D
DATA_STATE_57,   //W
DATA_STATE_4C,   //L
DATA_STATE_3A,   //:
DATA_STATE_20,   //ç©ºæ ¼
DATA_STATE_GET_SUM,
DATA_STATE_FINEISH
} DATA_STATE_en  ;
static DATA_STATE_en  data_state=DATA_STATE_IDLE;
#endif

char firm_name_buffer[256];
static   char common_send_buff[256];
static char ota_raw_qisend_cmd[32];
static u8 ota_raw_qisend_payload[257];
static char ota_default_url[128];
static char ota_log_line_buffer[OTA_RAW_LOG_LINE_MAX];

static void ota_use_local_firmware_name(void);

static const char *ota_get_download_url(void)
{
    const char *url;

    url = zk_get_ota_url();
    if (url != NULL && url[0] != '\0')
    {
        return url;
    }

    if (firm_name_buffer[0] == '\0')
    {
        ota_use_local_firmware_name();
    }
    snprintf(ota_default_url, sizeof(ota_default_url), "http://47.120.15.220:888/downloads/%s", firm_name_buffer);
    return ota_default_url;
}

#if !OTA_RAW_TCP_STREAM_DEBUG
static boolean_en ota_build_ota_url_string(char *buf, u16 buf_size, u16 *url_len)
{
    int len;

    if (buf == NULL || url_len == NULL || buf_size == 0)
    {
        return BOOL_FALSE;
    }

    len = snprintf(buf, buf_size, "%s\r\n", ota_get_download_url());
    if (len <= 2 || len >= buf_size || len > 255)
    {
        buf[0] = '\0';
        *url_len = 0;
        return BOOL_FALSE;
    }

    *url_len = (u16)(len - 2);
    return BOOL_TRUE;
}
#endif

#if OTA_HTTP10_CLOSE_WORKAROUND
static boolean_en ota_build_http10_close_header(char *buf, u16 buf_size, u16 *header_len)
{
    const char *url;
    const char *host_start;
    const char *path_start;
    const char *path;
    int host_len;
    int len;

    if (buf == NULL || header_len == NULL || buf_size == 0)
    {
        return BOOL_FALSE;
    }

    url = ota_get_download_url();
    if (strncmp(url, "http://", 7) != 0)
    {
        buf[0] = '\0';
        *header_len = 0;
        return BOOL_FALSE;
    }

    host_start = url + 7;
    path_start = strchr(host_start, '/');
    if (path_start == NULL)
    {
        host_len = (int)strlen(host_start);
        path = "/";
    }
    else
    {
        host_len = (int)(path_start - host_start);
        path = path_start;
    }

    if (host_len <= 0)
    {
        buf[0] = '\0';
        *header_len = 0;
        return BOOL_FALSE;
    }

    len = snprintf(buf,
                   buf_size,
                   "GET %s HTTP/1.0\r\n"
                   "Host: %.*s\r\n"
                   "Accept: */*\r\n"
                   "Accept-Encoding: identity\r\n"
                   "Connection: close\r\n"
                   "\r\n",
                   path,
                   host_len,
                   host_start);
    if (len <= 0 || len >= buf_size || len > 255)
    {
        buf[0] = '\0';
        *header_len = 0;
        return BOOL_FALSE;
    }

    *header_len = (u16)len;
    return BOOL_TRUE;
}
#endif

static char ota_ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

static boolean_en ota_stristarts(const char *text, const char *prefix)
{
    if (text == NULL || prefix == NULL)
    {
        return BOOL_FALSE;
    }
    while (*prefix != '\0')
    {
        if (ota_ascii_lower(*text) != ota_ascii_lower(*prefix))
        {
            return BOOL_FALSE;
        }
        ++text;
        ++prefix;
    }
    return BOOL_TRUE;
}

static boolean_en ota_parse_http_url_for_raw_tcp(void)
{
    const char *url;
    const char *host_start;
    const char *p;
    u16 host_len;
    u16 path_len;
    u32 port;

    url = ota_get_download_url();
    if (strncmp(url, "http://", 7) != 0)
    {
        OTA_LOGE("raw tcp only supports http url\r\n");
        return BOOL_FALSE;
    }

    host_start = url + 7;
    p = host_start;
    while (*p != '\0' && *p != ':' && *p != '/')
    {
        ++p;
    }
    host_len = (u16)(p - host_start);
    if (host_len == 0U || host_len >= (u16)sizeof(ota_raw_host))
    {
        OTA_LOGE("raw tcp host invalid len=%u\r\n", (unsigned int)host_len);
        return BOOL_FALSE;
    }
    memcpy(ota_raw_host, host_start, host_len);
    ota_raw_host[host_len] = '\0';

    port = 80U;
    if (*p == ':')
    {
        ++p;
        port = 0U;
        while (*p >= '0' && *p <= '9')
        {
            port = (port * 10U) + (u32)(*p - '0');
            ++p;
        }
        if (port == 0U || port > 65535U)
        {
            OTA_LOGE("raw tcp port invalid=%u\r\n", (unsigned int)port);
            return BOOL_FALSE;
        }
    }
    ota_raw_port = (u16)port;

    if (*p == '\0')
    {
        strcpy(ota_raw_path, "/");
    }
    else
    {
        path_len = (u16)strlen(p);
        if (path_len == 0U || path_len >= (u16)sizeof(ota_raw_path))
        {
            OTA_LOGE("raw tcp path invalid len=%u\r\n", (unsigned int)path_len);
            return BOOL_FALSE;
        }
        memcpy(ota_raw_path, p, path_len + 1U);
    }

    OTA_LOGI("raw tcp url ok\r\n");
    return BOOL_TRUE;
}

static void ota_raw_http_reset(void)
{
    ota_raw_http_line_pos = 0;
    ota_raw_http_state = OTA_RAW_HTTP_HEADER;
    ota_raw_http_chunked = 0;
    ota_raw_http_content_length_present = 0;
    ota_raw_http_content_length = 0;
    ota_raw_http_body_received = 0;
    ota_raw_http_chunk_remaining = 0;
    ota_raw_http_chunk_size = 0;
    ota_raw_http_chunk_ext = 0;
    ota_raw_body_done = 0;
    ota_raw_socket_closed = 0;
    ota_raw_close_success = 0;
    ota_raw_qisend_line_pos = 0;
    ota_raw_prompt_active = 0;
    ota_raw_prompt_start_tick = 0;
    ota_raw_prompt_rx_bytes = 0;
    ota_raw_prompt_tail_count = 0;
    ota_raw_prompt_tail_pos = 0;
    ota_raw_qird_remaining = 0;
    ota_raw_qird_zero_count = 0;
    ota_raw_http_status_code = 0;
    ota_raw_http_status_present = 0;
    ota_raw_http_header_last_pos = 0;
    memset(ota_raw_qisend_line, 0, sizeof(ota_raw_qisend_line));
    memset(ota_raw_prompt_tail, 0, sizeof(ota_raw_prompt_tail));
    memset(ota_raw_http_header_last4, 0, sizeof(ota_raw_http_header_last4));
    memset(ota_raw_http_line, 0, sizeof(ota_raw_http_line));
}

static void ota_raw_http_parse_header_line(const char *line)
{
    const char *p;
    u32 value;
    u8 digits;
    int status;

    if (line == NULL)
    {
        return;
    }
    if (ota_stristarts(line, "HTTP/") == BOOL_TRUE)
    {
        p = strchr(line, ' ');
        if (p == NULL)
        {
            OTA_LOGE("raw http status line invalid=%s\r\n", line);
            ota_stream_flash_error = 1U;
            ota_raw_http_state = OTA_RAW_HTTP_ERROR;
            return;
        }
        while (*p == ' ')
        {
            ++p;
        }
        value = 0U;
        digits = 0U;
        while (*p >= '0' && *p <= '9')
        {
            value = (value * 10U) + (u32)(*p - '0');
            ++p;
            ++digits;
        }
        if (digits != 3U)
        {
            OTA_LOGE("raw http status code invalid line=%s\r\n", line);
            ota_stream_flash_error = 1U;
            ota_raw_http_state = OTA_RAW_HTTP_ERROR;
            return;
        }
        status = (int)value;
        ota_raw_http_status_code = status;
        ota_raw_http_status_present = 1U;
        if (status != 200)
        {
            OTA_LOGE("raw http status bad status=%d\r\n", status);
            ota_stream_flash_error = 1U;
            ota_raw_http_state = OTA_RAW_HTTP_ERROR;
        }
    }
    else if (ota_stristarts(line, "Content-Length:") == BOOL_TRUE)
    {
        p = line + strlen("Content-Length:");
        while (*p == ' ')
        {
            ++p;
        }
        value = 0U;
        digits = 0U;
        while (*p >= '0' && *p <= '9')
        {
            value = (value * 10U) + (u32)(*p - '0');
            ++p;
            ++digits;
        }
        if (digits > 0U)
        {
            ota_raw_http_content_length = value;
            ota_raw_http_content_length_present = 1U;
            OTA_LOGI("raw http cl=%u\r\n", (unsigned int)value);
        }
    }
    else if (ota_stristarts(line, "Transfer-Encoding:") == BOOL_TRUE)
    {
        if (strstr(line, "chunked") != NULL || strstr(line, "Chunked") != NULL)
        {
            ota_raw_http_chunked = 1U;
            OTA_LOGI("raw http chunked\r\n");
        }
    }
}

static void ota_raw_http_header_byte(u8 dat)
{
    ota_raw_http_header_last4[ota_raw_http_header_last_pos & 3U] = dat;
    ota_raw_http_header_last_pos++;

    if (dat == '\n')
    {
        if (ota_raw_http_line_pos < (u16)(sizeof(ota_raw_http_line) - 1U))
        {
            ota_raw_http_line[ota_raw_http_line_pos++] = '\0';
        }
        else
        {
            ota_raw_http_line[sizeof(ota_raw_http_line) - 1U] = '\0';
        }
        ota_raw_http_parse_header_line(ota_raw_http_line);
        ota_raw_http_line_pos = 0U;
        ota_raw_http_line[0] = '\0';
        if (ota_raw_http_state == OTA_RAW_HTTP_ERROR)
        {
            return;
        }
    }
    else if (dat != '\r')
    {
        if (ota_raw_http_line_pos < (u16)(sizeof(ota_raw_http_line) - 1U))
        {
            ota_raw_http_line[ota_raw_http_line_pos++] = (char)dat;
        }
    }

    if (ota_raw_http_header_last_pos >= 4U &&
        ota_raw_http_header_last4[(ota_raw_http_header_last_pos - 4U) & 3U] == '\r' &&
        ota_raw_http_header_last4[(ota_raw_http_header_last_pos - 3U) & 3U] == '\n' &&
        ota_raw_http_header_last4[(ota_raw_http_header_last_pos - 2U) & 3U] == '\r' &&
        ota_raw_http_header_last4[(ota_raw_http_header_last_pos - 1U) & 3U] == '\n')
    {
        if (ota_raw_http_status_present == 0U)
        {
            OTA_LOGE("raw http header done without status\r\n");
            ota_stream_flash_error = 1U;
            ota_raw_http_state = OTA_RAW_HTTP_ERROR;
            return;
        }
        OTA_LOGI("raw http header done status=%d chunked=%u clp=%u cl=%u\r\n",
                 ota_raw_http_status_code,
                 ota_raw_http_chunked,
                 ota_raw_http_content_length_present,
                 (unsigned int)ota_raw_http_content_length);
        if (ota_raw_http_content_length_present &&
            ota_stream_set_expected_size(ota_raw_http_content_length, "raw_http_content_length") != BOOL_TRUE)
        {
            ota_stream_flash_error = 1U;
            ota_raw_http_state = OTA_RAW_HTTP_ERROR;
            return;
        }
        ota_raw_http_state = ota_raw_http_chunked ? OTA_RAW_HTTP_CHUNK_SIZE : OTA_RAW_HTTP_BODY;
    }
}

static boolean_en ota_raw_http_body_byte(u8 dat)
{
    if (ota_stream_write_byte(dat) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    ota_raw_http_body_received++;
    if (ota_stream_finished)
    {
        ota_raw_body_done = 1U;
        ota_raw_http_state = OTA_RAW_HTTP_DONE;
    }
    else if (ota_raw_http_content_length_present &&
             ota_raw_http_body_received >= ota_raw_http_content_length)
    {
        ota_raw_body_done = 1U;
        ota_raw_http_state = OTA_RAW_HTTP_DONE;
    }
    return BOOL_TRUE;
}

static boolean_en ota_raw_http_chunk_data_byte(u8 dat)
{
    if (ota_raw_http_chunk_remaining == 0U)
    {
        ota_raw_http_state = OTA_RAW_HTTP_CHUNK_CR;
        return BOOL_TRUE;
    }
    if (ota_raw_http_body_byte(dat) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    ota_raw_http_chunk_remaining--;
    if (ota_raw_http_chunk_remaining == 0U && ota_raw_http_state != OTA_RAW_HTTP_DONE)
    {
        ota_raw_http_state = OTA_RAW_HTTP_CHUNK_CR;
    }
    return BOOL_TRUE;
}

static boolean_en ota_raw_http_consume_byte(u8 dat)
{
    switch (ota_raw_http_state)
    {
        case OTA_RAW_HTTP_HEADER:
            ota_raw_http_header_byte(dat);
            return BOOL_TRUE;

        case OTA_RAW_HTTP_BODY:
            return ota_raw_http_body_byte(dat);

        case OTA_RAW_HTTP_CHUNK_SIZE:
            if (dat == '\r')
            {
                return BOOL_TRUE;
            }
            if (dat == '\n')
            {
                if (ota_raw_http_chunk_size == 0U)
                {
                    ota_raw_body_done = 1U;
                    ota_raw_http_state = OTA_RAW_HTTP_DONE;
                }
                else
                {
                    ota_raw_http_chunk_remaining = ota_raw_http_chunk_size;
                    ota_raw_http_chunk_size = 0U;
                    ota_raw_http_chunk_ext = 0U;
                    ota_raw_http_state = OTA_RAW_HTTP_CHUNK_DATA;
                }
                return BOOL_TRUE;
            }
            if (dat == ';')
            {
                ota_raw_http_chunk_ext = 1U;
                return BOOL_TRUE;
            }
            if (ota_raw_http_chunk_ext)
            {
                return BOOL_TRUE;
            }
            if (dat >= '0' && dat <= '9')
            {
                ota_raw_http_chunk_size = (ota_raw_http_chunk_size * 16U) + (u32)(dat - '0');
                return BOOL_TRUE;
            }
            if (dat >= 'a' && dat <= 'f')
            {
                ota_raw_http_chunk_size = (ota_raw_http_chunk_size * 16U) + (u32)(dat - 'a' + 10);
                return BOOL_TRUE;
            }
            if (dat >= 'A' && dat <= 'F')
            {
                ota_raw_http_chunk_size = (ota_raw_http_chunk_size * 16U) + (u32)(dat - 'A' + 10);
                return BOOL_TRUE;
            }
            OTA_LOGE("raw http bad chunk size char=0x%02x\r\n", dat);
            ota_raw_http_state = OTA_RAW_HTTP_ERROR;
            return BOOL_FALSE;

        case OTA_RAW_HTTP_CHUNK_DATA:
            return ota_raw_http_chunk_data_byte(dat);

        case OTA_RAW_HTTP_CHUNK_CR:
            if (dat == '\r')
            {
                ota_raw_http_state = OTA_RAW_HTTP_CHUNK_LF;
            }
            return BOOL_TRUE;

        case OTA_RAW_HTTP_CHUNK_LF:
            ota_raw_http_chunk_size = 0U;
            ota_raw_http_chunk_ext = 0U;
            ota_raw_http_state = OTA_RAW_HTTP_CHUNK_SIZE;
            return BOOL_TRUE;

        case OTA_RAW_HTTP_DONE:
            return BOOL_TRUE;

        case OTA_RAW_HTTP_CHUNK_TRAILER:
        case OTA_RAW_HTTP_ERROR:
        default:
            return BOOL_FALSE;
    }
}

static boolean_en ota_parse_qird_len(const char *line, u16 *read_len)
{
    const char *p;
    u32 value;
    u8 digits;

    if (line == NULL || read_len == NULL)
    {
        return BOOL_FALSE;
    }
    p = strstr(line, "+QIRD:");
    if (p == NULL)
    {
        return BOOL_FALSE;
    }
    p += strlen("+QIRD:");
    while (*p == ' ')
    {
        ++p;
    }
    value = 0U;
    digits = 0U;
    while (*p >= '0' && *p <= '9')
    {
        value = (value * 10U) + (u32)(*p - '0');
        ++p;
        ++digits;
    }
    if (digits == 0U || value > 1500U)
    {
        return BOOL_FALSE;
    }
    *read_len = (u16)value;
    return BOOL_TRUE;
}

static void ota_clear_rx_buffer(void)
{
    recvLength = 0;
    memset(stringBuf, 0x00, RECV_BUF_LENGTH);
}

static void ota_feed_watchdog_if_enabled(void)
{
    watchdog_feed_dog();
}

static boolean_en ota_log_append_char(u16 *pos, char value)
{
    if (pos == NULL || ((*pos + 1U) >= (u16)sizeof(ota_log_line_buffer)))
    {
        return BOOL_FALSE;
    }

    ota_log_line_buffer[*pos] = value;
    (*pos)++;
    ota_log_line_buffer[*pos] = '\0';
    return BOOL_TRUE;
}

static void ota_log_append_truncated(u16 *pos)
{
    (void)ota_log_append_char(pos, ' ');
    (void)ota_log_append_char(pos, '.');
    (void)ota_log_append_char(pos, '.');
    (void)ota_log_append_char(pos, '.');
}

#if OTA_RAW_HEX_LOG_ENABLE
static void ota_log_hex(const char *tag, const uint8 *buf, u16 len)
{
    u16 i;
    u16 pos;
    int written;

    if (tag == NULL || buf == NULL)
    {
        return;
    }

    ota_feed_watchdog_if_enabled();
    written = snprintf(ota_log_line_buffer, sizeof(ota_log_line_buffer), "%s len=%u:", tag, (unsigned int)len);
    if (written < 0)
    {
        return;
    }
    if (written >= (int)sizeof(ota_log_line_buffer))
    {
        ota_log_line_buffer[sizeof(ota_log_line_buffer) - 1U] = '\0';
        OTA_LOGD("%s\r\n", ota_log_line_buffer);
        ota_feed_watchdog_if_enabled();
        return;
    }
    pos = (u16)written;

    for (i = 0; i < len; i++)
    {
        written = snprintf(&ota_log_line_buffer[pos], sizeof(ota_log_line_buffer) - pos, " %02X", buf[i]);
        if (written <= 0)
        {
            break;
        }
        if (written >= (int)(sizeof(ota_log_line_buffer) - pos))
        {
            ota_log_line_buffer[sizeof(ota_log_line_buffer) - 1U] = '\0';
            break;
        }
        pos = (u16)(pos + (u16)written);
        if ((pos + 4U) >= (u16)sizeof(ota_log_line_buffer) && (i + 1U) < len)
        {
            ota_log_append_truncated(&pos);
            break;
        }
    }
    OTA_LOGD("%s\r\n", ota_log_line_buffer);
    ota_feed_watchdog_if_enabled();
}
#endif

static void ota_log_ascii_safe(const char *tag, const uint8 *buf, u16 len)
{
    u16 i;
    u16 pos;
    int written;
    static const char hex_chars[] = "0123456789ABCDEF";

    if (tag == NULL || buf == NULL)
    {
        return;
    }

    ota_feed_watchdog_if_enabled();
    written = snprintf(ota_log_line_buffer, sizeof(ota_log_line_buffer), "%s len=%u: ", tag, (unsigned int)len);
    if (written < 0)
    {
        return;
    }
    if (written >= (int)sizeof(ota_log_line_buffer))
    {
        ota_log_line_buffer[sizeof(ota_log_line_buffer) - 1U] = '\0';
        OTA_LOGD("%s\r\n", ota_log_line_buffer);
        ota_feed_watchdog_if_enabled();
        return;
    }
    pos = (u16)written;

    for (i = 0; i < len; i++)
    {
        uint8 c = buf[i];

        if (c >= 0x20 && c <= 0x7E)
        {
            if (ota_log_append_char(&pos, (char)c) == BOOL_FALSE)
            {
                ota_log_append_truncated(&pos);
                break;
            }
        }
        else if (c == '\r')
        {
            if (ota_log_append_char(&pos, '\\') == BOOL_FALSE ||
                ota_log_append_char(&pos, 'r') == BOOL_FALSE)
            {
                ota_log_append_truncated(&pos);
                break;
            }
        }
        else if (c == '\n')
        {
            if (ota_log_append_char(&pos, '\\') == BOOL_FALSE ||
                ota_log_append_char(&pos, 'n') == BOOL_FALSE)
            {
                ota_log_append_truncated(&pos);
                break;
            }
        }
        else
        {
            if (ota_log_append_char(&pos, '\\') == BOOL_FALSE ||
                ota_log_append_char(&pos, 'x') == BOOL_FALSE ||
                ota_log_append_char(&pos, hex_chars[(c >> 4) & 0x0FU]) == BOOL_FALSE ||
                ota_log_append_char(&pos, hex_chars[c & 0x0FU]) == BOOL_FALSE)
            {
                ota_log_append_truncated(&pos);
                break;
            }
        }
        if ((pos + 4U) >= (u16)sizeof(ota_log_line_buffer) && (i + 1U) < len)
        {
            ota_log_append_truncated(&pos);
            break;
        }
    }
    OTA_LOGD("%s\r\n", ota_log_line_buffer);
    ota_feed_watchdog_if_enabled();
}

static void ota_log_raw_rx(const uint8 *buf, u16 len)
{
    ota_log_ascii_safe("[OTA][RAW][RX]", buf, len);
#if OTA_RAW_HEX_LOG_ENABLE
    ota_log_hex("[OTA][HEX][RX]", buf, len);
#endif
}

static void ota_log_raw_tx_bytes(const uint8 *buf, u16 len)
{
    if (buf == NULL || len == 0U)
    {
        return;
    }

    ota_log_ascii_safe("[OTA][RAW][TX]", buf, len);
}

static void ota_log_raw_tx(const char *buf)
{
    if (buf == NULL)
    {
        return;
    }

    ota_log_raw_tx_bytes((const uint8 *)buf, (u16)strlen(buf));
}

static void ota_raw_prompt_reset(void)
{
    ota_raw_prompt_active = 1U;
    ota_raw_prompt_start_tick = Timer_GetTickCount();
    ota_raw_prompt_rx_bytes = 0U;
    ota_raw_prompt_tail_count = 0U;
    ota_raw_prompt_tail_pos = 0U;
    ota_raw_qisend_line_pos = 0U;
    memset(ota_raw_prompt_tail, 0, sizeof(ota_raw_prompt_tail));
    memset(ota_raw_qisend_line, 0, sizeof(ota_raw_qisend_line));
}

static void ota_raw_prompt_record_tail(u8 dat)
{
    ota_raw_prompt_tail[ota_raw_prompt_tail_pos] = dat;
    ota_raw_prompt_tail_pos++;
    if (ota_raw_prompt_tail_pos >= (u8)sizeof(ota_raw_prompt_tail))
    {
        ota_raw_prompt_tail_pos = 0U;
    }
    if (ota_raw_prompt_tail_count < (u8)sizeof(ota_raw_prompt_tail))
    {
        ota_raw_prompt_tail_count++;
    }
}

static void ota_raw_log_prompt_timeout(void)
{
    u8 i;
    u8 idx;
    u8 start;
    u16 pos;
    int written;

    if (ota_raw_prompt_tail_count == 0U)
    {
        OTA_LOGE("raw tcp qisend prompt timeout rx_bytes=%u last=empty\r\n",
                 (unsigned int)ota_raw_prompt_rx_bytes);
        return;
    }

    start = (ota_raw_prompt_tail_count < (u8)sizeof(ota_raw_prompt_tail)) ? 0U : ota_raw_prompt_tail_pos;
    pos = 0U;
    ota_log_line_buffer[0] = '\0';
    for (i = 0U; i < ota_raw_prompt_tail_count; i++)
    {
        idx = (u8)((start + i) % (u8)sizeof(ota_raw_prompt_tail));
        written = snprintf(&ota_log_line_buffer[pos],
                           sizeof(ota_log_line_buffer) - pos,
                           "%s%02X",
                           (i == 0U) ? "" : " ",
                           (unsigned int)ota_raw_prompt_tail[idx]);
        if (written <= 0)
        {
            break;
        }
        if (written >= (int)(sizeof(ota_log_line_buffer) - pos))
        {
            ota_log_line_buffer[sizeof(ota_log_line_buffer) - 1U] = '\0';
            break;
        }
        pos = (u16)(pos + (u16)written);
    }

    OTA_LOGE("raw tcp qisend prompt timeout rx_bytes=%u last=%s\r\n",
             (unsigned int)ota_raw_prompt_rx_bytes,
             ota_log_line_buffer);
}

static ota_raw_prompt_result_en ota_raw_wait_prompt(u32 timeout_ms)
{
    u8 dat;

    if (ota_raw_prompt_active == 0U)
    {
        ota_raw_prompt_reset();
    }
    ota_feed_watchdog_if_enabled();

    while (dequeue(&usartRecvQueue, &dat))
    {
        ota_raw_prompt_rx_bytes++;
        ota_raw_prompt_record_tail(dat);
        if ((ota_raw_prompt_rx_bytes & 0x3FU) == 0U)
        {
            ota_feed_watchdog_if_enabled();
        }

        if (dat == '>')
        {
            if (ota_raw_qisend_line_pos > 0U)
            {
                ota_log_raw_rx((const uint8 *)ota_raw_qisend_line, ota_raw_qisend_line_pos);
                ota_raw_qisend_line_pos = 0U;
                ota_raw_qisend_line[0] = '\0';
            }
            ota_raw_prompt_active = 0U;
            OTA_LOGI("raw tcp qisend prompt ok rx_bytes=%u\r\n",
                     (unsigned int)ota_raw_prompt_rx_bytes);
            return OTA_RAW_PROMPT_OK;
        }

        if (ota_raw_qisend_line_pos < (u16)(sizeof(ota_raw_qisend_line) - 1U))
        {
            ota_raw_qisend_line[ota_raw_qisend_line_pos++] = (char)dat;
            ota_raw_qisend_line[ota_raw_qisend_line_pos] = '\0';
        }

        if (strstr(ota_raw_qisend_line, "+CME ERROR:") != NULL ||
            strstr(ota_raw_qisend_line, "ERROR") != NULL)
        {
            ota_log_raw_rx((const uint8 *)ota_raw_qisend_line, ota_raw_qisend_line_pos);
            OTA_LOGE("raw tcp qisend rejected line=%s\r\n", ota_raw_qisend_line);
            ota_raw_qisend_line_pos = 0U;
            ota_raw_qisend_line[0] = '\0';
            ota_raw_prompt_active = 0U;
            return OTA_RAW_PROMPT_ERROR;
        }

        if (dat == '\n' ||
            ota_raw_qisend_line_pos >= (u16)(sizeof(ota_raw_qisend_line) - 1U))
        {
            ota_log_raw_rx((const uint8 *)ota_raw_qisend_line, ota_raw_qisend_line_pos);
            ota_raw_qisend_line_pos = 0U;
            ota_raw_qisend_line[0] = '\0';
        }
    }

    if (Timer_PassedDelay(ota_raw_prompt_start_tick, timeout_ms))
    {
        ota_raw_log_prompt_timeout();
        ota_raw_prompt_active = 0U;
        return OTA_RAW_PROMPT_TIMEOUT;
    }

    return OTA_RAW_PROMPT_WAITING;
}

static void ota_raw_query_socket_state(const char *reason)
{
    u32 start_tick;
    u8 done;

    OTA_LOGW("raw tcp socket status query reason=%s\r\n",
             (reason != NULL) ? reason : "unknown");
    ota_log_raw_tx("AT+QISTATE=1,0\r\n");
    ota_clear_rx_buffer();
    nb_modem_send_command_ota("AT+QISTATE=1,0\r\n", strlen("AT+QISTATE=1,0\r\n"));
    start_tick = Timer_GetTickCount();
    done = 0U;
    while (!Timer_PassedDelay(start_tick, OTA_RAW_QISTATE_TIMEOUT_MS))
    {
        ota_feed_watchdog_if_enabled();
        if (readLine(stringBuf, &recvLength, 0))
        {
            ota_log_raw_rx(stringBuf, recvLength);
            if (strstr((const char *)stringBuf, "+QISTATE:") != NULL)
            {
                OTA_LOGW("raw tcp socket status result=%s", (char *)stringBuf);
            }
            if (strstr((const char *)stringBuf, "OK") != NULL ||
                strstr((const char *)stringBuf, "ERROR") != NULL)
            {
                done = 1U;
                ota_clear_rx_buffer();
                break;
            }
            ota_clear_rx_buffer();
        }
    }
    if (done == 0U)
    {
        OTA_LOGW("raw tcp socket status query timeout\r\n");
    }
}

static void ota_raw_start_qisend_query(void)
{
    uint8 uart_ret;
    const char *query_cmd = "AT+QISEND=?\r\n";

    OTA_LOGI("raw tcp qisend query start\r\n");
    ota_log_raw_tx(query_cmd);
    ota_clear_rx_buffer();
    uart_ret = nb_modem_send_command_ota((void *)query_cmd, strlen(query_cmd));
    OTA_LOGD("raw tcp qisend query uart ret=%u len=%u\r\n",
             (unsigned int)uart_ret,
             (unsigned int)strlen(query_cmd));
    http_get_timer=Timer_GetTickCount();
    if (uart_ret != 0U)
    {
        OTA_LOGW("raw tcp qisend query uart send failed ret=%u, continue\r\n",
                 (unsigned int)uart_ret);
        ota_connect_state=CONNECT_OTA_AT_RAW_QISEND;
        return;
    }
    ota_connect_state=CONNECT_OTA_AT_RAW_QISEND_QUERY;
}

static void ota_log_stringbuf_rx_if_present(void)
{
    u16 len;

    len = (u16)strlen((const char *)stringBuf);
    if (len > 0U)
    {
        ota_log_raw_rx(stringBuf, len);
    }
}

static void ota_use_local_firmware_name(void)
{
    memset(firm_name_buffer, 0, sizeof(firm_name_buffer));
    strncpy(firm_name_buffer, OTA_LOCAL_FIRMWARE_NAME, sizeof(firm_name_buffer) - 1);
}

static void ota_reset_for_bad_url(void)
{
    OTA_LOGE("download failed: url too long or invalid\r\n");
    ota_connect_state = CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH;
#if OTA_DISABLE_FAIL_RESET
    OTA_LOGW("fail reset disabled reason=bad_url\r\n");
    changea_to_MQTT_modle();
#else
    soft_reset();
#endif
}

#if !OTA_RAW_TCP_STREAM_DEBUG
static boolean_en ota_parse_qhttpget_result(const char *line)
{
    const char *p;
    int err;
    int status;
    u32 content_len;
    u8 digit_count;

    if (line == NULL)
    {
        return BOOL_FALSE;
    }

    p = strstr(line, "+QHTTPGET:");
    if (p == NULL)
    {
        return BOOL_FALSE;
    }
    p += strlen("+QHTTPGET:");
    while (*p == ' ')
    {
        ++p;
    }

    err = 0;
    digit_count = 0;
    while (*p >= '0' && *p <= '9')
    {
        err = (err * 10) + (*p - '0');
        ++p;
        ++digit_count;
    }
    if (digit_count == 0U || *p != ',')
    {
        return BOOL_FALSE;
    }
    ++p;

    status = 0;
    digit_count = 0;
    while (*p >= '0' && *p <= '9')
    {
        status = (status * 10) + (*p - '0');
        ++p;
        ++digit_count;
    }
    if (digit_count == 0U)
    {
        return BOOL_FALSE;
    }

    content_len = 0;
    ota_http_content_length_present = 0;
    if (*p == ',')
    {
        ++p;
        digit_count = 0;
        while (*p >= '0' && *p <= '9')
        {
            content_len = (content_len * 10U) + (u32)(*p - '0');
            ++p;
            ++digit_count;
        }
        if (digit_count > 0U)
        {
            ota_http_content_length_present = 1;
        }
    }

    ota_http_err_code = err;
    ota_http_status_code = status;
    ota_http_content_length = content_len;
    return BOOL_TRUE;
}

#if OTA_USE_QHTTPREADFILE_UFS
static boolean_en ota_parse_qhttpreadfile_result(const char *line, int *err_code)
{
    const char *p;
    int err;

    if (line == NULL || err_code == NULL)
    {
        return BOOL_FALSE;
    }

    p = strstr(line, "+QHTTPREADFILE:");
    if (p == NULL)
    {
        return BOOL_FALSE;
    }
    p += strlen("+QHTTPREADFILE:");
    while (*p == ' ')
    {
        ++p;
    }

    err = 0;
    if (*p < '0' || *p > '9')
    {
        return BOOL_FALSE;
    }
    while (*p >= '0' && *p <= '9')
    {
        err = (err * 10) + (*p - '0');
        ++p;
    }

    *err_code = err;
    return BOOL_TRUE;
}
#endif

static boolean_en ota_parse_qflst_target(const char *line, const char *target, u32 *file_size)
{
    const char *p;
    u32 size;

    if (line == NULL || target == NULL || file_size == NULL)
    {
        return BOOL_FALSE;
    }

    p = strstr(line, target);
    if (p == NULL)
    {
        return BOOL_FALSE;
    }
    p += strlen(target);

    if (*p < '0' || *p > '9')
    {
        return BOOL_FALSE;
    }

    size = 0;
    while (*p >= '0' && *p <= '9')
    {
        size = (size * 10U) + (u32)(*p - '0');
        ++p;
    }

    *file_size = size;
    return BOOL_TRUE;
}

static boolean_en ota_parse_qflst_firmware_line(const char *line, u32 *file_size)
{
    char target[96];
    int len;

    if (line == NULL || file_size == NULL)
    {
        return BOOL_FALSE;
    }

    len = snprintf(target,
                   sizeof(target),
                   "+QFLST: \"%s%s\",",
                   OTA_LOCAL_UFS_PATH_PREFIX,
                   firm_name_buffer);
    if (len > 0 && len < (int)sizeof(target))
    {
        if (ota_parse_qflst_target(line, target, file_size) == BOOL_TRUE)
        {
            return BOOL_TRUE;
        }
    }

    len = snprintf(target, sizeof(target), "+QFLST: \"%s\",", firm_name_buffer);
    if (len > 0 && len < (int)sizeof(target))
    {
        if (ota_parse_qflst_target(line, target, file_size) == BOOL_TRUE)
        {
            return BOOL_TRUE;
        }
    }

    return BOOL_FALSE;
}

static boolean_en ota_parse_qflds_line(const char *line, u32 *free_size, u32 *total_size)
{
    const char *p;
    u32 free_value;
    u32 total_value;
    u8 digit_count;

    if (line == NULL || free_size == NULL || total_size == NULL)
    {
        return BOOL_FALSE;
    }

    p = strstr(line, "+QFLDS:");
    if (p == NULL)
    {
        return BOOL_FALSE;
    }
    p += strlen("+QFLDS:");
    while (*p == ' ')
    {
        ++p;
    }

    free_value = 0;
    digit_count = 0;
    while (*p >= '0' && *p <= '9')
    {
        free_value = (free_value * 10U) + (u32)(*p - '0');
        ++p;
        ++digit_count;
    }
    if (digit_count == 0U || *p != ',')
    {
        return BOOL_FALSE;
    }
    ++p;

    total_value = 0;
    digit_count = 0;
    while (*p >= '0' && *p <= '9')
    {
        total_value = (total_value * 10U) + (u32)(*p - '0');
        ++p;
        ++digit_count;
    }
    if (digit_count == 0U)
    {
        return BOOL_FALSE;
    }

    *free_size = free_value;
    *total_size = total_value;
    return BOOL_TRUE;
}
#endif

static void ota_reset_after_diag(void)
{
#if OTA_DISABLE_FAIL_RESET
    OTA_LOGW("fail reset disabled: keep running and return mqtt\r\n");
    changea_to_MQTT_modle();
#else
    HAL_Delay(300);
    soft_reset();
#endif
}

#if OTA_USE_QHTTPREADFILE_UFS
static void ota_log_qhttpreadfile_error_detail(int err_code)
{
    if (err_code == OTA_QHTTPREADFILE_ERR_MEMORY_ALLOC)
    {
        OTA_LOGE("qhttpreadfile err=729 means memory allocation failed in module HTTP stack\r\n");
        OTA_LOGW("module HTTP memory error: prefer non-chunked GET response with Content-Length\r\n");
        if (ota_http_content_length_present == 0U)
        {
            OTA_LOGW("qhttpget had no content length; server response may be chunked or close-delimited\r\n");
        }
    }
}
#endif

static void ota_start_http_stop_cleanup(const char *reason)
{
    ota_clear_rx_buffer();
    OTA_LOGI("http cleanup start reason=%s\r\n", (reason != NULL) ? reason : "unknown");
    ota_log_raw_tx("AT+QHTTPSTOP\r\n");
    nb_modem_send_command_ota("AT+QHTTPSTOP\r\n", strlen("AT+QHTTPSTOP\r\n"));
    http_get_timer = Timer_GetTickCount();
    ota_connect_state = CONNECT_OTA_AT_QHTTPSTOP_CLEANUP;
}

#if OTA_USE_QHTTPREADFILE_UFS
static void ota_start_readfile_fs_diag(const char *reason)
{
    ota_diag_qflst_found = 0;
    ota_diag_qflst_size = 0;
    ota_diag_is_success_path = (reason != NULL && strcmp(reason, "readfile_success") == 0) ? 1U : 0U;
    ota_clear_rx_buffer();
    OTA_LOGI("start qflst diagnose reason=%s\r\n",
             (reason != NULL) ? reason : "unknown");
    OTA_LOGI("qflst diagnose start file=%s%s\r\n",
             OTA_LOCAL_UFS_PATH_PREFIX,
             firm_name_buffer);
    ota_log_raw_tx("AT+QFLST\r\n");
    OTA_LOGI("module fs diagnose start reason=%s file=%s%s\r\n",
             (reason != NULL) ? reason : "unknown",
             OTA_LOCAL_UFS_PATH_PREFIX,
             firm_name_buffer);
    nb_modem_send_command_ota("AT+QFLST\r\n", strlen("AT+QFLST\r\n"));
    http_get_timer = Timer_GetTickCount();
    ota_connect_state = CONNECT_OTA_AT_QHTTPREADFILE_QFLST_DIAG;
}
#endif

static u32 ota_stream_read_le32(const u8 *buf, u16 offset)
{
    return ((u32)buf[offset]) |
           ((u32)buf[offset + 1U] << 8) |
           ((u32)buf[offset + 2U] << 16) |
           ((u32)buf[offset + 3U] << 24);
}

static u16 ota_stream_read_le16(const u8 *buf, u16 offset)
{
    return (u16)(((u16)buf[offset]) | ((u16)buf[offset + 1U] << 8));
}

static boolean_en ota_stream_size_in_range(u32 size)
{
    if (size < OTA_STREAM_HEADER_MIN_LEN)
    {
        return BOOL_FALSE;
    }
    if (size > OTA_STREAM_BACKUP_CAPACITY || size > OTA_STREAM_APP_MAX_SIZE)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

static boolean_en ota_stream_set_expected_size(u32 size, const char *source)
{
    if (ota_stream_size_in_range(size) == BOOL_FALSE)
    {
        OTA_LOGE("stream size bad src=%s size=%u cap=%u max=%u\r\n",
                 (source != NULL) ? source : "unknown",
                 (unsigned int)size,
                 (unsigned int)OTA_STREAM_BACKUP_CAPACITY,
                 (unsigned int)OTA_STREAM_APP_MAX_SIZE);
        return BOOL_FALSE;
    }

    ota_stream_expected_size = size;
    ota_stream_expected_known = 1U;
    OTA_LOGI("stream size=%u src=%s\r\n",
             (unsigned int)ota_stream_expected_size,
             (source != NULL) ? source : "unknown");
    return BOOL_TRUE;
}

static boolean_en ota_stream_erase_backup_area(void)
{
    FLASH_EraseInitTypeDef erase_init;
    HAL_StatusTypeDef status;
    u32 page_error;
    u32 addr;
    u32 end_next;
    u32 page_count;

    end_next = OTABAKROM_ENDADDR + 1U;
    if ((OTABAKROM_STARTADDR % FLASH_PAGE_SIZE) != 0U ||
        (end_next % FLASH_PAGE_SIZE) != 0U ||
        end_next <= OTABAKROM_STARTADDR)
    {
        OTA_LOGE("download failed: backup erase range bad start=0x%08x end=0x%08x page=%u\r\n",
                 (unsigned int)OTABAKROM_STARTADDR,
                 (unsigned int)OTABAKROM_ENDADDR,
                 (unsigned int)FLASH_PAGE_SIZE);
        ota_stream_backup_erased = 0U;
        return BOOL_FALSE;
    }

    ota_stream_backup_erased = 0U;
    ota_stream_program_pending = 0U;
    ota_stream_program_len = 0U;
    ota_stream_program_addr = 0U;
    page_count = OTA_STREAM_BACKUP_CAPACITY / FLASH_PAGE_SIZE;
    OTA_LOGI("stream backup erase start addr=0x%08x size=%u pages=%u\r\n",
             (unsigned int)OTABAKROM_STARTADDR,
             (unsigned int)OTA_STREAM_BACKUP_CAPACITY,
             (unsigned int)page_count);

    HAL_FLASH_Unlock();
    for (addr = OTABAKROM_STARTADDR; addr < end_next; addr += FLASH_PAGE_SIZE)
    {
        page_error = 0xFFFFFFFFU;
        erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
        erase_init.PageAddress = addr;
        erase_init.NbPages = 1U;
        status = HAL_FLASHEx_Erase(&erase_init, &page_error);
        if (status != HAL_OK)
        {
            HAL_FLASH_Lock();
            OTA_LOGE("download failed: backup erase fail addr=0x%08x page_error=0x%08x status=%d\r\n",
                     (unsigned int)addr,
                     (unsigned int)page_error,
                     status);
            return BOOL_FALSE;
        }
        ota_feed_watchdog_if_enabled();
    }
    HAL_FLASH_Lock();

    ota_stream_backup_erased = 1U;
    OTA_LOGI("stream backup erase done pages=%u\r\n", (unsigned int)page_count);
    return BOOL_TRUE;
}

static boolean_en ota_stream_programming_blocked(void)
{
    return (ota_connect_state == CONNECT_OTA_AT_RAW_QIRD_DATA) ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en ota_stream_verify_programmed_bytes(u32 addr, const u8 *buf, u16 len)
{
    u16 i;

    for (i = 0U; i < len; i++)
    {
        if (*((u8 *)(addr + i)) != buf[i])
        {
            OTA_LOGE("download failed: stream program verify mismatch addr=0x%08x off=%u exp=0x%02x got=0x%02x\r\n",
                     (unsigned int)addr,
                     (unsigned int)i,
                     (unsigned int)buf[i],
                     (unsigned int)(*((u8 *)(addr + i))));
            return BOOL_FALSE;
        }
    }
    return BOOL_TRUE;
}

static boolean_en ota_stream_program_bytes(u32 addr, const u8 *buf, u16 len)
{
    HAL_StatusTypeDef status;
    u16 pos;
    u16 halfword;

    if (len == 0U)
    {
        return BOOL_TRUE;
    }
    if (ota_stream_backup_erased == 0U)
    {
        OTA_LOGE("download failed: stream program before backup erase addr=0x%08x len=%u\r\n",
                 (unsigned int)addr,
                 (unsigned int)len);
        return BOOL_FALSE;
    }
    if ((addr & 1U) != 0U ||
        addr < OTABAKROM_STARTADDR ||
        (addr + (u32)len - 1U) > OTABAKROM_ENDADDR)
    {
        OTA_LOGE("download failed: stream program range bad addr=0x%08x len=%u\r\n",
                 (unsigned int)addr,
                 (unsigned int)len);
        return BOOL_FALSE;
    }

    HAL_FLASH_Unlock();
    for (pos = 0U; pos < len; pos = (u16)(pos + 2U))
    {
        halfword = (u16)buf[pos];
        if ((pos + 1U) < len)
        {
            halfword |= (u16)((u16)buf[pos + 1U] << 8);
        }
        else
        {
            halfword |= 0xFF00U;
        }
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + pos, (u64)halfword);
        if (status != HAL_OK)
        {
            HAL_FLASH_Lock();
            OTA_LOGE("download failed: stream program fail addr=0x%08x off=%u status=%d\r\n",
                     (unsigned int)addr,
                     (unsigned int)pos,
                     status);
            return BOOL_FALSE;
        }
        if ((pos & 0x00FFU) == 0U)
        {
            ota_feed_watchdog_if_enabled();
        }
    }
    HAL_FLASH_Lock();

    return ota_stream_verify_programmed_bytes(addr, buf, len);
}

static boolean_en ota_stream_program_pending_page(void)
{
    if (ota_stream_program_pending == 0U)
    {
        return BOOL_TRUE;
    }
    if (ota_stream_programming_blocked() == BOOL_TRUE)
    {
        OTA_LOGE("download failed: Flash program requested in QIRD_DATA addr=0x%08x len=%u\r\n",
                 (unsigned int)ota_stream_program_addr,
                 (unsigned int)ota_stream_program_len);
        return BOOL_FALSE;
    }
    if (ota_stream_program_bytes(ota_stream_program_addr,
                                 ota_stream_program_buf,
                                 ota_stream_program_len) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }

    ota_stream_flushed += ota_stream_program_len;
    OTA_LOGI("stream program addr=0x%08x len=%u programmed=%u\r\n",
             (unsigned int)ota_stream_program_addr,
             (unsigned int)ota_stream_program_len,
             (unsigned int)ota_stream_flushed);
    ota_stream_program_pending = 0U;
    ota_stream_program_len = 0U;
    ota_stream_program_addr = 0U;
    ota_feed_watchdog_if_enabled();
    return BOOL_TRUE;
}

static boolean_en ota_stream_stage_current_page(void)
{
    u32 addr;

    if (ota_stream_page_pos == 0U)
    {
        return BOOL_TRUE;
    }
    if (ota_stream_program_pending != 0U)
    {
        if (ota_stream_programming_blocked() == BOOL_TRUE)
        {
            OTA_LOGE("download failed: stream page buffer overrun while QIRD_DATA pending=%u page_pos=%u\r\n",
                     (unsigned int)ota_stream_program_len,
                     (unsigned int)ota_stream_page_pos);
            return BOOL_FALSE;
        }
        if (ota_stream_program_pending_page() != BOOL_TRUE)
        {
            return BOOL_FALSE;
        }
    }

    addr = OTABAKROM_STARTADDR + ota_stream_flushed;
    if (addr < OTABAKROM_STARTADDR ||
        (addr + ota_stream_page_pos - 1U) > OTABAKROM_ENDADDR)
    {
        OTA_LOGE("download failed: stream flash range overflow addr=0x%08x len=%u\r\n",
                 (unsigned int)addr,
                 (unsigned int)ota_stream_page_pos);
        return BOOL_FALSE;
    }

    memcpy(ota_stream_program_buf, ota_stream_page_buf, ota_stream_page_pos);
    ota_stream_program_addr = addr;
    ota_stream_program_len = ota_stream_page_pos;
    ota_stream_program_pending = 1U;
    ota_stream_page_pos = 0U;

    if (ota_stream_programming_blocked() == BOOL_TRUE)
    {
        OTA_LOGD("stream program deferred in QIRD_DATA addr=0x%08x len=%u\r\n",
                 (unsigned int)ota_stream_program_addr,
                 (unsigned int)ota_stream_program_len);
        return BOOL_TRUE;
    }

    return ota_stream_program_pending_page();
}

static boolean_en ota_stream_program_ready_pages(void)
{
    if (ota_stream_programming_blocked() == BOOL_TRUE)
    {
        OTA_LOGE("download failed: Flash program checkpoint hit in QIRD_DATA\r\n");
        return BOOL_FALSE;
    }
    if (ota_stream_program_pending != 0U)
    {
        if (ota_stream_program_pending_page() != BOOL_TRUE)
        {
            return BOOL_FALSE;
        }
    }
    if ((ota_stream_finished || ota_raw_body_done) && ota_stream_page_pos > 0U)
    {
        return ota_stream_stage_current_page();
    }
    return BOOL_TRUE;
}

static void ota_stream_reset(void)
{
    ota_stream_expected_size = 0;
    ota_stream_expected_known = 0;
    ota_stream_received = 0;
    ota_stream_flushed = 0;
    ota_stream_page_pos = 0;
    ota_stream_program_addr = 0;
    ota_stream_program_len = 0;
    ota_stream_program_pending = 0;
    ota_stream_finished = 0;
    ota_stream_flash_error = 0;
    ota_stream_header_checked = 0;
    ota_stream_header_valid = 0;
    ota_stream_header_size = 0;
    ota_stream_header_checksum = 0;
    ota_stream_header_device_type = 0;
    ota_stream_last_progress_log = 0;
#if !OTA_RAW_TCP_STREAM_DEBUG
    ota_stream_last_rx_tick = Timer_GetTickCount();
    ota_stream_last_wait_log = ota_stream_last_rx_tick;
#endif
    ota_stream_start_drop_count = usart_queue_drop_count;

    OTA_LOGI("stream backup=0x%08x cap=%u app_max=%u qdrop=%u\r\n",
             (unsigned int)OTABAKROM_STARTADDR,
             (unsigned int)OTA_STREAM_BACKUP_CAPACITY,
             (unsigned int)OTA_STREAM_APP_MAX_SIZE,
             (unsigned int)ota_stream_start_drop_count);
    if (ota_stream_backup_erased == 0U)
    {
        OTA_LOGE("download failed: backup area not erased before stream\r\n");
        ota_stream_flash_error = 1U;
    }
    if (ota_http_content_length_present)
    {
        if (ota_stream_set_expected_size(ota_http_content_length, "qhttpget_content_length") != BOOL_TRUE)
        {
            ota_stream_flash_error = 1U;
        }
    }
    else
    {
        OTA_LOGW("stream size unknown: parse header\r\n");
    }
}

static boolean_en ota_stream_try_parse_header(void)
{
    u32 raw_size;
    u32 size;

    if (ota_stream_header_checked || ota_stream_page_pos < OTA_STREAM_HEADER_MIN_LEN)
    {
        return BOOL_TRUE;
    }

    ota_stream_header_checked = 1U;
    ota_stream_header_checksum = ota_stream_read_le32(ota_stream_page_buf, ADDR_CHECKSUM_OFFSET);
    raw_size = ota_stream_read_le32(ota_stream_page_buf, ADDR_SIZE_OFFSET);
    ota_stream_header_size = raw_size & 0x00FFFFFFU;
    ota_stream_header_device_type = ota_stream_read_le16(ota_stream_page_buf, ADDR_TYPE_OFFSET);
    size = ota_stream_header_size;

    OTA_LOGI("stream image header checksum=0x%08x raw_size=0x%08x size=%u type=0x%04x\r\n",
             (unsigned int)ota_stream_header_checksum,
             (unsigned int)raw_size,
             (unsigned int)size,
             (unsigned int)ota_stream_header_device_type);

    if (ota_stream_header_checksum != (u32)0x12345678 &&
        raw_size != (u32)0x89ABCDEF &&
        ota_stream_size_in_range(size) == BOOL_TRUE &&
        (size % 4U) == 0U &&
        ota_stream_header_device_type == (u16)OTA_EXPECTED_DEVICE_TYPE)
    {
        ota_stream_header_valid = 1U;
        if (ota_stream_expected_known == 0U)
        {
            return ota_stream_set_expected_size(size, "firmware_header");
        }
        if (size != ota_stream_expected_size)
        {
            OTA_LOGE("download failed: header size mismatch header=%u expected=%u\r\n",
                     (unsigned int)size,
                     (unsigned int)ota_stream_expected_size);
            return BOOL_FALSE;
        }
        return BOOL_TRUE;
    }

    if (ota_stream_header_device_type != (u16)OTA_EXPECTED_DEVICE_TYPE)
    {
        OTA_LOGE("download failed: device type mismatch got=0x%04x exp=0x%04x\r\n",
                 (unsigned int)ota_stream_header_device_type,
                 (unsigned int)OTA_EXPECTED_DEVICE_TYPE);
    }
    else
    {
        OTA_LOGW("stream image header invalid or raw test file\r\n");
    }
    if (ota_stream_expected_known)
    {
#if OTA_STREAM_ALLOW_RAW_BIN_TEST
        OTA_LOGW("raw test allowed, boot off\r\n");
        return BOOL_TRUE;
#else
        return BOOL_FALSE;
#endif
    }

    OTA_LOGE("download failed: no content length and firmware header is invalid\r\n");
    return BOOL_FALSE;
}

static boolean_en ota_stream_flush_page(void)
{
    return ota_stream_stage_current_page();
}

static boolean_en ota_stream_write_byte(u8 dat)
{
    if (ota_stream_finished || ota_stream_flash_error)
    {
        return BOOL_FALSE;
    }
    if (ota_stream_expected_known && ota_stream_received >= ota_stream_expected_size)
    {
        ota_stream_finished = 1U;
        return BOOL_TRUE;
    }

    ota_stream_page_buf[ota_stream_page_pos++] = dat;
    ota_stream_received++;
#if !OTA_RAW_TCP_STREAM_DEBUG
    ota_stream_last_rx_tick = Timer_GetTickCount();
#endif

    if (ota_stream_try_parse_header() != BOOL_TRUE)
    {
        ota_stream_flash_error = 1U;
        return BOOL_FALSE;
    }

    if (ota_stream_expected_known && ota_stream_received == ota_stream_expected_size)
    {
        if (ota_stream_flush_page() != BOOL_TRUE)
        {
            ota_stream_flash_error = 1U;
            return BOOL_FALSE;
        }
        ota_stream_finished = 1U;
        OTA_LOGI("stream body complete rx=%u programmed=%u pending=%u qdrop=%u\r\n",
                 (unsigned int)ota_stream_received,
                 (unsigned int)ota_stream_flushed,
                 (unsigned int)(ota_stream_program_pending ? ota_stream_program_len : 0U),
                 (unsigned int)(usart_queue_drop_count - ota_stream_start_drop_count));
        return BOOL_TRUE;
    }

    if (ota_stream_page_pos >= FLASH_PAGE_SIZE)
    {
        if (ota_stream_flush_page() != BOOL_TRUE)
        {
            ota_stream_flash_error = 1U;
            return BOOL_FALSE;
        }
    }

    if (ota_stream_received - ota_stream_last_progress_log >= 8192U)
    {
        ota_stream_last_progress_log = ota_stream_received;
        OTA_LOGI("stream progress rx=%u known=%u exp=%u qdrop=%u\r\n",
                 (unsigned int)ota_stream_received,
                 ota_stream_expected_known,
                 (unsigned int)ota_stream_expected_size,
                 (unsigned int)(usart_queue_drop_count - ota_stream_start_drop_count));
    }
    return BOOL_TRUE;
}

#if !OTA_RAW_TCP_STREAM_DEBUG
static void ota_stream_process_queue(void)
{
    u8 dat;
    u16 budget;
    u32 now;

    budget = 0;
    while (ota_stream_finished == 0U && dequeue(&usartRecvQueue, &dat))
    {
        if (ota_stream_write_byte(dat) != BOOL_TRUE)
        {
            break;
        }
        budget++;
        if (budget >= 512U)
        {
            budget = 0;
            ota_feed_watchdog_if_enabled();
        }
    }

    if (ota_stream_finished == 0U &&
        ota_stream_flash_error == 0U &&
        Timer_PassedDelay(ota_stream_last_wait_log, OTA_STREAM_WAIT_LOG_MS))
    {
        now = Timer_GetTickCount();
        ota_stream_last_wait_log = now;
        OTA_LOGW("stream waiting rx=%u known=%u exp=%u miss=%u idle=%u qdrop=%u ore_delta=%u\r\n",
                 (unsigned int)ota_stream_received,
                 ota_stream_expected_known,
                 (unsigned int)ota_stream_expected_size,
                 (unsigned int)((ota_stream_expected_known && ota_stream_expected_size > ota_stream_received) ? (ota_stream_expected_size - ota_stream_received) : 0U),
                 (unsigned int)(now - ota_stream_last_rx_tick),
                 (unsigned int)(usart_queue_drop_count - ota_stream_start_drop_count),
                 (unsigned int)(usart_uart1_ore_count - ota_stream_start_ore_count));
    }
}
#endif

static boolean_en ota_stream_verify_backup(void)
{
    if (ota_stream_flash_error)
    {
        return BOOL_FALSE;
    }
    if (ota_stream_program_ready_pages() != BOOL_TRUE)
    {
        ota_stream_flash_error = 1U;
        return BOOL_FALSE;
    }
    if (ota_stream_expected_known == 0U)
    {
        OTA_LOGE("download failed: stream size was never determined\r\n");
        return BOOL_FALSE;
    }
    if (ota_stream_received != ota_stream_expected_size ||
        ota_stream_flushed != ota_stream_expected_size)
    {
        OTA_LOGE("download failed: stream byte count mismatch expected=%u received=%u flushed=%u\r\n",
                 (unsigned int)ota_stream_expected_size,
                 (unsigned int)ota_stream_received,
                 (unsigned int)ota_stream_flushed);
        return BOOL_FALSE;
    }

    if (ota_stream_header_valid)
    {
        if (get_checksum_status() == BOOL_TRUE)
        {
            OTA_LOGI("STREAM DOWNLOAD VERIFY SUCCESS: backup ready addr=0x%08x size=%u checksum=0x%08x\r\n",
                     (unsigned int)OTABAKROM_STARTADDR,
                     (unsigned int)ota_stream_expected_size,
                     (unsigned int)ota_stream_header_checksum);
            return BOOL_TRUE;
        }
        OTA_LOGE("download failed: backup checksum verify failed size=%u checksum=0x%08x\r\n",
                 (unsigned int)ota_stream_expected_size,
                 (unsigned int)ota_stream_header_checksum);
        return BOOL_FALSE;
    }

#if OTA_STREAM_ALLOW_RAW_BIN_TEST
    OTA_LOGW("STREAM RAW OK addr=0x%08x size=%u boot off\r\n",
             (unsigned int)OTABAKROM_STARTADDR,
             (unsigned int)ota_stream_expected_size);
    return BOOL_TRUE;
#else
    OTA_LOGE("download failed: image header invalid\r\n");
    return BOOL_FALSE;
#endif
}

#if !OTA_RAW_TCP_STREAM_DEBUG
static void ota_start_qhttpurl(void)
{
    u16 length;
    static char buff[64];

    if (ota_build_ota_url_string(common_send_buff, sizeof(common_send_buff), &length) == BOOL_FALSE)
    {
        ota_reset_for_bad_url();
        return;
    }
    snprintf(buff, sizeof(buff), "AT+QHTTPURL=%u,30\r\n", length);
    OTA_LOGI("qhttpurl start len=%u\r\n", length);
    OTA_LOGI("url ready len=%u\r\n", length);
    OTA_LOGD("url=%s\r\n", ota_get_download_url());
    ota_log_raw_tx(buff);
    send_AT_Command_machine_star(buff, strlen(buff), "CONNECT",20, 0);
    ota_connect_state=CONNECT_OTA_AT_QHTTPURL;
}
#endif

static void ota_raw_start_close(const char *reason, u8 success_path)
{
    OTA_LOGI("raw tcp close reason=%s success=%u\r\n",
             (reason != NULL) ? reason : "unknown",
             success_path);
    ota_raw_close_success = success_path;
    ota_log_raw_tx("AT+QICLOSE=0\r\n");
    ota_clear_rx_buffer();
    nb_modem_send_command_ota("AT+QICLOSE=0\r\n", strlen("AT+QICLOSE=0\r\n"));
    http_get_timer = Timer_GetTickCount();
    ota_connect_state = CONNECT_OTA_AT_RAW_CLOSE;
}


/************************************
åè½æè¿°ï¼å¯å¨ç¡¬ä»¶å¤ä½?
*************************************/
void  _4G_OTA_machine_star(void)
{
       OTA_LOGI("download task start local=%s\r\n", firm_name_buffer);
       OTA_LOGI("mode=%s qhttpreadfile=%u download_only=%u raw_bin_test=%u\r\n",
                OTA_USE_RAW_TCP_STREAM ? "RAW_TCP_STREAM" : "MODULE_HTTP",
                (unsigned int)OTA_USE_QHTTPREADFILE_UFS,
                (unsigned int)OTA_DEBUG_DOWNLOAD_ONLY,
                (unsigned int)OTA_STREAM_ALLOW_RAW_BIN_TEST);
       OTA_LOGI("code marker=ota_raw_tcp_v1\r\n");
       ota_connect_state=CONNECT_OTA_RESETING;
       server_big_pick_counter=0;
       POWERED_DOWN_read_count=0;
       http_get_timer=0;
#if !OTA_RAW_TCP_STREAM_DEBUG
       ota_http_err_code=-1;
       ota_http_status_code=0;
#endif
       ota_http_content_length=0;
       ota_http_content_length_present=0;
       ota_last_readfile_err_code=0;
       ota_raw_host[0]='\0';
       ota_raw_path[0]='\0';
       ota_raw_port=80U;
       ota_stream_backup_erased=0;
       ota_stream_program_pending=0;
       ota_stream_program_len=0;
       ota_stream_program_addr=0;
       ota_raw_http_reset();
#if OTA_USE_QHTTPREADFILE_UFS
       ota_readfile_command_ok_logged=0;
       ota_diag_qflst_found=0;
       ota_diag_qflst_size=0;
       ota_diag_is_success_path=0;
       ota_qflds_found=0;
       ota_qflds_free_size=0;
       ota_qflds_total_size=0;
       ota_qflds_command_sent=0;
#endif
#if OTA_DEBUG_DOWNLOAD_ONLY
       MCU_OTA_state=MCU_OTA_STATE_IDLE;
#endif
}
/************************************
åè½æè¿°ï¼å¯å¨æå¡å¨åºä»¶ä¸è½½é
ç½®
*************************************/
void  _4G_OTA_machine_contextid(void)
    {

  // send_AT_Command_machine_star("AT+QHTTPCFG=\"contextid\",1 \r\n", strlen("AT+QHTTPCFG=\"contextid\",1 \r\n"), "OK\r\n", 20, 0);
     congfig_delay_timer=Timer_GetTickCount();
     ota_connect_state=CONNECT_OTA_AT_QHTTPCFG_CINFIG;

    }
/************************************
åè½æè¿°ï¼æ¥è¯¢åºä»¶ä¸è½½å®å?
*************************************/
boolean_en  _4G_OTA_machine_finish(void)
    {
      if( ota_connect_state==CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH)
      {
          return BOOL_TRUE;
      }
      else{
          return BOOL_FALSE;
      }
    }
/************************************
åè½æè¿°ï¼åºä»¶ä¸è½½å¤ç?
*************************************/
void _4G_OTA_machine(void)
{
     ota_feed_watchdog_if_enabled();

     switch(ota_connect_state)
    {
      case CONNECT_OTA_STATE_IDLE:
            break;
       case CONNECT_OTA_RESETING:
            resetNbModule();//æ¨¡åå¤ä½
            ota_connect_state=CONNECT_OTA_READY;
            break;

       case CONNECT_OTA_READY:
             if( _4g_reset_finish()==BOOL_TRUE)
            {
               recvLength = 0;
                if (readLine(stringBuf, &recvLength, 0))
                {
                    if( strstr((const char *) stringBuf, "POWERED DOWN"))
                     {
                            ota_connect_state=CONNECT_OTA_RESETING;
                            printf("__________________æ¨¡åéä¸çµ___________________\n");
                           break;
                     }
                    else if( strstr((const char *) stringBuf, "RDY"))
                    {
                              congfig_delay_timer=Timer_GetTickCount();
                              ota_connect_state=CONNECT_OTA_AT_QHTTPCFG_CINFIG;
                    }
                    else
                    {     //æªè¯»å?
                         if(POWERED_DOWN_read_count<2)
                         { //éè¯»
                                ota_connect_state= CONNECT_OTA_READY;
                          break;
                         }
                         else
                         {//ç´æ¥èµ°ä¸å?
                             POWERED_DOWN_read_count=0;
                         }
                      }
                   }
              }

             break;
        case CONNECT_OTA_AT_QHTTPCFG_CINFIG:
             if(Timer_PassedDelay(congfig_delay_timer,400))  //OTA åè¿æ¥çæ¶åè¦ç­?00mS ä»¥ä¸
             {
                   send_AT_Command_machine_star("AT+QMTCLOSE=0\r\n",strlen("AT+QMTCLOSE=0\r\n"),"OK", 5, 1);//+QMTCLOSE: <client_idx>,<result>+QMTCLOSE: 0,0
                   ota_connect_state= CONNECT_OTA_AT_QMTT_CLOSE;
             }
             break;

         case CONNECT_OTA_AT_QMTT_CLOSE:
              if(send_AT_Command_machine_finish()==TRUE)
              {
                    // Close QMT and switch to OTA mode.
                    set_gateway_state_idle() ;//ç½®ä½éä¿¡éé»ç¶æ?
                    SET_NB_STAT_EPOWER_DOWN();//éç½®URCå¤çç¶æ?
                    OTA_ENABLE=1;
#if OTA_RAW_TCP_STREAM_DEBUG
                    OTA_LOGI("raw tcp mode: bypass module HTTP stack\r\n");
                    ota_log_raw_tx("AT+QICLOSE=0\r\n");
                    send_AT_Command_machine_star("AT+QICLOSE=0\r\n", strlen("AT+QICLOSE=0\r\n"), "OK", 20, 0);
                    ota_connect_state=CONNECT_OTA_AT_RAW_QICLOSE;
#else
                    OTA_LOGI("http cfg contextid=1\r\n");
                    ota_log_raw_tx("AT+QHTTPCFG=\"contextid\",1\r\n");
                    send_AT_Command_machine_star("AT+QHTTPCFG=\"contextid\",1\r\n", strlen("AT+QHTTPCFG=\"contextid\",1\r\n"), "OK\r\n", 20, 0);
                    ota_connect_state=CONNECT_OTA_AT_QHTTPCFG_contextid;
#endif
             }
             break;

         case CONNECT_OTA_AT_RAW_QICLOSE:
              if(send_AT_Command_machine_finish()==TRUE)
              {
                    ota_log_stringbuf_rx_if_present();
                     if (ota_parse_http_url_for_raw_tcp() != BOOL_TRUE)
                     {
                         ota_reset_for_bad_url();
                         break;
                     }
                     if (ota_stream_erase_backup_area() != BOOL_TRUE)
                     {
                         ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH;
                         ota_reset_after_diag();
                         break;
                     }
                     snprintf(common_send_buff,
                              sizeof(common_send_buff),
                              "AT+QIOPEN=%u,%u,\"TCP\",\"%s\",%u,0,0\r\n",
                             (unsigned int)OTA_RAW_TCP_CONTEXT_ID,
                             (unsigned int)OTA_RAW_TCP_CONNECT_ID,
                             ota_raw_host,
                             (unsigned int)ota_raw_port);
                    OTA_LOGI("raw tcp open start\r\n");
                    ota_log_raw_tx(common_send_buff);
                    ota_clear_rx_buffer();
                    nb_modem_send_command_ota(common_send_buff, strlen(common_send_buff));
                    http_get_timer=Timer_GetTickCount();
                    ota_connect_state=CONNECT_OTA_AT_RAW_QIOPEN;
              }
              break;

         case CONNECT_OTA_AT_RAW_QIOPEN:
              ota_feed_watchdog_if_enabled();
              if(!Timer_PassedDelay(http_get_timer, OTA_RAW_TCP_OPEN_TIMEOUT_MS))
              {
                    if (readLine(stringBuf, &recvLength, 0))
                    {
                        ota_log_raw_rx(stringBuf, recvLength);
                        if (strstr((const char *)stringBuf, "+QIOPEN: 0,0") != NULL)
                        {
                            OTA_LOGI("raw tcp open ok\r\n");
                            ota_clear_rx_buffer();
                            ota_raw_start_qisend_query();
                            break;
                        }
                        if (strstr((const char *)stringBuf, "+QIOPEN:") != NULL ||
                            strstr((const char *)stringBuf, "ERROR") != NULL)
                        {
                            OTA_LOGE("raw tcp open failed line=%s\r\n", (char *)stringBuf);
                            ota_raw_start_close("open_failed", 0U);
                            break;
                        }
                        ota_clear_rx_buffer();
                    }
              }
              else
              {
                    OTA_LOGE("raw tcp open timeout\r\n");
                    ota_raw_start_close("open_timeout", 0U);
              }
              break;

         case CONNECT_OTA_AT_RAW_QISEND_QUERY:
              ota_feed_watchdog_if_enabled();
              if(!Timer_PassedDelay(http_get_timer, OTA_RAW_QISEND_QUERY_TIMEOUT_MS))
              {
                    if (readLine(stringBuf, &recvLength, 0))
                    {
                        ota_log_raw_rx(stringBuf, recvLength);
                        OTA_LOGD("raw tcp qisend query rx=%s", (char *)stringBuf);
                        if (strstr((const char *)stringBuf, "OK") != NULL)
                        {
                            OTA_LOGI("raw tcp qisend query done\r\n");
                            ota_clear_rx_buffer();
                            ota_connect_state=CONNECT_OTA_AT_RAW_QISEND;
                            break;
                        }
                        if (strstr((const char *)stringBuf, "ERROR") != NULL)
                        {
                            OTA_LOGW("raw tcp qisend query error line=%s", (char *)stringBuf);
                            ota_clear_rx_buffer();
                            ota_connect_state=CONNECT_OTA_AT_RAW_QISEND;
                            break;
                        }
                        ota_clear_rx_buffer();
                    }
              }
              else
              {
                    OTA_LOGW("raw tcp qisend query timeout, continue\r\n");
                    ota_clear_rx_buffer();
                    ota_connect_state=CONNECT_OTA_AT_RAW_QISEND;
              }
              break;

         case CONNECT_OTA_AT_RAW_QISEND:
               {
                    u16 req_len;
                    uint8 uart_ret;
                    int len;
                    int cmd_len;

                    len = snprintf(common_send_buff,
                                   sizeof(common_send_buff),
                                   "GET %s HTTP/1.1\r\n"
                                   "Host: %s:%u\r\n"
                                   "Accept: */*\r\n"
                                   "Accept-Encoding: identity\r\n"
                                   "Connection: close\r\n"
                                   "\r\n",
                                   ota_raw_path,
                                   ota_raw_host,
                                   (unsigned int)ota_raw_port);
                    if (len <= 0 || len >= (int)sizeof(common_send_buff))
                    {
                        OTA_LOGE("raw tcp request too long\r\n");
                        ota_raw_start_close("request_too_long", 0U);
                        break;
                    }
                    req_len = (u16)len;
#if OTA_RAW_QISEND_WITH_LEN
                    cmd_len = snprintf(ota_raw_qisend_cmd,
                                       sizeof(ota_raw_qisend_cmd),
                                       "AT+QISEND=%u,%u\r\n",
                                       (unsigned int)OTA_RAW_TCP_CONNECT_ID,
                                       (unsigned int)req_len);
#else
                    cmd_len = snprintf(ota_raw_qisend_cmd,
                                       sizeof(ota_raw_qisend_cmd),
                                       "AT+QISEND=%u\r\n",
                                       (unsigned int)OTA_RAW_TCP_CONNECT_ID);
#endif
                    if (cmd_len <= 0 || cmd_len >= (int)sizeof(ota_raw_qisend_cmd))
                    {
                        OTA_LOGE("raw tcp qisend command too long\r\n");
                        ota_raw_start_close("qisend_cmd_too_long", 0U);
                        break;
                    }
                    OTA_LOGI("raw tcp qisend mode=%s len=%u\r\n",
#if OTA_RAW_QISEND_WITH_LEN
                             "with_len",
#else
                             "no_len_ctrl_z",
#endif
                             (unsigned int)req_len);
                    ota_log_raw_tx(ota_raw_qisend_cmd);
                    ota_clear_rx_buffer();
                    ota_raw_prompt_reset();
                    uart_ret = nb_modem_send_command_ota(ota_raw_qisend_cmd, strlen(ota_raw_qisend_cmd));
                    OTA_LOGD("raw tcp qisend uart ret=%u len=%u\r\n",
                             (unsigned int)uart_ret,
                             (unsigned int)strlen(ota_raw_qisend_cmd));
                    if (uart_ret != 0U)
                    {
                        OTA_LOGE("raw tcp qisend uart send failed ret=%u\r\n",
                                 (unsigned int)uart_ret);
                        ota_raw_start_close("qisend_uart_send_fail", 0U);
                        break;
                    }
                    http_get_timer=Timer_GetTickCount();
                    ota_connect_state=CONNECT_OTA_AT_RAW_QISEND_DATA;
              }
              break;

         case CONNECT_OTA_AT_RAW_QISEND_DATA:
              {
                    ota_raw_prompt_result_en prompt_result;

                    prompt_result = ota_raw_wait_prompt(OTA_RAW_QISEND_PROMPT_TIMEOUT_MS);
                    if (prompt_result == OTA_RAW_PROMPT_OK)
                    {
                        u16 http_req_len;
                        u16 send_len;
                        uint8 uart_ret;

                        http_req_len = (u16)strlen(common_send_buff);
                        OTA_LOGI("raw tcp http request payload sent len=%u\r\n",
                                 (unsigned int)http_req_len);
#if OTA_RAW_QISEND_WITH_LEN
                        send_len = http_req_len;
                        ota_log_raw_tx(common_send_buff);
                        uart_ret = nb_modem_send_command_ota(common_send_buff, send_len);
#else
                        send_len = (u16)(http_req_len + 1U);
                        if (send_len > (u16)sizeof(ota_raw_qisend_payload))
                        {
                            OTA_LOGE("raw tcp request payload too long len=%u\r\n",
                                     (unsigned int)send_len);
                            ota_raw_start_close("request_payload_too_long", 0U);
                            break;
                        }
                        memcpy(ota_raw_qisend_payload, common_send_buff, http_req_len);
                        ota_raw_qisend_payload[http_req_len] = 0x1AU;
                        ota_log_raw_tx_bytes(ota_raw_qisend_payload, send_len);
                        uart_ret = nb_modem_send_command_ota(ota_raw_qisend_payload, send_len);
#endif
                        OTA_LOGD("raw tcp http payload uart ret=%u len=%u\r\n",
                                 (unsigned int)uart_ret,
                                 (unsigned int)send_len);
                        if (uart_ret != 0U)
                        {
                            OTA_LOGE("raw tcp http payload uart send failed ret=%u\r\n",
                                     (unsigned int)uart_ret);
                            ota_raw_start_close("qisend_payload_uart_send_fail", 0U);
                            break;
                        }
                        http_get_timer=Timer_GetTickCount();
                        ota_connect_state=CONNECT_OTA_AT_RAW_WAIT_SEND_OK;
                        break;
                    }
                    if (prompt_result == OTA_RAW_PROMPT_ERROR)
                    {
                        ota_raw_query_socket_state("qisend_rejected");
                        ota_raw_start_close("qisend_rejected", 0U);
                        break;
                    }
                    if (prompt_result == OTA_RAW_PROMPT_TIMEOUT)
                    {
                        ota_raw_query_socket_state("qisend_prompt_timeout");
                        ota_raw_start_close("qisend_prompt_timeout", 0U);
                    }
              }
              break;

         case CONNECT_OTA_AT_RAW_WAIT_SEND_OK:
              ota_feed_watchdog_if_enabled();
              if(!Timer_PassedDelay(http_get_timer, OTA_RAW_TCP_SEND_TIMEOUT_MS))
              {
                    if (readLine(stringBuf, &recvLength, 0))
                    {
                        ota_log_raw_rx(stringBuf, recvLength);
                        if (strstr((const char *)stringBuf, "SEND OK") != NULL)
                        {
                            OTA_LOGI("raw tcp send ok\r\n");
                            ota_stream_reset();
                            ota_raw_http_reset();
                            ota_raw_total_timer=Timer_GetTickCount();
                            ota_raw_idle_timer=ota_raw_total_timer;
                            ota_clear_rx_buffer();
                            ota_connect_state=CONNECT_OTA_AT_RAW_WAIT_DATA;
                            break;
                        }
                        if (strstr((const char *)stringBuf, "SEND FAIL") != NULL ||
                            strstr((const char *)stringBuf, "ERROR") != NULL)
                        {
                            OTA_LOGE("raw tcp send failed\r\n");
                            ota_raw_start_close("send_failed", 0U);
                            break;
                        }
                        ota_clear_rx_buffer();
                    }
              }
              else
              {
                    OTA_LOGE("raw tcp send timeout\r\n");
                    ota_raw_start_close("send_timeout", 0U);
              }
              break;

         case CONNECT_OTA_AT_RAW_WAIT_DATA:
              ota_feed_watchdog_if_enabled();
              if (ota_stream_flash_error || ota_raw_http_state == OTA_RAW_HTTP_ERROR)
              {
                    ota_raw_start_close("raw_stream_error", 0U);
                    break;
              }
              if (ota_raw_body_done || ota_stream_finished)
              {
                    OTA_LOGI("raw tcp body done body_rx=%u stream_rx=%u\r\n",
                             (unsigned int)ota_raw_http_body_received,
                             (unsigned int)ota_stream_received);
                    ota_raw_start_close("raw_body_done", 1U);
                    break;
              }
              if (Timer_PassedDelay(ota_raw_total_timer, OTA_RAW_TCP_TOTAL_TIMEOUT_MS))
              {
                    OTA_LOGE("raw tcp total timeout body_rx=%u stream_rx=%u expected=%u\r\n",
                             (unsigned int)ota_raw_http_body_received,
                             (unsigned int)ota_stream_received,
                             (unsigned int)ota_stream_expected_size);
                    ota_raw_start_close("raw_total_timeout", 0U);
                    break;
              }
              if (Timer_PassedDelay(ota_raw_idle_timer, OTA_RAW_TCP_IDLE_TIMEOUT_MS))
              {
                    OTA_LOGE("raw tcp idle timeout body_rx=%u stream_rx=%u expected=%u closed=%u\r\n",
                             (unsigned int)ota_raw_http_body_received,
                             (unsigned int)ota_stream_received,
                             (unsigned int)ota_stream_expected_size,
                             ota_raw_socket_closed);
                    ota_raw_start_close("raw_idle_timeout", 0U);
                    break;
              }
              while (readLine(stringBuf, &recvLength, 0))
              {
                    ota_log_raw_rx(stringBuf, recvLength);
                    if (strstr((const char *)stringBuf, "+QIURC: \"closed\"") != NULL)
                    {
                        ota_raw_socket_closed = 1U;
                    }
                    ota_clear_rx_buffer();
              }
              snprintf(common_send_buff,
                       sizeof(common_send_buff),
                       "AT+QIRD=%u,%u\r\n",
                       (unsigned int)OTA_RAW_TCP_CONNECT_ID,
                       (unsigned int)OTA_RAW_TCP_QIRD_LEN);
              ota_log_raw_tx(common_send_buff);
              ota_clear_rx_buffer();
              nb_modem_send_command_ota(common_send_buff, strlen(common_send_buff));
              http_get_timer=Timer_GetTickCount();
              ota_connect_state=CONNECT_OTA_AT_RAW_QIRD;
              break;

         case CONNECT_OTA_AT_RAW_QIRD:
              ota_feed_watchdog_if_enabled();
              if(!Timer_PassedDelay(http_get_timer, 5000U))
              {
                    if (readLine(stringBuf, &recvLength, 0))
                    {
                        u16 qird_len;

                        ota_log_raw_rx(stringBuf, recvLength);
                        if (ota_parse_qird_len((const char *)stringBuf, &qird_len) == BOOL_TRUE)
                        {
                            ota_raw_qird_remaining = qird_len;
                            if (qird_len == 0U)
                            {
                                ota_raw_qird_zero_count++;
                                ota_connect_state=CONNECT_OTA_AT_RAW_QIRD_TRAILER;
                            }
                            else
                            {
                                ota_raw_qird_zero_count = 0U;
                                ota_raw_idle_timer=Timer_GetTickCount();
                                ota_connect_state=CONNECT_OTA_AT_RAW_QIRD_DATA;
                            }
                            ota_clear_rx_buffer();
                            break;
                        }
                        if (strstr((const char *)stringBuf, "+QIURC: \"closed\"") != NULL)
                        {
                            ota_raw_socket_closed = 1U;
                        }
                        if (strstr((const char *)stringBuf, "ERROR") != NULL)
                        {
                            OTA_LOGE("raw tcp qird error\r\n");
                            ota_raw_start_close("qird_error", 0U);
                            break;
                        }
                        ota_clear_rx_buffer();
                    }
              }
              else
              {
                    OTA_LOGE("raw tcp qird response timeout\r\n");
                    ota_raw_start_close("qird_timeout", 0U);
              }
              break;

         case CONNECT_OTA_AT_RAW_QIRD_DATA:
              {
                    u8 dat;
                    u16 qird_feed_counter;

                    ota_feed_watchdog_if_enabled();
                    qird_feed_counter = 0U;
                    while (ota_raw_qird_remaining > 0U && dequeue(&usartRecvQueue, &dat))
                    {
                        if (ota_raw_http_consume_byte(dat) != BOOL_TRUE)
                        {
                            OTA_LOGE("raw http consume failed state=%d body_rx=%u\r\n",
                                     ota_raw_http_state,
                                     (unsigned int)ota_raw_http_body_received);
                            ota_raw_start_close("raw_parse_error", 0U);
                            break;
                        }
                        ota_raw_qird_remaining--;
                        qird_feed_counter++;
                        if ((qird_feed_counter & 0x3FU) == 0U)
                        {
                            ota_feed_watchdog_if_enabled();
                        }
                    }
                    if (ota_connect_state != CONNECT_OTA_AT_RAW_QIRD_DATA)
                    {
                        break;
                    }
                    if (ota_raw_qird_remaining == 0U)
                    {
                        ota_connect_state=CONNECT_OTA_AT_RAW_QIRD_TRAILER;
                    }
                    else if (Timer_PassedDelay(http_get_timer, 5000U))
                    {
                        OTA_LOGE("raw tcp qird data timeout remain=%u\r\n",
                                 (unsigned int)ota_raw_qird_remaining);
                        ota_raw_start_close("qird_data_timeout", 0U);
                    }
              }
              break;

         case CONNECT_OTA_AT_RAW_QIRD_TRAILER:
               ota_feed_watchdog_if_enabled();
               if(!Timer_PassedDelay(http_get_timer, 5000U))
               {
                     if (readLine(stringBuf, &recvLength, 0))
                    {
                        ota_log_raw_rx(stringBuf, recvLength);
                        if (strstr((const char *)stringBuf, "OK") != NULL)
                        {
                            ota_clear_rx_buffer();
                            if (ota_stream_program_ready_pages() != BOOL_TRUE)
                            {
                                ota_stream_flash_error = 1U;
                                ota_raw_start_close("stream_program_error", 0U);
                                break;
                            }
                            ota_connect_state=CONNECT_OTA_AT_RAW_WAIT_DATA;
                            break;
                        }
                        if (strstr((const char *)stringBuf, "ERROR") != NULL)
                        {
                            OTA_LOGE("raw tcp qird trailer error\r\n");
                            ota_raw_start_close("qird_trailer_error", 0U);
                            break;
                        }
                        ota_clear_rx_buffer();
                    }
              }
              else
              {
                    OTA_LOGE("raw tcp qird trailer timeout\r\n");
                    ota_raw_start_close("qird_trailer_timeout", 0U);
              }
              break;

         case CONNECT_OTA_AT_RAW_CLOSE:
              ota_feed_watchdog_if_enabled();
              if(!Timer_PassedDelay(http_get_timer, OTA_QHTTPSTOP_TIMEOUT_MS))
              {
                    if (readLine(stringBuf, &recvLength, 0))
                    {
                        ota_log_raw_rx(stringBuf, recvLength);
                        if (strstr((const char *)stringBuf, "OK") != NULL ||
                            strstr((const char *)stringBuf, "ERROR") != NULL)
                        {
                            ota_clear_rx_buffer();
                            if (ota_raw_close_success)
                            {
                                ota_connect_state=CONNECT_OTA_AT_STREAM_VERIFY;
                            }
                            else
                            {
                                ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH;
                                ota_reset_after_diag();
                            }
                            break;
                        }
                        ota_clear_rx_buffer();
                    }
              }
              else
              {
                    OTA_LOGW("raw tcp close timeout success=%u\r\n", ota_raw_close_success);
                    if (ota_raw_close_success)
                    {
                        ota_connect_state=CONNECT_OTA_AT_STREAM_VERIFY;
                    }
                    else
                    {
                        ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH;
                        ota_reset_after_diag();
                    }
              }
              break;

#if !OTA_RAW_TCP_STREAM_DEBUG
         case CONNECT_OTA_AT_QHTTPCFG_contextid:
             if(send_AT_Command_machine_finish()==TRUE)
             {
                  ota_log_stringbuf_rx_if_present();
                  OTA_LOGI("http cfg responseheader=0\r\n");
                  ota_log_raw_tx("AT+QHTTPCFG=\"responseheader\",0\r\n");
                  send_AT_Command_machine_star("AT+QHTTPCFG=\"responseheader\",0\r\n", strlen("AT+QHTTPCFG=\"responseheader\",0\r\n"),"OK\r\n", 20, 0);
                  ota_connect_state=CONNECT_OTA_AT_QHTTPCFG_responseheader;
             }
           break;

         case CONNECT_OTA_AT_QHTTPCFG_responseheader:
             if(send_AT_Command_machine_finish()==TRUE)
             {
                  ota_log_stringbuf_rx_if_present();
                  OTA_LOGI("http cfg rspout/auto=0\r\n");
                  ota_log_raw_tx("AT+QHTTPCFG=\"rspout/auto\",0\r\n");
                  send_AT_Command_machine_star("AT+QHTTPCFG=\"rspout/auto\",0\r\n", strlen("AT+QHTTPCFG=\"rspout/auto\",0\r\n"),"OK\r\n", 20, 0);
                  ota_connect_state=CONNECT_OTA_AT_QHTTPCFG_rspout_auto;
              }
              break;

         case CONNECT_OTA_AT_QHTTPCFG_rspout_auto:
             if(send_AT_Command_machine_finish()==TRUE)
             {
                  ota_log_stringbuf_rx_if_present();
#if OTA_HTTP10_CLOSE_WORKAROUND
                  OTA_LOGI("http cfg requestheader=1 for http10_close workaround\r\n");
                  ota_log_raw_tx("AT+QHTTPCFG=\"requestheader\",1\r\n");
                  send_AT_Command_machine_star("AT+QHTTPCFG=\"requestheader\",1\r\n",strlen("AT+QHTTPCFG=\"requestheader\",1\r\n"),"OK\r\n", 20, 0);
#else
                  OTA_LOGI("http cfg requestheader=0 for standard qhttpget\r\n");
                  ota_log_raw_tx("AT+QHTTPCFG=\"requestheader\",0\r\n");
                  send_AT_Command_machine_star("AT+QHTTPCFG=\"requestheader\",0\r\n",strlen("AT+QHTTPCFG=\"requestheader\",0\r\n"),"OK\r\n", 20, 0);
#endif
                  ota_connect_state=CONNECT_OTA_AT_QHTTPCFG_requestheader;
              }
              break;

         case CONNECT_OTA_AT_QHTTPCFG_requestheader:
              if(send_AT_Command_machine_finish()==TRUE)
              {
                  ota_log_stringbuf_rx_if_present();
#if OTA_HTTP10_CLOSE_WORKAROUND
                  ota_start_qhttpurl();
#else
                  OTA_LOGI("http cfg header Connection close\r\n");
                  ota_log_raw_tx("AT+QHTTPCFG=\"header\",\"Connection: close\"\r\n");
                  send_AT_Command_machine_star("AT+QHTTPCFG=\"header\",\"Connection: close\"\r\n",
                                               strlen("AT+QHTTPCFG=\"header\",\"Connection: close\"\r\n"),
                                               "OK", 20, 0);
                  ota_connect_state=CONNECT_OTA_AT_QHTTPCFG_header_close;
#endif
              }
              break;

         case CONNECT_OTA_AT_QHTTPCFG_header_close:
              if(send_AT_Command_machine_finish()==TRUE)
              {
                  ota_log_stringbuf_rx_if_present();
                  if (strstr((const char *)stringBuf, "ERROR") != NULL ||
                      strstr((const char *)stringBuf, "+CME ERROR:") != NULL)
                  {
                      OTA_LOGW("http cfg header Connection close rejected, continue standard get\r\n");
                  }
                  ota_start_qhttpurl();
              }
              break;

         case CONNECT_OTA_AT_QHTTPURL:
             if(send_AT_Command_machine_finish()==TRUE)
             {
                 u16 length;
                 ota_log_stringbuf_rx_if_present();
                memset(common_send_buff,0x00,sizeof(common_send_buff));
                if (ota_build_ota_url_string(common_send_buff, sizeof(common_send_buff), &length) == BOOL_FALSE)
                {
                    ota_reset_for_bad_url();
                    break;
                }
                 OTA_LOGD("url sent=%s\r\n", ota_get_download_url());
                 ota_log_raw_tx(common_send_buff);
                 send_AT_Command_machine_star(common_send_buff, strlen(common_send_buff), "OK",20, 0);
                // send_AT_Command_machine_star("http://47.120.15.220:888/downloads/cat1.bin\r\n", strlen("http://47.120.15.220:888/downloads/cat1.bin\r\n"), "OK",20, 0);      //ä¸è¦å é¤
                //  send_AT_Command_machine_star("http://47.120.15.220:888/downloads/cat120250401162230.bin\r\n", strlen("http://47.120.15.220:888/downloads/cat120250401162230.bin\r\n"), "OK",20, 0);
                   ota_connect_state=CONNECT_OTA_AT_QHTTPURL_WAIT_OK;
             }
              break;

          case CONNECT_OTA_AT_QHTTPURL_WAIT_OK:
              if(send_AT_Command_machine_finish()==TRUE)
              {
                   ota_log_stringbuf_rx_if_present();
                   if (strstr((const char *)stringBuf, "ERROR") != NULL ||
                       strstr((const char *)stringBuf, "+CME ERROR:") != NULL)
                   {
                       OTA_LOGE("download failed: qhttpurl body rejected\r\n");
                       ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH;
                       ota_reset_after_diag();
                       ota_clear_rx_buffer();
                       break;
                   }
                   OTA_LOGI("stream mode: bypass UFS\r\n");
                   ota_clear_rx_buffer();
                   ota_connect_state=CONNECT_OTA_AT_QHTTPGET;
              }
              break;

#if OTA_USE_QHTTPREADFILE_UFS
          case CONNECT_OTA_AT_QFDEL :
              if(send_AT_Command_machine_finish()==TRUE)
              {
                   ota_log_stringbuf_rx_if_present();
#if OTA_DEBUG_CLEAR_ALL_UFS
                   snprintf(common_send_buff,
                            sizeof(common_send_buff),
                            "AT+QFDEL=\"*\"\r\n");
                   OTA_LOGW("debug cleanup: delete all UFS files before download\r\n");
#else
                   snprintf(common_send_buff,
                            sizeof(common_send_buff),
                            "AT+QFDEL=\"%s%s\"\r\n",
                            OTA_LOCAL_UFS_PATH_PREFIX,
                            firm_name_buffer);
                   OTA_LOGI("delete old ufs file path=%s%s\r\n",
                            OTA_LOCAL_UFS_PATH_PREFIX,
                            firm_name_buffer);
#endif
                   ota_log_raw_tx(common_send_buff);
                   send_AT_Command_machine_star(common_send_buff,strlen(common_send_buff), "OK",20, 0);
                   ota_connect_state=CONNECT_OTA_AT_QFLDS;
              }
              break;

          case CONNECT_OTA_AT_QFLDS:
                {
                    u32 free_size;
                    u32 total_size;

                    ota_feed_watchdog_if_enabled();
                    if (ota_qflds_command_sent == 0U)
                    {
                        if(send_AT_Command_machine_finish()==TRUE)
                        {
                            ota_log_stringbuf_rx_if_present();
                            ota_qflds_found = 0;
                            ota_qflds_free_size = 0;
                            ota_qflds_total_size = 0;
                            ota_clear_rx_buffer();
                            OTA_LOGI("query ufs space before download\r\n");
                            ota_log_raw_tx("AT+QFLDS=\"UFS\"\r\n");
                            nb_modem_send_command_ota("AT+QFLDS=\"UFS\"\r\n", strlen("AT+QFLDS=\"UFS\"\r\n"));
                            http_get_timer=Timer_GetTickCount();
                            ota_qflds_command_sent = 1U;
                        }
                        break;
                    }

                    if(!Timer_PassedDelay(http_get_timer, OTA_QFLDS_TIMEOUT_MS))
                    {
                        if (readLine(stringBuf, &recvLength, 0))
                        {
                            ota_log_raw_rx(stringBuf, recvLength);
                            if (ota_parse_qflds_line((const char *)stringBuf, &free_size, &total_size) == BOOL_TRUE)
                            {
                                ota_qflds_found = 1U;
                                ota_qflds_free_size = free_size;
                                ota_qflds_total_size = total_size;
                                OTA_LOGI("ufs space before download free=%u total=%u\r\n",
                                         (unsigned int)ota_qflds_free_size,
                                         (unsigned int)ota_qflds_total_size);
                            }
                            if (strstr((const char *)stringBuf, "OK") != NULL)
                            {
                                if (ota_qflds_found == 0U)
                                {
                                    OTA_LOGW("ufs space query finished but no +QFLDS parsed\r\n");
                                }
                                ota_qflds_command_sent = 0U;
                                ota_clear_rx_buffer();
                                ota_connect_state=CONNECT_OTA_AT_QHTTPGET;
                                break;
                            }
                            if (strstr((const char *)stringBuf, "ERROR") != NULL ||
                                strstr((const char *)stringBuf, "+CME ERROR:") != NULL)
                            {
                                OTA_LOGW("ufs space query error, continue download for diagnosis\r\n");
                                ota_qflds_command_sent = 0U;
                                ota_clear_rx_buffer();
                                ota_connect_state=CONNECT_OTA_AT_QHTTPGET;
                                break;
                            }
                            ota_clear_rx_buffer();
                        }
                    }
                    else
                    {
                        OTA_LOGW("ufs space query timeout, continue download for diagnosis\r\n");
                        ota_qflds_command_sent = 0U;
                        ota_clear_rx_buffer();
                        ota_connect_state=CONNECT_OTA_AT_QHTTPGET;
                    }
                }
                break;

#endif

           case CONNECT_OTA_AT_QHTTPGET:
                {
                    static char buff[64];
#if OTA_HTTP10_CLOSE_WORKAROUND
                    u16 header_len;

                    if (ota_build_http10_close_header(common_send_buff, sizeof(common_send_buff), &header_len) == BOOL_FALSE)
                    {
                        OTA_LOGE("download failed: cannot build http10 close header\r\n");
                        ota_reset_for_bad_url();
                        break;
                    }
                    snprintf(buff,
                             sizeof(buff),
                             "AT+QHTTPGET=%u,%u,%u\r\n",
                             (unsigned int)OTA_QHTTPGET_WAIT_SEC,
                             (unsigned int)header_len,
                             (unsigned int)OTA_QHTTPGET_INPUT_TIMEOUT_SEC);
                    OTA_LOGI("qhttpget start mode=http10_close wait=%us header_len=%u input_timeout=%us\r\n",
                             (unsigned int)OTA_QHTTPGET_WAIT_SEC,
                             (unsigned int)header_len,
                             (unsigned int)OTA_QHTTPGET_INPUT_TIMEOUT_SEC);
                    OTA_LOGD("http url=%s\r\n", ota_get_download_url());
                    ota_log_raw_tx(buff);
                    send_AT_Command_machine_star(buff,strlen(buff),"CONNECT",20, 0);
                    ota_connect_state=CONNECT_OTA_AT_QHTTPGET_HEADER;
#else
                    snprintf(buff, sizeof(buff), "AT+QHTTPGET=%u\r\n", (unsigned int)OTA_QHTTPGET_WAIT_SEC);
                    OTA_LOGI("qhttpget start wait=%us\r\n", (unsigned int)OTA_QHTTPGET_WAIT_SEC);
                    OTA_LOGD("http url=%s\r\n", ota_get_download_url());
                    ota_log_raw_tx(buff);
                    ota_clear_rx_buffer();
                    nb_modem_send_command_ota(buff, strlen(buff));
                    http_get_timer=Timer_GetTickCount();
                    ota_connect_state=CONNECT_OTA_AT_QHTTPGET_WAIT;
#endif
                }
                break;

           case CONNECT_OTA_AT_QHTTPGET_HEADER:
                if(send_AT_Command_machine_finish()==TRUE)
                {
                    ota_log_stringbuf_rx_if_present();
                    if (strstr((const char *)stringBuf, "CONNECT") == NULL)
                    {
                        OTA_LOGE("download failed: qhttpget header input not ready raw=%s\r\n", (char *)stringBuf);
                        ota_clear_rx_buffer();
                        ota_start_http_stop_cleanup("qhttpget_no_connect");
                        break;
                    }
                    OTA_LOGI("qhttpget http10 close header input len=%u\r\n",
                             (unsigned int)strlen(common_send_buff));
                    ota_log_raw_tx(common_send_buff);
                    ota_clear_rx_buffer();
                    nb_modem_send_command_ota(common_send_buff, strlen(common_send_buff));
                    http_get_timer=Timer_GetTickCount();
                    ota_connect_state=CONNECT_OTA_AT_QHTTPGET_WAIT;
                }
                break;

           case CONNECT_OTA_AT_QHTTPGET_WAIT:
                ota_feed_watchdog_if_enabled();
                if(!Timer_PassedDelay(http_get_timer, OTA_QHTTPGET_TIMEOUT_MS))
                {
                    if (readLine(stringBuf, &recvLength, 0))
                    {
                        ota_log_raw_rx(stringBuf, recvLength);
                        if (ota_parse_qhttpget_result((const char *)stringBuf) == BOOL_TRUE)
                        {
                            OTA_LOGI("qhttpget result err=%d http=%d content_len_present=%u content_len=%u\r\n",
                                     ota_http_err_code,
                                     ota_http_status_code,
                                     ota_http_content_length_present,
                                     (unsigned int)ota_http_content_length);
                            OTA_LOGI("server response: err=%d http=%d content_len=%u\r\n",
                                     ota_http_err_code,
                                     ota_http_status_code,
                                     (unsigned int)ota_http_content_length);
                            OTA_LOGI("UFS check bypassed cl_present=%u cl=%u\r\n",
                                     ota_http_content_length_present,
                                     (unsigned int)ota_http_content_length);
                            if (ota_http_err_code == 0 && ota_http_status_code >= 200 && ota_http_status_code < 300)
                            {
                                ota_connect_state=CONNECT_OTA_AT_QHTTPREAD;
                            }
                            else
                            {
                                OTA_LOGE("download failed: server response err=%d http=%d\r\n",
                                         ota_http_err_code,
                                         ota_http_status_code);
                                ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH;
                                ota_reset_after_diag();
                            }
                            ota_clear_rx_buffer();
                            break;
                        }
                        if (strstr((const char *)stringBuf, "ERROR") != NULL ||
                            strstr((const char *)stringBuf, "+CME ERROR:") != NULL)
                        {
                            OTA_LOGE("download failed: qhttpget error\r\n");
                            ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH;
                            ota_reset_after_diag();
                            ota_clear_rx_buffer();
                            break;
                        }
                        ota_clear_rx_buffer();
                    }
                }
                else
                {
                    OTA_LOGE("qhttpget timeout elapsed=%u limit=%u\r\n",
                             (unsigned int)(Timer_GetTickCount() - http_get_timer),
                             (unsigned int)OTA_QHTTPGET_TIMEOUT_MS);
                    ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH;
                    ota_reset_after_diag();
                }
                break;

       case CONNECT_OTA_AT_QHTTPREAD:
                snprintf(common_send_buff,
                         sizeof(common_send_buff),
                         "AT+QHTTPREAD=%u\r\n",
                         (unsigned int)OTA_QHTTPREAD_WAIT_SEC);
                OTA_LOGI("qhttpread stream start wait=%us direct_to_backup=1\r\n",
                         (unsigned int)OTA_QHTTPREAD_WAIT_SEC);
                ota_stream_reset();
                if (ota_stream_flash_error)
                {
                    ota_start_http_stop_cleanup("stream_expected_size_invalid");
                    break;
                }
                ota_log_raw_tx(common_send_buff);
                ota_clear_rx_buffer();
                nb_modem_send_command_ota(common_send_buff, strlen(common_send_buff));
                http_get_timer=Timer_GetTickCount();
                ota_connect_state=CONNECT_OTA_AT_QHTTPREAD_WAIT_CONNECT;
              break;

        case CONNECT_OTA_AT_QHTTPREAD_WAIT_CONNECT:
              ota_feed_watchdog_if_enabled();
              if(!Timer_PassedDelay(http_get_timer, OTA_QHTTPREAD_TIMEOUT_MS))
              {
                    if (readLine(stringBuf, &recvLength, 0))
                    {
                        ota_log_raw_rx(stringBuf, recvLength);
                        if (strstr((const char *)stringBuf, "CONNECT") != NULL)
                        {
                            OTA_LOGI("qhttpread stream connected: start binary body receive\r\n");
                            ota_clear_rx_buffer();
                            http_get_timer=Timer_GetTickCount();
                            ota_connect_state=CONNECT_OTA_AT_QHTTPREAD_STREAM;
                            break;
                        }
                        if (strstr((const char *)stringBuf, "ERROR") != NULL ||
                            strstr((const char *)stringBuf, "+CME ERROR:") != NULL)
                        {
                            OTA_LOGE("download failed: qhttpread connect error\r\n");
                            ota_start_http_stop_cleanup("qhttpread_connect_error");
                            break;
                        }
                        ota_clear_rx_buffer();
                    }
              }
              else
              {
                    OTA_LOGE("qhttpread connect timeout elapsed=%u limit=%u\r\n",
                             (unsigned int)(Timer_GetTickCount() - http_get_timer),
                             (unsigned int)OTA_QHTTPREAD_TIMEOUT_MS);
                    ota_start_http_stop_cleanup("qhttpread_connect_timeout");
              }
              break;

        case CONNECT_OTA_AT_QHTTPREAD_STREAM:
              ota_feed_watchdog_if_enabled();
              ota_stream_process_queue();
              if (ota_stream_flash_error)
              {
                    ota_start_http_stop_cleanup("stream_flash_error");
                    break;
              }
              if (ota_stream_finished)
              {
                    OTA_LOGI("qhttpread body saved to backup: wait trailer\r\n");
                    http_get_timer=Timer_GetTickCount();
                    ota_connect_state=CONNECT_OTA_AT_QHTTPREAD_TRAILER;
                    break;
              }
              if(Timer_PassedDelay(http_get_timer, OTA_QHTTPREAD_TIMEOUT_MS))
              {
                    OTA_LOGE("qhttpread stream timeout elapsed=%u limit=%u rx=%u known=%u exp=%u qdrop=%u ore_delta=%u\r\n",
                             (unsigned int)(Timer_GetTickCount() - http_get_timer),
                             (unsigned int)OTA_QHTTPREAD_TIMEOUT_MS,
                             (unsigned int)ota_stream_received,
                             ota_stream_expected_known,
                             (unsigned int)ota_stream_expected_size,
                             (unsigned int)(usart_queue_drop_count - ota_stream_start_drop_count),
                             (unsigned int)(usart_uart1_ore_count - ota_stream_start_ore_count));
                    ota_start_http_stop_cleanup("qhttpread_stream_timeout");
              }
              break;

        case CONNECT_OTA_AT_QHTTPREAD_TRAILER:
              ota_feed_watchdog_if_enabled();
              if(!Timer_PassedDelay(http_get_timer, OTA_QHTTPSTOP_TIMEOUT_MS))
              {
                    if (readLine(stringBuf, &recvLength, 0))
                    {
                        ota_log_raw_rx(stringBuf, recvLength);
                        if (strstr((const char *)stringBuf, "+QHTTPREAD: 0") != NULL ||
                            strstr((const char *)stringBuf, "+QHTTPREAD:0") != NULL)
                        {
                            OTA_LOGI("qhttpread trailer success\r\n");
                            ota_clear_rx_buffer();
                            ota_connect_state=CONNECT_OTA_AT_STREAM_VERIFY;
                            break;
                        }
                        if (strstr((const char *)stringBuf, "+QHTTPREAD:") != NULL)
                        {
                            OTA_LOGE("download failed: qhttpread trailer error line=%s\r\n", (char *)stringBuf);
                            ota_start_http_stop_cleanup("qhttpread_trailer_error");
                            break;
                        }
                        if (strstr((const char *)stringBuf, "ERROR") != NULL ||
                            strstr((const char *)stringBuf, "+CME ERROR:") != NULL)
                        {
                            OTA_LOGE("download failed: qhttpread trailer error\r\n");
                            ota_start_http_stop_cleanup("qhttpread_trailer_error");
                            break;
                        }
                        ota_clear_rx_buffer();
                    }
              }
              else
              {
                    OTA_LOGW("qhttpread trailer timeout, verify backup anyway received=%u flushed=%u\r\n",
                             (unsigned int)ota_stream_received,
                             (unsigned int)ota_stream_flushed);
                    ota_connect_state=CONNECT_OTA_AT_STREAM_VERIFY;
              }
              break;

#endif

        case CONNECT_OTA_AT_STREAM_VERIFY:
              if (ota_stream_verify_backup() == BOOL_TRUE)
              {
                    OTA_LOGI("stream verify ok: mark upgrade and jump boot size=%u\r\n",
                             (unsigned int)ota_stream_expected_size);
                    sys_data.sn = 0xaa5555aa;
                    sys_data_store();
                    ota_feed_watchdog_if_enabled();
                    HAL_Delay(50);
                    ota_feed_watchdog_if_enabled();
                    MCU_OTA_state=MCU_OTA__COMPLETE;
                    iap_jump2boot();
                    break;
              }
              OTA_LOGE("stream verify failed: no boot jump\r\n");
              ota_start_http_stop_cleanup("stream_verify_failed");
              break;

#if OTA_USE_QHTTPREADFILE_UFS
       case CONNECT_OTA_AT_QHTTPREADFILE:
                snprintf(common_send_buff,
                         sizeof(common_send_buff),
                         "AT+QHTTPREADFILE=\"%s%s\",%u\r\n",
                         OTA_LOCAL_UFS_PATH_PREFIX,
                         firm_name_buffer,
                         (unsigned int)OTA_QHTTPREADFILE_WAIT_SEC);
                OTA_LOGI("qhttpreadfile start file=%s%s wait=%us\r\n",
                         OTA_LOCAL_UFS_PATH_PREFIX,
                         firm_name_buffer,
                         (unsigned int)OTA_QHTTPREADFILE_WAIT_SEC);
                OTA_LOGI("save to module fs start file=%s%s wait=%us\r\n",
                         OTA_LOCAL_UFS_PATH_PREFIX,
                         firm_name_buffer,
                         (unsigned int)OTA_QHTTPREADFILE_WAIT_SEC);
                ota_log_raw_tx(common_send_buff);
                ota_readfile_command_ok_logged = 0;
                ota_clear_rx_buffer();
                nb_modem_send_command_ota(common_send_buff,strlen(common_send_buff));
                http_get_timer=Timer_GetTickCount();
                ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_WAIT;
              break;

        case CONNECT_OTA_AT_QHTTPREADFILE_STROE_WAIT:
              {
                int readfile_err;

                ota_feed_watchdog_if_enabled();

                if(!Timer_PassedDelay(http_get_timer, OTA_QHTTPREADFILE_TIMEOUT_MS))    //    &&Timer_PassedDelay(http_get__data_wait_timer, 150)
                {
                    if (readLine(stringBuf, &recvLength, 0))
                    {
                        ota_log_raw_rx(stringBuf, recvLength);
                        OTA_LOGD("module fs save line len=%u\r\n", recvLength);
                        if (strstr((const char *)stringBuf, "OK") != NULL &&
                            ota_readfile_command_ok_logged == 0)
                        {
                            ota_readfile_command_ok_logged = 1;
                            OTA_LOGI("qhttpreadfile command accepted\r\n");
                            OTA_LOGI("module fs save command accepted\r\n");
                        }
                        if (ota_parse_qhttpreadfile_result((const char *)stringBuf, &readfile_err) == BOOL_TRUE)
                        {
                            OTA_LOGI("qhttpreadfile result err=%d\r\n", readfile_err);
                            OTA_LOGI("module fs save result err=%d\r\n", readfile_err);
                            ota_last_readfile_err_code = readfile_err;
                            if (readfile_err != 0)
                            {
                                OTA_LOGE("qhttpreadfile result err=%d\r\n", readfile_err);
                                OTA_LOGE("download failed: module fs save result err=%d\r\n", readfile_err);
                                ota_log_qhttpreadfile_error_detail(readfile_err);
                                ota_start_readfile_fs_diag("readfile_error");
                                break;
                            }
                            ota_start_readfile_fs_diag("readfile_success");
                            break;
                        }
                        if (strstr((const char *)stringBuf, "ERROR") != NULL ||
                            strstr((const char *)stringBuf, "+CME ERROR:") != NULL)
                        {
                            OTA_LOGE("download failed: module fs save error\r\n");
                            ota_start_readfile_fs_diag("readfile_error");
                            break;
                        }
                        ota_clear_rx_buffer();
                     }
                } // ç­å¾
æåå¨å­ååºæ¶æ¯<80S
                else
                {
                     OTA_LOGE("qhttpreadfile timeout elapsed=%u limit=%u\r\n",
                              (unsigned int)(Timer_GetTickCount() - http_get_timer),
                              (unsigned int)OTA_QHTTPREADFILE_TIMEOUT_MS);
                     OTA_LOGE("download failed: module fs save timeout wait=%ums file=%s%s\r\n",
                              (unsigned int)OTA_QHTTPREADFILE_TIMEOUT_MS,
                              OTA_LOCAL_UFS_PATH_PREFIX,
                              firm_name_buffer);
                     ota_start_readfile_fs_diag("readfile_timeout");
                }
              }
              break;
        case CONNECT_OTA_AT_QHTTPREADFILE_QFLST_DIAG:
             ota_feed_watchdog_if_enabled();
             if(!Timer_PassedDelay(http_get_timer, OTA_QHTTPREADFILE_DIAG_TIMEOUT_MS))
             {
                if (readLine(stringBuf, &recvLength, 0))
                {
                    u32 file_size;

                    ota_log_raw_rx(stringBuf, recvLength);
                    OTA_LOGI("module fs diag line len=%u\r\n", recvLength);
                    if (ota_parse_qflst_firmware_line((const char *)stringBuf, &file_size) == BOOL_TRUE)
                    {
                        ota_diag_qflst_found = 1;
                        ota_diag_qflst_size = file_size;
                        OTA_LOGI("ufs file found path=%s%s size=%u\r\n",
                                 OTA_LOCAL_UFS_PATH_PREFIX,
                                 firm_name_buffer,
                                 (unsigned int)ota_diag_qflst_size);
                        OTA_LOGI("module fs file present file=%s%s size=%u\r\n",
                                 OTA_LOCAL_UFS_PATH_PREFIX,
                                 firm_name_buffer,
                                 (unsigned int)ota_diag_qflst_size);
                    }
                    if (strstr((const char *)stringBuf, "OK") != NULL)
                    {
                        OTA_LOGI("qflst diagnose finish found=%u size=%u\r\n",
                                 ota_diag_qflst_found,
                                 (unsigned int)ota_diag_qflst_size);
                        if (ota_diag_is_success_path && ota_diag_qflst_found && ota_diag_qflst_size > 0U)
                        {
                            OTA_LOGI("DEBUG DOWNLOAD ONLY SUCCESS: UFS file ready path=%s%s size=%u\r\n",
                                     OTA_LOCAL_UFS_PATH_PREFIX,
                                     firm_name_buffer,
                                     (unsigned int)ota_diag_qflst_size);
                            OTA_LOGI("skip mcu flash write in debug version\r\n");
                            ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH;
                            MCU_OTA_state=MCU_OTA_STATE_IDLE;
                            set_gateway_state_idle();
                            changea_to_MQTT_modle();
                            break;
                        }
                        if (ota_diag_qflst_found)
                        {
                            OTA_LOGE("download failed: no readfile success urc, but UFS file exists size=%u\r\n",
                                     (unsigned int)ota_diag_qflst_size);
                        }
                        else
                        {
                            OTA_LOGE("ufs file missing path=%s%s\r\n",
                                     OTA_LOCAL_UFS_PATH_PREFIX,
                                     firm_name_buffer);
                            OTA_LOGE("download failed: no readfile success urc and UFS file missing\r\n");
                        }
                        ota_start_http_stop_cleanup("after_readfile_diag");
                        break;
                    }
                    if (strstr((const char *)stringBuf, "ERROR") != NULL ||
                        strstr((const char *)stringBuf, "+CME ERROR:") != NULL)
                    {
                        OTA_LOGE("module fs diag failed\r\n");
                        ota_start_http_stop_cleanup("after_readfile_diag_error");
                        break;
                    }
                    ota_clear_rx_buffer();
                }
             }
             else
             {
                OTA_LOGE("module fs diag timeout file=%s%s found=%u size=%u\r\n",
                         OTA_LOCAL_UFS_PATH_PREFIX,
                         firm_name_buffer,
                         ota_diag_qflst_found,
                         (unsigned int)ota_diag_qflst_size);
                ota_start_http_stop_cleanup("after_readfile_diag_timeout");
             }
             break;
#endif

        case CONNECT_OTA_AT_QHTTPSTOP_CLEANUP:
             ota_feed_watchdog_if_enabled();
             if(!Timer_PassedDelay(http_get_timer, OTA_QHTTPSTOP_TIMEOUT_MS))
             {
                if (readLine(stringBuf, &recvLength, 0))
                {
                    ota_log_raw_rx(stringBuf, recvLength);
                    if (strstr((const char *)stringBuf, "OK") != NULL)
                    {
                        OTA_LOGI("http cleanup finish: qhttpstop ok last_readfile_err=%d\r\n",
                                 ota_last_readfile_err_code);
                        ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH;
                        ota_reset_after_diag();
                        break;
                    }
                    if (strstr((const char *)stringBuf, "ERROR") != NULL ||
                        strstr((const char *)stringBuf, "+CME ERROR:") != NULL)
                    {
                        OTA_LOGW("http cleanup finish: qhttpstop error ignored last_readfile_err=%d\r\n",
                                 ota_last_readfile_err_code);
                        ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH;
                        ota_reset_after_diag();
                        break;
                    }
                    ota_clear_rx_buffer();
                }
             }
             else
             {
                OTA_LOGW("http cleanup timeout wait=%ums last_readfile_err=%d\r\n",
                         (unsigned int)OTA_QHTTPSTOP_TIMEOUT_MS,
                         ota_last_readfile_err_code);
                ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH;
                ota_reset_after_diag();
             }
             break;
        case CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH:
                break;
     }
 }

 typedef enum
 {
     OTA_PROGRESS_AT_REPORT_IDLE,
     OTA_PROGRESS_REPORT_CONFIG,
     OTA_PROGRESS_AT_REPORT_PROGRESS,
     OTA_PROGRESS_AT_REPORT_CLOSE,
     OTA_PROGRESS_AT_CANHGEBACKTO_OTA,
     OTA_PROGRESS_AT_REPORT_FINISH,
 }OTA_PROGRESS_en;
 OTA_PROGRESS_en ota_progress_state=OTA_PROGRESS_AT_REPORT_IDLE;
 /************************************
åè½æè¿°ï¼å¯å¨è¿åº¦ä¸æ?
*************************************/
 void ota_progress_report_statr(void)
 {
   ota_progress_state=OTA_PROGRESS_REPORT_CONFIG;
 }

/************************************
åè½æè¿°ï¼æ¥è¯¢è¿åº¦ä¸æ¥å®å?
*************************************/
boolean_en ota_progress_report_is_finish(void)
{
   if(ota_progress_state==OTA_PROGRESS_AT_REPORT_FINISH)
   {
    return BOOL_TRUE;
   }
   else
   {
       return BOOL_FALSE;
   }

 }


/************************************
åè½æè¿°ï¼ä¸ä¼ åºä»¶è¿åº?
æ³¨æï¼?   åªè½å¨OTAå
³é­æ¶ä¸ä¼?
*************************************/
void upload_ota_progress_fsm_process(void)
{
   switch(ota_progress_state)
   {
       case OTA_PROGRESS_AT_REPORT_IDLE :
                break;

       case   OTA_PROGRESS_REPORT_CONFIG:
           if(send_AT_Command_machine_finish()==TRUE)
            {
                OTA_ENABLE=0;// close OTA and switch to MQTT
                 _4G_configModule_star_from_onestate( CONNECT_CONFIG_AT_qmtping) ;//ä»æä¸ä¸ªç¶æå¼å§å¯å?åéMQTTå¼å¯æä»?
                 ota_progress_state=OTA_PROGRESS_AT_REPORT_PROGRESS;
            }

          break;
       case    OTA_PROGRESS_AT_REPORT_PROGRESS:

              if(_4G_configModule_machine_finish() ==BOOL_TRUE)
              {
                  if(last_server_big_pick)
                  {
                    upload_ota_progress_bar(DEVICE_ID ,90 );
                  }
                  else if(server_big_pick_counter==0)
                  {
                   upload_ota_progress_bar(DEVICE_ID ,30 );
                  }
                  else if(server_big_pick_counter==1)
                  {
                    upload_ota_progress_bar(DEVICE_ID ,50 );
                  }
                  wait_openota_timer =Timer_GetTickCount();//è·åæ¶é´ç¹ï¼è¿åº¦ä¸æ¥éè¦èæ¶100mSï¼?
                  ota_progress_state=OTA_PROGRESS_AT_REPORT_CLOSE;
               }
                 break;

        case   OTA_PROGRESS_AT_REPORT_CLOSE:
               if (Timer_PassedDelay(wait_openota_timer, 200))    // Wait for progress data before QMTCLOSE.
               {
                 send_AT_Command_machine_star("AT+QMTCLOSE=0\r\n",strlen("AT+QMTCLOSE=0\r\n"),"OK", 2, 1);//+QMTCLOSE: <client_idx>,<result>+QMTCLOSE: 0,0//"+QMTCLOSE: 0,0"
                 ota_progress_state=OTA_PROGRESS_AT_CANHGEBACKTO_OTA;
               }
               break;

           case  OTA_PROGRESS_AT_CANHGEBACKTO_OTA:
                if(send_AT_Command_machine_finish()==TRUE)
                {
                  OTA_ENABLE=1;
                  ota_progress_state=OTA_PROGRESS_AT_REPORT_FINISH;
                }
                 break;

           case OTA_PROGRESS_AT_REPORT_FINISH:
             break;
     }

}

/************************************
åè½æè¿°ï¼è½¬ç§»åºä»¶æåæ¥è¯?
*************************************/
boolean_en  mcu_copy_firmware_finish(void)
{
      if( MCU_OTA_state==MCU_OTA__COMPLETE)
      {

          return BOOL_TRUE;
      }
      else{
          return BOOL_FALSE;
      }
}
/************************************
åè½æè¿°ï¼æ¥æ¶åºä»¶ç¶ææ¥è¯?
*************************************/
boolean_en  mcu_copy_firmware_getdata(void)
{
      if( MCU_OTA_state==MCU_OTA_MCU_GETDATA)
      {
          return BOOL_TRUE;
      }
      else{
          return BOOL_FALSE;
      }
}

/************************************
åè½æè¿°ï¼å¼å§è½¬ç§»åºä»?
*************************************/
void  mcu_copy_firmware_star(void)
     {
#if !OTA_USE_QHTTPREADFILE_UFS
         OTA_LOGW("module fs copy disabled: raw tcp stream writes OTA backup directly\r\n");
         MCU_OTA_state=MCU_OTA_STATE_IDLE;
#else
#if OTA_DEBUG_DOWNLOAD_ONLY
         OTA_LOGW("debug download only: skip mcu_copy_firmware_star\r\n");
         MCU_OTA_state=MCU_OTA_STATE_IDLE;
#else
         OTA_LOGI("move firmware to flash start src=UFS:%s dst=0x%08x\r\n",
                  firm_name_buffer,
                  (unsigned int)OTABAKROM_STARTADDR);
         MCU_OTA_state=MCU_OTA_STATE_RESETING;
         server_big_pick_counter=0;
         save_byete_counter=0;
         firmware_total_size=0;
         SERVER_CHECSUM=0;
         last_server_big_pick=0;
         last_total_size=0;
         pfile=0;
         checsum_temp=0;
         OTA_DATA_IS_READY=0;
         OTA_DATA_IS_finish=0;
         data_state=DATA_STATE_IDLE;
         tihs_time_SERVER_PICK_SIZE=0;
#endif
#endif
     }



/************************************
åè½æè¿°ï¼å¼æè®¡ç®æ ¡éªå+
*************************************/
 #include <stdint.h>
// è®¡ç® 16 ä½æ ¡éªå
 u16 bak_frash_checksum_XOR(u32 size)
{
    u16 checksum = 0;
    u8 *data;
    data=( u8 *)(OTABAKROM_STARTADDR );
    // å¦ææ°æ®é¿åº¦ä¸ºå¥æ?
    if (size % 2 != 0)
    {
        // å°æåä¸ä¸ªå­ç¬¦è®¾ç½®ä¸ºé«?8 ä½?ä½?8 ä½è®¾ç½®ä¸º 0
        checksum = data[size - 1] << 8;
        size--;
    }

    // æ§è¡ 16 ä½?XOR è¿ç®
    for (u32 i = 0; i < size; i += 2)
    {
        uint16_t word = (data[i] << 8) | data[i + 1];
        checksum ^= word;
    }
     printf("XORSUM=0x%02x\n", checksum);
    return checksum;
}


 /************************************
åè½æè¿°ï¼æ£æµå¤ä»½åºçæ°æ®æ¯å¦æ­£ç¡®ï¼ç¡®è®¤ç¨åºçå®æ´æ§ã?
è¾å
¥åæ°ï¼æ 
è¾åºè¿åï¼æ­£ç¡®è¿å?BOOL_TRUEï¼æ ¡éªä¸éè¿è¿å BOOL_FALSE

*************************************/
boolean_en get_checksum_status_XOR( u16 sum, u32 size)
{

    if(ota_stream_size_in_range(size) == BOOL_TRUE && sum == bak_frash_checksum_XOR(size))
    {
        return BOOL_TRUE;
    }
    else
    {
        return BOOL_FALSE;
    }
}


u32 sum32(u32 dat)
{
    u32 sum   = u32hh(dat);
    sum = sum + u32hl(dat);
    sum = sum + u32lh(dat);
    sum = sum + u32ll(dat);
    return(sum);
}

/************************************
åè½æè¿°ï¼è®¡ç®ç¨åºåç§åçæ ¡éªå?
è¾å
¥åæ°ï¼æ°æ®çå¤§å°ï¼?å­èæ°ã?
è¾åºè¿åï¼æ ¡éªå?
*************************************/
u32 user_frash_checksum(u32 size)
{
    u32 tmp;
    u32 i,sum = 0;
    for(i=0; i<size; i++)
    {
        if(i<ADDR_CHECKSUM_OFFSET/4 || i>=(ADDR_SIZE_OFFSET+4)/4)
        {
            tmp = *((__IO u32 *)OTABAKROM_STARTADDR + i);   //æ ¡éªå¤ä»½å?
            sum += sum32(tmp);
        }
    }
      printf("check_sum=0x%x\n", sum);
    return(sum);
}


/************************************
åè½æè¿°ï¼æ£æµåºç¨åºçæ°æ®æ¯å¦æ­£ç¡®ï¼ç¡®è®¤ç¨åºçå®æ´æ§ã?
è¾å
¥åæ°ï¼æ 
è¾åºè¿åï¼æ­£ç¡®è¿å?BOOL_TRUEï¼æ ¡éªä¸éè¿è¿å BOOL_FALSE

*************************************/
boolean_en get_checksum_status(void)
{
    u32 sum, raw_size, size;
    u16 device_type;

    sum = *((__IO u32 *)OTABAKROM_STARTADDR + ADDR_CHECKSUM_OFFSET/4);
    raw_size = *((__IO u32 *)OTABAKROM_STARTADDR + ADDR_SIZE_OFFSET/4);
    size = raw_size & 0x00FFFFFFU;
    device_type = *((__IO u16 *)(OTABAKROM_STARTADDR + ADDR_TYPE_OFFSET));

    if (sum == (u32)0x12345678 ||
        raw_size == (u32)0x89ABCDEF ||
        size == 0U ||
        ota_stream_size_in_range(size) != BOOL_TRUE ||
        (size % 4U) != 0U)
    {
        OTA_LOGE("checksum invalid header sum=0x%08x raw_size=0x%08x size=%u\r\n",
                 (unsigned int)sum,
                 (unsigned int)raw_size,
                 (unsigned int)size);
        return BOOL_FALSE;
    }

    if (device_type != (u16)OTA_EXPECTED_DEVICE_TYPE)
    {
        OTA_LOGE("checksum invalid device_type got=0x%04x exp=0x%04x\r\n",
                 (unsigned int)device_type,
                 (unsigned int)OTA_EXPECTED_DEVICE_TYPE);
        return BOOL_FALSE;
    }

    if(sum == user_frash_checksum(size/4U))
    {
       return BOOL_TRUE;
    }
    else
    {
        return BOOL_FALSE;
    }
}


/************************************
åè½æè¿°ï¼åºä»¶è½¬ç§»å¤ç?
*************************************/

void  mcu_copy_firmware_machine(void)
{
#if !OTA_USE_QHTTPREADFILE_UFS
    if (MCU_OTA_state != MCU_OTA_STATE_IDLE && MCU_OTA_state != MCU_OTA__COMPLETE)
    {
        OTA_LOGW("module fs copy state ignored while disabled state=%d\r\n", MCU_OTA_state);
        MCU_OTA_state=MCU_OTA_STATE_IDLE;
    }
    return;
#else
#if OTA_DEBUG_DOWNLOAD_ONLY
    if (MCU_OTA_state != MCU_OTA_STATE_IDLE)
    {
        OTA_LOGW("debug download only: force stop mcu copy state=%d\r\n", MCU_OTA_state);
        MCU_OTA_state=MCU_OTA_STATE_IDLE;
    }
#else
    upload_ota_progress_fsm_process();
   switch(MCU_OTA_state)
   {
        case  MCU_OTA_STATE_IDLE:

                break;

        case  MCU_OTA_STATE_RESETING:
               //å¯å¨è½¬ç§»åºä»¶å°MCU

                sprintf(common_send_buff,"+QFLST: \"UFS:%s\"",firm_name_buffer );
                OTA_LOGI("module fs file check file=UFS:%s\r\n", firm_name_buffer);
                send_AT_Command_machine_star("AT+QFLST\r\n",strlen("AT+QFLST\r\n"),common_send_buff,20,1);     // è¦æåæä»¶å¤§å°?    æå
¥åºä»¶åå­ç¬¦ä¸²
           //   send_AT_Command_machine_star("AT+QFLST\r\n",strlen("AT+QFLST\r\n"),"+QFLST: \"UFS:cat1.bin\"",20,1);     // è¦æåæä»¶å¤§å°?          //ä¸è¦å é¤
             // send_AT_Command_machine_star("AT+QFLST\r\n",strlen("AT+QFLST\r\n"),"+QFLST: \"UFS:cat120250401162230.bin\"",20,1);     // è¦æåæä»¶å¤§å°?          //ä¸è¦å é¤
                 printf("_STROE_FINISH_OTA\n "); //+QFLST: "UFS:cat1.bin",24136
                 MCU_OTA_state=MCU_OTA_STATE_GETFILESIZE;
       break;


       case MCU_OTA_STATE_GETFILESIZE:
           if(send_AT_Command_machine_finish()==TRUE)
            {/*
                  if (readLine(stringBuf, &recvLength, 0))
                    {
                        //æåæä»¶ååç»­å¤ç?

                      text=strstr((const char *) stringBuf, "+QFLST: \"UFS:cat1.bin\",");
                          printf("_å­ç¬¦ä½ç½®=%s\n",text);
                       if(text!=NULL){   //æåæä»¶å¤§å°
                          text+=23;
                            printf("______________firmware_size=%s\n",text);
                             while (*text != '\r') {
                           pLength = pLength * 10 + *text - '0';
                             text++;
                            }
                           firmware_size =pLength;
                            printf("______________firmware_size=%d\n",firmware_size);
                           pLength=0;

                       }
                       else
                       {
                           printf("______________firmware_size=æªè¿å
¥\r\n");

                       }

                    }*/
                printf("----MCU_OTA_state=%d,=%d\n",MCU_OTA_state,MCU_OTA_state==MCU_OTA_STATE_QFDWL_GET_FIRMWARE);
                static char common_send_buff_2[64];
                sprintf(common_send_buff,"AT+QFDWL=\"%s\"\r\n",firm_name_buffer );
                sprintf(common_send_buff_2,"AT+QFDWL=\"%s\"",firm_name_buffer );
                OTA_LOGI("module fs download to mcu start file=%s\r\n", firm_name_buffer);
                send_AT_Command_machine_star(common_send_buff,strlen(common_send_buff),common_send_buff_2,255,1); //+QFDWL: 152937,0a31 "CONNECT\r\n"  //???
            //  send_AT_Command_machine_star("AT+QFDWL=\"cat1.bin\"\r\n",strlen("AT+QFDWL=\"cat1.bin\"\r\n"),"AT+QFDWL=\"cat1.bin\"",255,1); //+QFDWL: 152937,0a31 "CONNECT\r\n"  //ä¸è¦å é¤
            //  send_AT_Command_machine_star("AT+QFDWL=\"cat120250401162230.bin\"\r\n",strlen("AT+QFDWL=\"cat120250401162230.bin\"\r\n"),"AT+QFDWL=\"cat120250401162230.bin\"",255,1); //+QFDWL: 152937,0a31 "CONNECT\r\n"  //ä¸è¦å é¤
                     MCU_OTA_state=MCU_OTA_STATE_QFDWL;
                }
                break;

          case  MCU_OTA_STATE_QFDWL :
              if(send_AT_Command_machine_finish()==TRUE)
               {
                    flushQueue(&usartRecvQueue);
                    memset(stringBuf,0x00,recvLength);//ä¸æ¸
ç©ºä¼å¤èç¹æ¶é´è¿åºç¼å²
                    recvLength=0;//
                    printf("----MCU_OTA_state=%d,=%d\n",MCU_OTA_state,MCU_OTA_state==MCU_OTA_STATE_QFDWL_GET_FIRMWARE);
                    OTA_LOGI("module fs transfer stream ready file=%s\r\n", firm_name_buffer);
                    MCU_OTA_state=MCU_OTA_STATE_QFDWL_GET_FIRMWARE;
                    data_state=DATA_STATE_IDLE;//æå¼åºåæ£ç´?
               }
               break;

          case  MCU_OTA_STATE_QFDWL_GET_FIRMWARE :
                {
                       uint8  dat;
                       static u8 *recvURC;
                       static u32 leng_temp=0;
                       static uint8 buf[2];
                       static u16 chec_size=0;
                       //    watchdog_feed_dog();
                       while (dequeue(&usartRecvQueue, &dat))       //+QFDWL: 152937,0a31   //2B 51 46 44 57 4C 3A
                       {
                              //  log_u32(1,  dat);
                                switch(data_state)
                                {


                                      case DATA_STATE_IDLE:
                                           if(dat=='+')//0x2B
                                           {  //  log_u32(1,  dat);


                                             data_state=DATA_STATE_2B;
                                           }
                                           else
                                           {
                                                data_state=DATA_STATE_IDLE;
                                           }
                                           break;
                                      case DATA_STATE_2B:
                                           if(dat=='Q')//0x51
                                           { //log_u32(1,  dat);
                                             data_state=DATA_STATE_51;
                                           }
                                           else
                                           {
                                                data_state=DATA_STATE_IDLE;
                                           }
                                           break;
                                      case DATA_STATE_51:
                                           if(dat=='F')//0x46
                                           {  //   log_u32(1,  dat);
                                            data_state=DATA_STATE_46;
                                           }
                                           else
                                           {
                                            data_state=DATA_STATE_IDLE;
                                           }
                                           break;
                                      case DATA_STATE_46:
                                           if(dat=='D')//0x44
                                           {   log_u32(1,  dat);
                                                data_state=DATA_STATE_44;
                                           }
                                           else
                                           {
                                                data_state=DATA_STATE_IDLE;
                                           }
                                           break;

                                      case DATA_STATE_44:
                                           if(dat=='W')
                                           { log_u32(1,  dat);
                                                data_state=DATA_STATE_57;
                                           }
                                           else
                                           {
                                                data_state=DATA_STATE_IDLE;
                                           }
                                           break;
                                      case DATA_STATE_57:

                                           if(dat=='L')
                                           {
                                                log_u32(1,  dat);
                                                data_state=DATA_STATE_4C;
                                           }
                                           else
                                           {
                                                data_state=DATA_STATE_IDLE;
                                           }
                                           break;

                                      case DATA_STATE_4C:
                                           if(dat==':')
                                           {
                                             //   log_u32(1,  dat);
                                                data_state=DATA_STATE_3A;
                                           }
                                           else
                                           {
                                                data_state=DATA_STATE_IDLE;
                                           }
                                           break;

                                      case DATA_STATE_3A:
                                          if(dat==0x20)//ç©ºæ ¼
                                           {
                                             //   log_u32(1,  dat);
                                                data_state=DATA_STATE_20;
                                           }
                                           else
                                           {
                                                data_state=DATA_STATE_IDLE;
                                           }
                                           break;
                                       case DATA_STATE_20:
                                            stringBuf[recvLength] =dat;
                                             ++recvLength;
                                             if(recvLength>21)  // æ°æ®è¿é¿æ¶èè
                                              {
                                                  data_state=DATA_STATE_FINEISH;
                                                 break;
                                              }

                                           if ((recvLength >= 2) && (stringBuf[recvLength - 2] == '\r') && (stringBuf[recvLength- 1] == '\n'))
                                           {
                                                stringBuf[recvLength ]= 0;
                                                data_state=DATA_STATE_GET_SUM;

                                           }
                                           break;
                                      case DATA_STATE_GET_SUM:
                                           recvURC=stringBuf;
                                           //  watchdog_feed_dog();
                                           while (*recvURC != ',')
                                           {
                                            leng_temp =leng_temp * 10 + *recvURC - '0';
                                             recvURC++;
                                            }
                                            recvURC++;
                                           if( hexStrToByte((const char *)recvURC,4,buf, &chec_size) )   //4å­èå­ç¬¦checsumè½¬æä¸¤å­è?
                                           {
                                              checsum_temp=buf[0]<<8|buf[1];//è·åæ ¡éªå?
                                              printf("recvURC=%d\n",chec_size);
                                              printf("recvURC=%02x\n",buf[0]);
                                              printf("recvURC=%02x\n",buf[1]);
                                           }
                                            //æåé¿åº¦åæ ¡éªå?
                                            tihs_time_SERVER_PICK_SIZE=leng_temp;//æ¬æ¬¡ååºä»¶å¤§å°?
                                            if(leng_temp>0)
                                            {
                                                last_server_big_pick=1;
                                                last_total_size=leng_temp;
                                                firmware_total_size=leng_temp;
                                                SERVER_CHECSUM=checsum_temp;
                                                OTA_LOGI("module fs transfer info size=%u xor=0x%04x\r\n",
                                                         (unsigned int)firmware_total_size,
                                                         SERVER_CHECSUM);
                                                 printf("total_temp=0x%08x\n",SERVER_CHECSUM);
                                                if( get_checksum_status_XOR( SERVER_CHECSUM, firmware_total_size)  )
                                                {
                                                    printf("OTA_OK___2\n");
                                                }
                                            }
                                            else
                                            {
                                             OTA_LOGE("download failed: invalid module fs transfer size=%u\r\n", (unsigned int)leng_temp);

                                            }
                                             leng_temp=0;
                                             memset(stringBuf,0x00,600);//ä¸æ¸
ç©ºä¼éå¤è¯»ç¸åçå
å®¹
                                             recvLength=0;
                                             MCU_OTA_state= MCU_OTA_AT_QFLST;
                                             data_state=DATA_STATE_FINEISH;
                                             break;

                                      case DATA_STATE_FINEISH:
                                           break;
                               }
                        }
              }
              break;
               case MCU_OTA_AT_QFLST:

                  sprintf(common_send_buff,"AT+QFOPEN=\"%s\",2\r\n",firm_name_buffer );
                  OTA_LOGI("open module fs file file=%s\r\n", firm_name_buffer);
                  send_AT_Command_machine_star(common_send_buff,strlen(common_send_buff),"+QFOPEN:",20,1);   //è·åæä»¶æé+QFOPEN: 1
             // send_AT_Command_machine_star("AT+QFOPEN=\"cat1.bin\",2\r\n",strlen("AT+QFOPEN=\"cat1.bin\",2\r\n"),"+QFOPEN:",20,1);   //è·åæä»¶æé+QFOPEN: 1
            //   send_AT_Command_machine_star("AT+QFOPEN=\"cat120250401162230.bin\",2\r\n",strlen("AT+QFOPEN=\"cat120250401162230.bin\",2\r\n"),"+QFOPEN:",20,1);   //è·åæä»¶æé+QFOPEN: 1
                  pfile=0;//åºä»¶å­èåå§ä½ç½®
                  MCU_OTA_state=MCU_OTA_AT_QFOPEN;
              break;

        case MCU_OTA_AT_QFOPEN:
             if(send_AT_Command_machine_finish()==TRUE )
             {
                 //åç
                 static   char common_temp[32] ="AT+QFSEEK=1,%u,0\r\n";

                 sprintf(common_send_buff,common_temp,pfile );
                 send_AT_Command_machine_star(common_send_buff,strlen(common_send_buff),"OK",20,1);   //è®¾ç½®æä»¶æéä¸ºæä»¶çåå§ä½ç½®ã?
                 MCU_OTA_state=MCU_OTA_AT_QFSEEK;
             }
            break;

       case MCU_OTA_AT_QFSEEK:
             if(send_AT_Command_machine_finish()==TRUE)
             {
               send_AT_Command_machine_star("AT+QFREAD=1,512\r\n",strlen("AT+QFREAD=1,512\r\n"),"CONNECT ",20,1);  //è¯»åæ°æ®ã?CONNECT 512\r\n"
               wait_data_timer =Timer_GetTickCount();
               MCU_OTA_state=MCU_OTA_AT_QFREAD;
             }
             break;
        case MCU_OTA_AT_QFREAD:                        //è¯»åæ°æ®ã?
             if(send_AT_Command_machine_finish()==TRUE)
             {
                      if(have_get_pack_length==0)
                      {
                          if( strstr((const char *) stringBuf, "CONNECT"))
                           {
                                static  u16 pack_length_temp=0;
                                pack_buf=stringBuf;   //stringBufå
å®¹ï¼AT+QFREAD=1,512\r\nCONNECT 512\r\n
                             // printf("CONNECTpack_buf=%s\n",pack_buf);
                                pack_buf+=26;
                                printf("CONNECTpack_buf=%s\n",pack_buf);
                                while (*pack_buf !='\r' )//è¿éä»¥\rå¤æ­ç»å°¾ææ¶ä¼æ°æ®è¿å¤§ï¼æ¯å¦512ä¼åæ?1255ï¼åªå¥½ç¨pack_length_temp>PICK_SIZEæ¥æªå?
                               {
                                    pack_length_temp =pack_length_temp * 10 + *pack_buf - '0';
                                    pack_buf++;
                                 if(pack_length_temp>PICK_SIZE )
                                  {
                                      printf("__pack length too large=%d\r\n", pack_length_temp);
                                      pack_length_temp=0;
                                      break;
                                  }
                               }
                               pack_length= pack_length_temp;
                               pack_length_temp=0;
                               have_get_pack_length=1;
                               printf("__è·åå°CONNECT=%d\r\n",pack_length);
                          }
                          else
                          {
                              printf("__æªè·åå°CONNECT_\n");
                          }
                       }

                 if (Timer_PassedDelay(wait_data_timer, 400))    //ç­?400MS æ°æ®æ¥æ¶å®?,å¤ªå°æ¥æ¶ä¸å®æ´æéè¯¯
                 {
                     MCU_OTA_state=MCU_OTA_MCU_GETDATA;
                     have_get_pack_length=0;
                 if (Timer_PassedDelay(wait_data_timer, 300))
                 {      //æ æ°æ®è¶
æ?             *************å¾
å¤çæ­»å¾ªç¯*********
                   //  MCU_OTA_state=MCU_OTA_AT_QFREAD_LOOP;
                 }
             }
             }
             break;
        case MCU_OTA_MCU_GETDATA:        //ç±æ¥æ¶å¤çåæ?
             if(OTA_DATA_IS_READY)       //æ¶ä¸å°éåå¾
å?
             {
                OTA_DATA_IS_READY=0;
                printf("3MCUå­å¨___________________\n");
                MCU_OTA_state=MCU_OTA_AT_QFREAD_LOOP;
               printf("tihs_time_SERVER_PICK_SIZE=%d\n",tihs_time_SERVER_PICK_SIZE);
             }
             else if (OTA_DATA_IS_finish)
             {
                  OTA_DATA_IS_finish=0;
                  MCU_OTA_state=MCU_OTA_AT_QFREAD_LOOP;
             }
             else if (Timer_PassedDelay(wait_data_timer, 5000))
             {
                  OTA_LOGE("download failed: no firmware data from module fs file=%s\r\n", firm_name_buffer);
                  printf("MCU_OTA_MCU_GETDATA timeout, no firmware data received\n");
                  sys_data.sn = 3;
                  sys_data_store();
                  changea_to_MQTT_modle();
                  last_server_big_pick = 0;
                  MCU_OTA_state = MCU_OTA__COMPLETE;
             }
            break;

       case MCU_OTA_AT_QFREAD_LOOP:
            if(pfile+PICK_SIZE<tihs_time_SERVER_PICK_SIZE)//å°äºæ¬æ¬¡åçæ?   //  åççº? pfile UFS æä»¶ä½ç½®
            {
                  pfile+=PICK_SIZE;
                  static   char common_temp[32] ="AT+QFSEEK=1,%u,0\r\n";
                //  static   char common_send_buff[64];
                  sprintf(common_send_buff,common_temp,pfile );
                  send_AT_Command_machine_star(common_send_buff,strlen(common_send_buff),"OK",20,1);   //è®¾ç½®æä»¶æéä¸ºæä»¶çåå§ä½ç½®ã?
                  MCU_OTA_state=MCU_OTA_AT_QFSEEK;
            }
            else
            {
                printf("pfile=%u\n",pfile);
                send_AT_Command_machine_star("AT+QFCLOSE=1\r\n",strlen("AT+QFCLOSE=1\r\n"),"OK",20,1);
                MCU_OTA_state=MCU_OTA_GET_REPORT_PROGRESS;
            }
            break;

       case  MCU_OTA_GET_REPORT_PROGRESS:
            if(send_AT_Command_machine_finish()==TRUE)
            {
                  ota_progress_report_statr();//å¼å¯ä¸æ¥ï¼åè¿è¡ä¸ä¸æ­?
                  MCU_OTA_state=MCU_OTA_GET_BIG_PICK; //
            }
            break;

       case MCU_OTA_GET_BIG_PICK:
            if(ota_progress_report_is_finish())
            {
                 if( last_server_big_pick)//æå¡å¨åºä»¶æåä¸å¸?ä»ä¹æ¶åæ¸
é?
                 {
                      OTA_LOGI("move firmware to flash complete bytes=%u\r\n", (unsigned int)save_byete_counter);
                      server_big_pick_counter=0;
                      save_byete_counter=0;
                      pfile=0;//å°çæéä½ç½®å½é¶
                      MCU_OTA_state=MCU_OTA_AT_QFCLOSE;
                  }
                  else  //
                  {
                      ++server_big_pick_counter;
                        pfile=0; //å°çæéä½ç½®å½é¶
                        ota_connect_state=CONNECT_OTA_AT_QFDEL;   //å»å é¤æ¨¡ååºä»?
                        MCU_OTA_state=MCU_OTA_STATE_IDLE;         //ç­å¾
æ°çæå¡å¨åºä»¶åçå¯å?
                  }
             }
             break;

          case MCU_OTA_AT_QFCLOSE :     //AT+QFCLOSE=1
              OTA_LOGI("verify firmware start size=%u xor=0x%04x\r\n",
                       (unsigned int)firmware_total_size,
                       SERVER_CHECSUM);
              if( get_checksum_status_XOR( SERVER_CHECSUM, firmware_total_size)  )//ä¼ è¾å±æ ¡éª?                                     ---------- ä¼ è¾éè¯¯æ²¡æåç°-------------
              {
                     printf("---------------OTA_XOR_CHECK_OK\n");
                     if( get_checksum_status())  //  åºç¨å±åºä»¶å®æ´æ§ç¶æè¯»å?
                     {
                           OTA_LOGI("verify firmware complete result=ok size=%u\r\n", (unsigned int)firmware_total_size);
                           printf("---------------OTA_SUM_CHECK_OK\n");
                           sys_data.sn=0xaa5555aa;//æ è®°ææ°åºä»¶
                           sys_data_store();
                           MCU_OTA_state=MCU_OTA_MCU_FINISH;
                     }
                     else
                     {
                        OTA_LOGE("verify firmware failed: app checksum size=%u\r\n", (unsigned int)firmware_total_size);
                        printf("---------------OTA_SUM_CHECK_ERROR\n");
                        sys_data.sn=3;//æ è®°åºä»¶ä¸è½½éè¯¯
                        sys_data_store();
                        changea_to_MQTT_modle();//åå°MQTT
                        last_server_big_pick=0;//è¿ä¸ªæ¶åæ¸
é?
                        MCU_OTA_state=MCU_OTA__COMPLETE;
                     }
              }
              else //æ ¡éªéè¯¯
              {
                    OTA_LOGE("verify firmware failed: transport xor expected=0x%04x size=%u\r\n",
                             SERVER_CHECSUM,
                             (unsigned int)firmware_total_size);
                    printf("------------OTA_XOR_CHECK_ERR\n");
                    sys_data.sn=3;//æ è®°åºä»¶ä¸è½½éè¯¯
                    sys_data_store();
                    sbuff= ((u8*)(DATAROM_STARTADDR)) ;//æå°
                    printf_buf(sbuff,64);
                    sbuff= ((u8*)(BAKDATAROM_STARTADDR)) ;//æå°
                    printf_buf(sbuff,64);
                    changea_to_MQTT_modle();//åå°MQTT
                    last_server_big_pick=0;//è¿ä¸ªæ¶åæ¸
é?
                    MCU_OTA_state=MCU_OTA__COMPLETE;
              }
               break;

          case   MCU_OTA_MCU_FINISH:
                    OTA_LOGI("upgrade start: mark new firmware and jump to boot\r\n");
                    printf("---------OTA å®æ¯------\n");
                    printf("---------ç³»ç»éå¯-------\n");
                    last_server_big_pick=0;//æ¸
é¶æ å¿ï¼é²æ­¢è·³è½¬å¤±è´¥åå½±åä¸æ¬¡OTA
                    server_big_pick_counter=0;
                    save_byete_counter=0;
                    firmware_total_size=0;
                    SERVER_CHECSUM=0;
                    sbuff= ((u8*)(DATAROM_STARTADDR)) ;
                    printf_buf(sbuff,64);
                    sbuff= ((u8*)(DATAROM_STARTADDR)) ;
                    printf_buf(sbuff,64);
                    HAL_Delay(50);
                    //è°å°bootå?
                    extern void iap_jump2boot(void);  //-------------------å¦ææ²¡ææå°æ°æ®å»¶æ¶çè¯ä¼å½±åBOOTçè·³è½?
                    MCU_OTA_state=MCU_OTA__COMPLETE;//è¿ä¸ªå°æ¹å¾éè¦ï¼ä¸å®è¦æ¾å¨iap_jump2boot()å½æ°ä¹åï¼å¦åè·³è½¬å¤±è´?
                    iap_jump2boot();
      break;

      case MCU_OTA__COMPLETE:
      break;

      default:
      break;
    }
#endif
#endif
}

/**
*@brief   å­å¨OTAæ¨¡åä¸æ¥çåºä»¶æµ
*@param	  bufï¼æ¨¡åä¸æ¥çåºä»¶
*@param	  lenthï¼åºä»¶é¿åº?
*@return  æ?
*/
 void OTA_STROE_MCU(uint8 *buf,u16 lenth)
{                //è§£æç»æ
#if !OTA_USE_QHTTPREADFILE_UFS
     OTA_LOGW("module fs store disabled: ignore len=%u\r\n", (unsigned int)lenth);
     (void)buf;
#else
#if OTA_DEBUG_DOWNLOAD_ONLY
     OTA_LOGW("debug download only: skip OTA_STROE_MCU len=%u\r\n", (unsigned int)lenth);
     (void)buf;
#else
     uint16 dataLength;
      dataLength=lenth;
     if(dataLength==PICK_SIZE|| (last_server_big_pick && dataLength==(u16)(last_total_size%PICK_SIZE)))
      {  //æ¦æª"OK\r\n"
            // printf("_________________________________save_byete_counterbingin=%u\n",save_byete_counter);
             flash_store(buf, dataLength, OTABAKROM_STARTADDR+save_byete_counter );  //-strlen("CONNECT 512\r\n")
             save_byete_counter+=dataLength;//ç´¯è®¡å­èï¼å°åå?
              printf("_________________________________save_byete_counter=%u\n",save_byete_counter);
             MCU_OTA_state=MCU_OTA_AT_QFREAD_LOOP;
             sbuff= ((u8*)(OTABAKROM_STARTADDR+save_byete_counter)) ;
             // printf_buf(sbuff-256,64);
             printf("1MCUå­å¨1\n");
             OTA_DATA_IS_READY=1;  //æåpayloadæå
             if(dataLength==(u16)(firmware_total_size%PICK_SIZE) &&last_server_big_pick)  //å¦ææåä¸çä¸º0ï¼ä¼ä¸ä¼åºBUGï¼?
             {
                OTA_DATA_IS_finish=1;
              }
         }
#endif
#endif
}
