#ifndef CURRENT_CAL_STORAGE_H
#define CURRENT_CAL_STORAGE_H

#include "current_cal_curve.h"

typedef enum
{
    CURRENT_CAL_SLOT_NONE = 0,
    CURRENT_CAL_SLOT_A = 1,
    CURRENT_CAL_SLOT_B = 2
} current_cal_slot_en;

void current_cal_storage_init(void);
boolean_en current_cal_storage_has_active_curve(void);
const current_cal_curve_t *current_cal_storage_active_curve(void);
u32 current_cal_storage_sequence(void);
current_cal_slot_en current_cal_storage_active_slot(void);
boolean_en current_cal_storage_commit(const current_cal_curve_t *curve);
boolean_en current_cal_storage_invalidate(void);

#endif
