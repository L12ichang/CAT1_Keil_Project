#ifndef NB_AT_LEGACY_ADAPTER_H
#define NB_AT_LEGACY_ADAPTER_H

#include "common.h"
#include "sys_at_engine.h"

typedef struct
{
    u32 routed_byte_count;
    u32 dropped_byte_count;
    u32 response_line_drop_count;
    u16 queue_high_watermark;
} nb_at_legacy_adapter_stats_st;

extern void nb_at_legacy_adapter_init(void);
extern void nb_at_legacy_adapter_set_urc_enabled(boolean_en enabled);
extern boolean_en nb_at_legacy_adapter_start(
    const char *command,
    u16 command_length,
    const char *expected_token,
    u32 timeout_ms,
    u8 retry_max,
    u16 owner_id,
    boolean_en expect_prompt);
extern boolean_en nb_at_legacy_adapter_read_response_line(
    u8 *buf,
    u16 capacity,
    u16 *length);
extern boolean_en nb_at_legacy_adapter_take_result(sys_at_result_en *result);
extern boolean_en nb_at_legacy_adapter_busy(void);
extern void nb_at_legacy_adapter_cancel(u16 owner_id);

extern boolean_en nb_at_legacy_adapter_read_byte(u8 *byte);
extern u16 nb_at_legacy_adapter_read_line(
    u8 *buf,
    u16 *length,
    u16 capacity);
extern void nb_at_legacy_adapter_route_raw_byte(u8 byte);
extern u8 nb_at_legacy_adapter_send_raw(const u8 *buf, u16 length);

extern boolean_en nb_at_legacy_adapter_begin_exclusive(
    u16 owner_id,
    u32 lease_ms);
extern boolean_en nb_at_legacy_adapter_enter_raw_mode(void);
extern boolean_en nb_at_legacy_adapter_arm_raw_mode(
    const char *trigger_token);
extern boolean_en nb_at_legacy_adapter_leave_raw_mode(void);
extern void nb_at_legacy_adapter_end_exclusive(void);
extern boolean_en nb_at_legacy_adapter_has_exclusive(void);
extern u16 nb_at_legacy_adapter_exclusive_generation(void);
extern void nb_at_legacy_adapter_get_stats(
    nb_at_legacy_adapter_stats_st *stats);

#endif
