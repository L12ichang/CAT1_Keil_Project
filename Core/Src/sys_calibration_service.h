#ifndef SYS_CALIBRATION_SERVICE_H
#define SYS_CALIBRATION_SERVICE_H

#include "type.h"

/* These gates stay zero until the external contract and safety evidence exist. */
#define SYS_CALIBRATION_PROTOCOL_FROZEN        0U
#define SYS_CALIBRATION_CODEC_AVAILABLE        0U
#define SYS_CALIBRATION_FLASH_COMMIT_ENABLED   0U
#define SYS_CALIBRATION_NONZERO_OUTPUT_ENABLED 0U

typedef enum
{
    SYS_CALIBRATION_STATE_DISABLED = 0,
    SYS_CALIBRATION_STATE_IDLE,
    SYS_CALIBRATION_STATE_ACTIVE,
    SYS_CALIBRATION_STATE_FAULT,
    SYS_CALIBRATION_STATE_ABORTED
} sys_calibration_state_en;

typedef enum
{
    SYS_CALIBRATION_RESULT_OK = 0,
    SYS_CALIBRATION_RESULT_NOT_AVAILABLE,
    SYS_CALIBRATION_RESULT_INVALID_STATE,
    SYS_CALIBRATION_RESULT_INVALID_ARGUMENT,
    SYS_CALIBRATION_RESULT_LEASE_EXPIRED
} sys_calibration_result_en;

typedef struct
{
    sys_calibration_state_en state;
    sys_calibration_result_en last_result;
    u32 session_id;
    u32 lease_deadline_ms;
    u32 result_seq;
    boolean_en codec_available;
    boolean_en commit_available;
    boolean_en nonzero_output_allowed;
} sys_calibration_service_status_st;

/************************************
功能描述：初始化校准服务为安全不可用状态
输入参数：无
输出返回：无
************************************/
extern void sys_calibration_service_init(void);

/************************************
功能描述：读取校准服务状态和能力门禁
输入参数：status 输出状态
输出返回：读取成功 BOOL_TRUE，否则 BOOL_FALSE
************************************/
extern boolean_en sys_calibration_service_get_status(
    sys_calibration_service_status_st *status);

/************************************
功能描述：尝试开始校准租约
输入参数：session_id 会话标识；now_ms 当前毫秒节拍；lease_ms 租约时长；status 输出状态
输出返回：本分支固定返回 NOT_AVAILABLE，非零输出不会开放
************************************/
extern sys_calibration_result_en sys_calibration_service_begin(
    u32 session_id,
    u32 now_ms,
    u32 lease_ms,
    sys_calibration_service_status_st *status);

/************************************
功能描述：尝试临时设置校准点
输入参数：session_id 会话标识；now_ms 当前毫秒节拍；level 协议等级；status 输出状态
输出返回：本分支固定返回 NOT_AVAILABLE，不执行 PWM 写入
************************************/
extern sys_calibration_result_en sys_calibration_service_set_point(
    u32 session_id,
    u32 now_ms,
    u16 level,
    sys_calibration_service_status_st *status);

/************************************
功能描述：尝试暂存不透明配置载荷
输入参数：session_id 会话标识；payload/length 外部载荷；status 输出状态
输出返回：本分支固定返回 NOT_AVAILABLE，不解析、不持久化载荷
************************************/
extern sys_calibration_result_en sys_calibration_service_stage_config(
    u32 session_id,
    const u8 *payload,
    u32 length,
    sys_calibration_service_status_st *status);

/************************************
功能描述：尝试提交校准配置
输入参数：session_id 会话标识；status 输出状态
输出返回：本分支固定返回 NOT_AVAILABLE，不擦写 Flash
************************************/
extern sys_calibration_result_en sys_calibration_service_commit(
    u32 session_id,
    sys_calibration_service_status_st *status);

/************************************
功能描述：让校准服务进入安全中止状态
输入参数：session_id 会话标识；status 输出状态
输出返回：中止状态更新成功 BOOL_TRUE，否则 BOOL_FALSE
注意：该框架不直接操作 PWM；真实 ABORT 必须由集成后的最高优先级安全关断路径执行。
************************************/
extern boolean_en sys_calibration_service_abort(
    u32 session_id,
    sys_calibration_service_status_st *status);

/************************************
功能描述：检查租约状态，不执行任何输出动作
输入参数：now_ms 当前毫秒节拍；status 输出状态
输出返回：状态读取成功 BOOL_TRUE，否则 BOOL_FALSE
************************************/
extern boolean_en sys_calibration_service_timer(
    u32 now_ms,
    sys_calibration_service_status_st *status);

#endif
