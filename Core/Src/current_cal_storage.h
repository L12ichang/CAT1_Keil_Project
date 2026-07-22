#ifndef CURRENT_CAL_STORAGE_H
#define CURRENT_CAL_STORAGE_H

#include "current_cal_curve.h"

typedef enum
{
    CURRENT_CAL_SLOT_NONE = 0,
    CURRENT_CAL_SLOT_A = 1,
    CURRENT_CAL_SLOT_B = 2
} current_cal_slot_en;

typedef enum
{
    CURRENT_CAL_SECTION_EMPTY = 0,
    CURRENT_CAL_SECTION_VALID = 1,
    CURRENT_CAL_SECTION_TOMBSTONE = 2
} current_cal_section_state_en;

#define CURRENT_CAL_METER_PAYLOAD_SIZE 128U

typedef struct
{
    current_cal_section_state_en state;
    u32 context_crc;
    u32 data_crc;
    u16 section_version;
    u16 data_length;
    u8 payload[CURRENT_CAL_METER_PAYLOAD_SIZE];
} current_cal_meter_section_t;

void current_cal_storage_init(void);
boolean_en current_cal_storage_has_active_curve(void);
boolean_en current_cal_storage_active_curve_is_legacy(void);
const current_cal_curve_t *current_cal_storage_active_curve(void);
u32 current_cal_storage_sequence(void);
current_cal_slot_en current_cal_storage_active_slot(void);
boolean_en current_cal_storage_commit(const current_cal_curve_t *curve);
boolean_en current_cal_storage_ensure_v2(void);
boolean_en current_cal_storage_invalidate(void);
boolean_en current_cal_storage_get_meter_section(current_cal_meter_section_t *section);
boolean_en current_cal_storage_commit_meter_section(const current_cal_meter_section_t *section);
/* Before another record erases either shared parameter page, make the latest
 * complete calibration envelope byte-identical and valid in both slots. */
boolean_en current_cal_storage_prepare_shared_page_update(void);

#endif
