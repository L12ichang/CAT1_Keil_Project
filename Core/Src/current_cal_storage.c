#include "current_cal_storage.h"
#include "flash_address_assignment.h"
#include "hw_flash.h"
#include <stddef.h>

#define CURRENT_CAL_FLASH_MAGIC          0x43414c31UL
#define CURRENT_CAL_FLASH_FORMAT_VERSION 1U
#define CURRENT_CAL_FLASH_TYPE_CURVE     1UL
#define CURRENT_CAL_FLASH_TYPE_TOMBSTONE 2UL
#define CURRENT_CAL_FLASH_VALID_MARKER   0x56414c43UL

typedef struct
{
    u32 magic;
    u16 format_version;
    u16 record_size;
    u32 sequence;
    u32 record_type;
    u32 profile_crc;
    u32 curve_crc;
    u16 curve_version;
    u16 point_count;
    u16 logical_pwm[CURRENT_CAL_POINT_COUNT];
    u16 reserved;
    u32 record_crc;
    u32 valid_marker;
} current_cal_flash_record_t;

CURRENT_CAL_STATIC_ASSERT(sizeof(current_cal_flash_record_t) == 80U);
CURRENT_CAL_STATIC_ASSERT(sizeof(current_cal_flash_record_t) <= CURRENT_CAL_FLASH_SLOT_RESERVED);
CURRENT_CAL_STATIC_ASSERT((offsetof(current_cal_flash_record_t, valid_marker) & 3U) == 0U);

static current_cal_curve_t current_cal_active_curve;
static boolean_en current_cal_active_valid = BOOL_FALSE;
static u32 current_cal_active_sequence = 0U;
static current_cal_slot_en current_cal_active_slot_id = CURRENT_CAL_SLOT_NONE;

static u32 current_cal_storage_record_crc(const current_cal_flash_record_t *record)
{
    return current_cal_crc32((const u8 *)record,
                             (u32)offsetof(current_cal_flash_record_t, record_crc));
}

static boolean_en current_cal_storage_record_valid(const current_cal_flash_record_t *record)
{
    current_cal_curve_t curve;

    if (record == NULL ||
        record->magic != CURRENT_CAL_FLASH_MAGIC ||
        record->format_version != CURRENT_CAL_FLASH_FORMAT_VERSION ||
        record->record_size != sizeof(*record) ||
        record->valid_marker != CURRENT_CAL_FLASH_VALID_MARKER ||
        record->record_crc != current_cal_storage_record_crc(record))
    {
        return BOOL_FALSE;
    }

    if (record->record_type == CURRENT_CAL_FLASH_TYPE_TOMBSTONE)
    {
        return BOOL_TRUE;
    }
    if (record->record_type != CURRENT_CAL_FLASH_TYPE_CURVE)
    {
        return BOOL_FALSE;
    }

    memset(&curve, 0, sizeof(curve));
    curve.curve_version = record->curve_version;
    curve.point_count = record->point_count;
    memcpy(curve.logical_pwm, record->logical_pwm, sizeof(curve.logical_pwm));
    curve.profile_crc = record->profile_crc;
    curve.curve_crc = record->curve_crc;
    return (current_cal_curve_validate(&curve, record->profile_crc) == CURRENT_CAL_CURVE_OK) ?
           BOOL_TRUE : BOOL_FALSE;
}

static boolean_en current_cal_storage_read(u32 address, current_cal_flash_record_t *record)
{
    if (record == NULL)
    {
        return BOOL_FALSE;
    }
    hw_flash_read_bytes(address, (u8 *)record, sizeof(*record));
    return current_cal_storage_record_valid(record);
}

static boolean_en current_cal_sequence_newer(u32 lhs, u32 rhs)
{
    return (((s32)(lhs - rhs)) > 0) ? BOOL_TRUE : BOOL_FALSE;
}

static void current_cal_storage_activate(const current_cal_flash_record_t *record,
                                         current_cal_slot_en slot)
{
    current_cal_active_sequence = record->sequence;
    current_cal_active_slot_id = slot;
    current_cal_active_valid = BOOL_FALSE;
    memset(&current_cal_active_curve, 0, sizeof(current_cal_active_curve));

    if (record->record_type != CURRENT_CAL_FLASH_TYPE_CURVE)
    {
        return;
    }
    current_cal_active_curve.curve_version = record->curve_version;
    current_cal_active_curve.point_count = record->point_count;
    memcpy(current_cal_active_curve.logical_pwm,
           record->logical_pwm,
           sizeof(current_cal_active_curve.logical_pwm));
    current_cal_active_curve.profile_crc = record->profile_crc;
    current_cal_active_curve.curve_crc = record->curve_crc;
    if (current_cal_curve_validate(&current_cal_active_curve,
                                   current_cal_profile_crc()) == CURRENT_CAL_CURVE_OK)
    {
        current_cal_active_valid = BOOL_TRUE;
    }
}

static boolean_en current_cal_storage_write_record(const current_cal_flash_record_t *record,
                                                   current_cal_slot_en slot)
{
    current_cal_flash_record_t staged_record;
    current_cal_flash_record_t readback;
    u32 address;
    u32 marker_address;
    HAL_StatusTypeDef status;

    address = (slot == CURRENT_CAL_SLOT_A) ?
              CURRENT_CAL_FLASH_SLOT_A_ADDR : CURRENT_CAL_FLASH_SLOT_B_ADDR;
    memcpy(&staged_record, record, sizeof(staged_record));
    staged_record.valid_marker = 0xffffffffUL;
    status = hw_flash_update_bytes_checked(address,
                                           (const u8 *)&staged_record,
                                           sizeof(staged_record));
    if (status != HAL_OK)
    {
        return BOOL_FALSE;
    }
    hw_flash_read_bytes(address, (u8 *)&readback, sizeof(readback));
    if (memcmp(&readback, &staged_record, sizeof(readback)) != 0 ||
        readback.record_crc != current_cal_storage_record_crc(&readback))
    {
        return BOOL_FALSE;
    }

    marker_address = address + (u32)offsetof(current_cal_flash_record_t, valid_marker);
    status = hw_flash_program_bytes_checked(marker_address,
                                            (const u8 *)&record->valid_marker,
                                            sizeof(record->valid_marker));
    if (status != HAL_OK)
    {
        return BOOL_FALSE;
    }
    if (current_cal_storage_read(address, &readback) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    return (memcmp(&readback, record, sizeof(readback)) == 0) ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en current_cal_storage_store(u32 record_type,
                                            const current_cal_curve_t *curve)
{
    current_cal_flash_record_t record;
    current_cal_slot_en target_slot;
    u32 next_sequence;

    next_sequence = current_cal_active_sequence + 1U;
    if (next_sequence == 0U)
    {
        next_sequence = 1U;
    }
    target_slot = (current_cal_active_slot_id == CURRENT_CAL_SLOT_A) ?
                  CURRENT_CAL_SLOT_B : CURRENT_CAL_SLOT_A;

    memset(&record, 0, sizeof(record));
    record.magic = CURRENT_CAL_FLASH_MAGIC;
    record.format_version = CURRENT_CAL_FLASH_FORMAT_VERSION;
    record.record_size = (u16)sizeof(record);
    record.sequence = next_sequence;
    record.record_type = record_type;
    record.profile_crc = current_cal_profile_crc();
    if (record_type == CURRENT_CAL_FLASH_TYPE_CURVE && curve != NULL)
    {
        record.profile_crc = curve->profile_crc;
        record.curve_crc = curve->curve_crc;
        record.curve_version = curve->curve_version;
        record.point_count = curve->point_count;
        memcpy(record.logical_pwm, curve->logical_pwm, sizeof(record.logical_pwm));
    }
    record.record_crc = current_cal_storage_record_crc(&record);
    record.valid_marker = CURRENT_CAL_FLASH_VALID_MARKER;

    if (current_cal_storage_write_record(&record, target_slot) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    current_cal_storage_activate(&record, target_slot);
    return BOOL_TRUE;
}

void current_cal_storage_init(void)
{
    current_cal_flash_record_t record_a;
    current_cal_flash_record_t record_b;
    const current_cal_flash_record_t *selected;
    current_cal_slot_en selected_slot;
    boolean_en valid_a;
    boolean_en valid_b;

    valid_a = current_cal_storage_read(CURRENT_CAL_FLASH_SLOT_A_ADDR, &record_a);
    valid_b = current_cal_storage_read(CURRENT_CAL_FLASH_SLOT_B_ADDR, &record_b);
    selected = NULL;
    selected_slot = CURRENT_CAL_SLOT_NONE;

    if (valid_a == BOOL_TRUE && valid_b == BOOL_TRUE)
    {
        if (current_cal_sequence_newer(record_b.sequence, record_a.sequence) == BOOL_TRUE)
        {
            selected = &record_b;
            selected_slot = CURRENT_CAL_SLOT_B;
        }
        else
        {
            selected = &record_a;
            selected_slot = CURRENT_CAL_SLOT_A;
        }
    }
    else if (valid_a == BOOL_TRUE)
    {
        selected = &record_a;
        selected_slot = CURRENT_CAL_SLOT_A;
    }
    else if (valid_b == BOOL_TRUE)
    {
        selected = &record_b;
        selected_slot = CURRENT_CAL_SLOT_B;
    }

    current_cal_active_valid = BOOL_FALSE;
    current_cal_active_sequence = 0U;
    current_cal_active_slot_id = CURRENT_CAL_SLOT_NONE;
    memset(&current_cal_active_curve, 0, sizeof(current_cal_active_curve));
    if (selected != NULL)
    {
        current_cal_storage_activate(selected, selected_slot);
        if (selected->record_type == CURRENT_CAL_FLASH_TYPE_CURVE &&
            current_cal_active_valid != BOOL_TRUE)
        {
            (void)current_cal_storage_invalidate();
        }
    }
}

boolean_en current_cal_storage_has_active_curve(void)
{
    return current_cal_active_valid;
}

const current_cal_curve_t *current_cal_storage_active_curve(void)
{
    return (current_cal_active_valid == BOOL_TRUE) ? &current_cal_active_curve : NULL;
}

u32 current_cal_storage_sequence(void)
{
    return current_cal_active_sequence;
}

current_cal_slot_en current_cal_storage_active_slot(void)
{
    return current_cal_active_slot_id;
}

boolean_en current_cal_storage_commit(const current_cal_curve_t *curve)
{
    if (current_cal_curve_validate(curve, current_cal_profile_crc()) != CURRENT_CAL_CURVE_OK)
    {
        return BOOL_FALSE;
    }
    return current_cal_storage_store(CURRENT_CAL_FLASH_TYPE_CURVE, curve);
}

boolean_en current_cal_storage_invalidate(void)
{
    current_cal_active_valid = BOOL_FALSE;
    memset(&current_cal_active_curve, 0, sizeof(current_cal_active_curve));
    return current_cal_storage_store(CURRENT_CAL_FLASH_TYPE_TOMBSTONE, NULL);
}
