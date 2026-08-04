/*************************************************************
程序功能：量产校准安全门禁与不可用服务框架
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "sys_calibration_service.h"
#include <string.h>

static sys_calibration_service_status_st _service_status;

static void sys_calibration_service_set_result(sys_calibration_result_en result)
{
    _service_status.last_result = result;
    ++_service_status.result_seq;
}

static boolean_en sys_calibration_service_status_copy(
    sys_calibration_service_status_st *status)
{
    if (status == NULL)
    {
        return BOOL_FALSE;
    }
    memcpy(status, &_service_status, sizeof(*status));
    return BOOL_TRUE;
}

static sys_calibration_result_en sys_calibration_service_unavailable(
    sys_calibration_service_status_st *status)
{
    sys_calibration_service_set_result(SYS_CALIBRATION_RESULT_NOT_AVAILABLE);
    (void)sys_calibration_service_status_copy(status);
    return SYS_CALIBRATION_RESULT_NOT_AVAILABLE;
}

/************************************
功能描述：初始化校准服务为安全不可用状态
输入参数：无
输出返回：无
************************************/
void sys_calibration_service_init(void)
{
    memset(&_service_status, 0, sizeof(_service_status));
    _service_status.state = SYS_CALIBRATION_STATE_DISABLED;
    _service_status.last_result = SYS_CALIBRATION_RESULT_NOT_AVAILABLE;
    _service_status.codec_available = SYS_CALIBRATION_CODEC_AVAILABLE ? BOOL_TRUE : BOOL_FALSE;
    _service_status.commit_available = SYS_CALIBRATION_FLASH_COMMIT_ENABLED ? BOOL_TRUE : BOOL_FALSE;
    _service_status.nonzero_output_allowed = SYS_CALIBRATION_NONZERO_OUTPUT_ENABLED ? BOOL_TRUE : BOOL_FALSE;
}

/************************************
功能描述：读取校准服务状态和能力门禁
输入参数：status 输出状态
输出返回：读取成功 BOOL_TRUE，否则 BOOL_FALSE
************************************/
boolean_en sys_calibration_service_get_status(sys_calibration_service_status_st *status)
{
    return sys_calibration_service_status_copy(status);
}

/************************************
功能描述：尝试开始校准租约
输入参数：session_id 会话标识；now_ms 当前毫秒节拍；lease_ms 租约时长；status 输出状态
输出返回：本分支固定返回 NOT_AVAILABLE
************************************/
sys_calibration_result_en sys_calibration_service_begin(
    u32 session_id,
    u32 now_ms,
    u32 lease_ms,
    sys_calibration_service_status_st *status)
{
    (void)session_id;
    (void)now_ms;
    (void)lease_ms;
    return sys_calibration_service_unavailable(status);
}

/************************************
功能描述：尝试临时设置校准点
输入参数：session_id 会话标识；now_ms 当前毫秒节拍；level 协议等级；status 输出状态
输出返回：本分支固定返回 NOT_AVAILABLE
************************************/
sys_calibration_result_en sys_calibration_service_set_point(
    u32 session_id,
    u32 now_ms,
    u16 level,
    sys_calibration_service_status_st *status)
{
    (void)session_id;
    (void)now_ms;
    (void)level;
    return sys_calibration_service_unavailable(status);
}

/************************************
功能描述：尝试暂存不透明配置载荷
输入参数：session_id 会话标识；payload/length 外部载荷；status 输出状态
输出返回：本分支固定返回 NOT_AVAILABLE
************************************/
sys_calibration_result_en sys_calibration_service_stage_config(
    u32 session_id,
    const u8 *payload,
    u32 length,
    sys_calibration_service_status_st *status)
{
    (void)session_id;
    (void)payload;
    (void)length;
    return sys_calibration_service_unavailable(status);
}

/************************************
功能描述：尝试提交校准配置
输入参数：session_id 会话标识；status 输出状态
输出返回：本分支固定返回 NOT_AVAILABLE
************************************/
sys_calibration_result_en sys_calibration_service_commit(
    u32 session_id,
    sys_calibration_service_status_st *status)
{
    (void)session_id;
    return sys_calibration_service_unavailable(status);
}

/************************************
功能描述：让校准服务进入安全中止状态
输入参数：session_id 会话标识；status 输出状态
输出返回：中止状态更新成功 BOOL_TRUE，否则 BOOL_FALSE
************************************/
boolean_en sys_calibration_service_abort(
    u32 session_id,
    sys_calibration_service_status_st *status)
{
    if ((_service_status.state != SYS_CALIBRATION_STATE_ACTIVE) &&
        (_service_status.state != SYS_CALIBRATION_STATE_DISABLED))
    {
        sys_calibration_service_set_result(SYS_CALIBRATION_RESULT_INVALID_STATE);
        return sys_calibration_service_status_copy(status);
    }
    if ((_service_status.state == SYS_CALIBRATION_STATE_ACTIVE) &&
        (_service_status.session_id != session_id))
    {
        sys_calibration_service_set_result(SYS_CALIBRATION_RESULT_INVALID_ARGUMENT);
        return sys_calibration_service_status_copy(status);
    }
    _service_status.session_id = session_id;
    _service_status.state = SYS_CALIBRATION_STATE_ABORTED;
    sys_calibration_service_set_result(SYS_CALIBRATION_RESULT_OK);
    return sys_calibration_service_status_copy(status);
}

/************************************
功能描述：检查租约状态，不执行任何输出动作
输入参数：now_ms 当前毫秒节拍；status 输出状态
输出返回：状态读取成功 BOOL_TRUE，否则 BOOL_FALSE
************************************/
boolean_en sys_calibration_service_timer(
    u32 now_ms,
    sys_calibration_service_status_st *status)
{
    (void)now_ms;
    return sys_calibration_service_status_copy(status);
}
