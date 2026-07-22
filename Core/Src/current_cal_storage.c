#include "current_cal_storage.h"
#include "flash_address_assignment.h"
#include "hw_flash.h"
#include "factory_user_data.h"

/* Legacy 80-byte record constants.  Never change: deployed v1 devices use it. */
#define CURRENT_CAL_V1_MAGIC             0x43414c31UL
#define CURRENT_CAL_V1_FORMAT_VERSION    1U
#define CURRENT_CAL_V1_TYPE_CURVE        1UL
#define CURRENT_CAL_V1_TYPE_TOMBSTONE    2UL
#define CURRENT_CAL_VALID_MARKER         0x56414c43UL
#define CURRENT_CAL_V1_RECORD_SIZE       80U

/*
 * V2 is an explicitly serialized 240-byte envelope.  PWM and meter sections
 * have independent state/context/data CRCs.  The outer CRC makes A/B selection
 * atomic; valid_marker is programmed only after full readback verification.
 */
#define CURRENT_CAL_V2_MAGIC             0x43414c32UL
#define CURRENT_CAL_V2_FORMAT_VERSION    2U
#define CURRENT_CAL_V2_RECORD_SIZE       240U
#define CURRENT_CAL_V2_FLAG_REPAIR_PAIR  0x52505231UL
#define CURRENT_CAL_REPAIR_GUARD_MARKER  0x00000000UL

#define V2_OFF_MAGIC                     0U
#define V2_OFF_FORMAT                    4U
#define V2_OFF_RECORD_SIZE               6U
#define V2_OFF_SEQUENCE                  8U
#define V2_OFF_FLAGS                     12U

#define V2_OFF_PWM_STATE                 16U
#define V2_OFF_PWM_CONTEXT_CRC           20U
#define V2_OFF_PWM_DATA_CRC              24U
#define V2_OFF_PWM_SECTION_VERSION       28U
#define V2_OFF_PWM_CURVE_VERSION         30U
#define V2_OFF_PWM_POINT_COUNT           32U
#define V2_OFF_PWM_RESERVED              34U
#define V2_OFF_PWM_CAL_MAX_MA            36U
#define V2_OFF_PWM_VALUES                40U
#define V2_PWM_SECTION_END               84U

#define V2_OFF_METER_STATE               84U
#define V2_OFF_METER_CONTEXT_CRC         88U
#define V2_OFF_METER_DATA_CRC            92U
#define V2_OFF_METER_SECTION_VERSION     96U
#define V2_OFF_METER_DATA_LENGTH         98U
#define V2_OFF_METER_PAYLOAD             100U
#define V2_OFF_METER_RESERVED            228U
#define V2_METER_SECTION_END             232U

#define V2_OFF_RECORD_CRC                232U
#define V2_OFF_VALID_MARKER               236U

CURRENT_CAL_STATIC_ASSERT(CURRENT_CAL_V2_RECORD_SIZE <= CURRENT_CAL_FLASH_SLOT_RESERVED);
CURRENT_CAL_STATIC_ASSERT(V2_OFF_PWM_VALUES + CURRENT_CAL_POINT_COUNT * 2U <= V2_PWM_SECTION_END);
CURRENT_CAL_STATIC_ASSERT(V2_OFF_METER_PAYLOAD + CURRENT_CAL_METER_PAYLOAD_SIZE == V2_OFF_METER_RESERVED);
CURRENT_CAL_STATIC_ASSERT(V2_OFF_METER_RESERVED + 4U == V2_METER_SECTION_END);
CURRENT_CAL_STATIC_ASSERT(V2_OFF_RECORD_CRC + 4U == V2_OFF_VALID_MARKER);
CURRENT_CAL_STATIC_ASSERT(V2_OFF_VALID_MARKER + 4U == CURRENT_CAL_V2_RECORD_SIZE);
CURRENT_CAL_STATIC_ASSERT((V2_OFF_VALID_MARKER & 3U) == 0U);

typedef enum
{
    CURRENT_CAL_RECORD_NONE = 0,
    CURRENT_CAL_RECORD_V1,
    CURRENT_CAL_RECORD_V2
} current_cal_record_format_en;

typedef struct
{
    current_cal_record_format_en format;
    u32 sequence;
    u8 bytes[CURRENT_CAL_V2_RECORD_SIZE];
} current_cal_record_t;

static current_cal_curve_t current_cal_active_curve;
static boolean_en current_cal_active_valid = BOOL_FALSE;
static boolean_en current_cal_active_legacy = BOOL_FALSE;
static current_cal_meter_section_t current_cal_active_meter;
static boolean_en current_cal_meter_valid = BOOL_FALSE;
static u32 current_cal_active_sequence = 0U;
static current_cal_slot_en current_cal_active_slot_id = CURRENT_CAL_SLOT_NONE;
static boolean_en current_cal_repair_required = BOOL_FALSE;
static boolean_en current_cal_repair_barrier_present = BOOL_FALSE;
static u32 current_cal_repair_sequence = 0U;

static u16 current_cal_get_u16_le(const u8 *src)
{
    return (u16)((u16)src[0] | ((u16)src[1] << 8));
}

static u32 current_cal_get_u32_le(const u8 *src)
{
    return (u32)src[0] |
           ((u32)src[1] << 8) |
           ((u32)src[2] << 16) |
           ((u32)src[3] << 24);
}

static void current_cal_put_u16_le(u8 *dst, u16 value)
{
    dst[0] = (u8)(value & 0xffU);
    dst[1] = (u8)((value >> 8) & 0xffU);
}

static void current_cal_put_u32_le(u8 *dst, u32 value)
{
    dst[0] = (u8)(value & 0xffUL);
    dst[1] = (u8)((value >> 8) & 0xffUL);
    dst[2] = (u8)((value >> 16) & 0xffUL);
    dst[3] = (u8)((value >> 24) & 0xffUL);
}

static u32 current_cal_v1_curve_crc(u16 curve_version,
                                    u16 point_count,
                                    const u16 *logical_pwm)
{
    u8 serialized[4U + CURRENT_CAL_POINT_COUNT * 2U];
    u8 *out;
    u8 i;

    out = serialized;
    current_cal_put_u16_le(out, curve_version); out += 2;
    current_cal_put_u16_le(out, point_count); out += 2;
    for (i = 0U; i < CURRENT_CAL_POINT_COUNT; ++i)
    {
        current_cal_put_u16_le(out, logical_pwm[i]);
        out += 2;
    }
    return current_cal_crc32(serialized, sizeof(serialized));
}

static boolean_en current_cal_v1_record_valid(const u8 *record)
{
    u32 record_type;
    u16 values[CURRENT_CAL_POINT_COUNT];
    u16 logical_max;
    u8 i;

    if (current_cal_get_u32_le(record + 0U) != CURRENT_CAL_V1_MAGIC ||
        current_cal_get_u16_le(record + 4U) != CURRENT_CAL_V1_FORMAT_VERSION ||
        current_cal_get_u16_le(record + 6U) != CURRENT_CAL_V1_RECORD_SIZE ||
        current_cal_get_u32_le(record + 76U) != CURRENT_CAL_VALID_MARKER ||
        current_cal_get_u32_le(record + 72U) != current_cal_crc32(record, 72U))
    {
        return BOOL_FALSE;
    }
    record_type = current_cal_get_u32_le(record + 12U);
    if (record_type == CURRENT_CAL_V1_TYPE_TOMBSTONE)
    {
        return BOOL_TRUE;
    }
    if (record_type != CURRENT_CAL_V1_TYPE_CURVE ||
        current_cal_get_u16_le(record + 24U) != CURRENT_CAL_CURVE_VERSION_LEGACY ||
        current_cal_get_u16_le(record + 26U) != CURRENT_CAL_POINT_COUNT)
    {
        return BOOL_FALSE;
    }
    for (i = 0U; i < CURRENT_CAL_POINT_COUNT; ++i)
    {
        values[i] = current_cal_get_u16_le(record + 28U + (u32)i * 2U);
    }
    if (values[0] != 0U ||
        current_cal_get_u32_le(record + 20U) !=
        current_cal_v1_curve_crc(CURRENT_CAL_CURVE_VERSION_LEGACY,
                                 CURRENT_CAL_POINT_COUNT, values))
    {
        return BOOL_FALSE;
    }
    logical_max = current_cal_pwm_logical_max();
    for (i = 1U; i < CURRENT_CAL_POINT_COUNT; ++i)
    {
        if (values[i] <= values[i - 1U] || values[i] > logical_max)
        {
            return BOOL_FALSE;
        }
    }
    return BOOL_TRUE;
}

static boolean_en current_cal_v2_record_valid(const u8 *record)
{
    return (current_cal_get_u32_le(record + V2_OFF_MAGIC) == CURRENT_CAL_V2_MAGIC &&
            current_cal_get_u16_le(record + V2_OFF_FORMAT) == CURRENT_CAL_V2_FORMAT_VERSION &&
            current_cal_get_u16_le(record + V2_OFF_RECORD_SIZE) == CURRENT_CAL_V2_RECORD_SIZE &&
            current_cal_get_u32_le(record + V2_OFF_VALID_MARKER) == CURRENT_CAL_VALID_MARKER &&
            current_cal_get_u32_le(record + V2_OFF_RECORD_CRC) ==
            current_cal_crc32(record, V2_OFF_RECORD_CRC)) ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en current_cal_storage_read(u32 address, current_cal_record_t *record)
{
    u32 magic;

    if (record == NULL)
    {
        return BOOL_FALSE;
    }
    memset(record, 0, sizeof(*record));
    hw_flash_read_bytes(address, record->bytes, CURRENT_CAL_V2_RECORD_SIZE);
    magic = current_cal_get_u32_le(record->bytes);
    if (magic == CURRENT_CAL_V2_MAGIC && current_cal_v2_record_valid(record->bytes) == BOOL_TRUE)
    {
        record->format = CURRENT_CAL_RECORD_V2;
        record->sequence = current_cal_get_u32_le(record->bytes + V2_OFF_SEQUENCE);
        return BOOL_TRUE;
    }
    if (magic == CURRENT_CAL_V1_MAGIC && current_cal_v1_record_valid(record->bytes) == BOOL_TRUE)
    {
        record->format = CURRENT_CAL_RECORD_V1;
        record->sequence = current_cal_get_u32_le(record->bytes + 8U);
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static boolean_en current_cal_marker_is_repair_guard(u32 marker)
{
    return (marker != CURRENT_CAL_VALID_MARKER &&
            marker != 0xffffffffUL &&
            (marker & CURRENT_CAL_VALID_MARKER) == marker) ?
           BOOL_TRUE : BOOL_FALSE;
}

/* Recognize a record whose CRC-covered bytes are still intact but whose
 * validity word has been deliberately programmed one-way into a repair
 * barrier.  Partial marker programming is also treated as a barrier. */
static boolean_en current_cal_record_repair_guarded(current_cal_record_t *record)
{
    u8 normalized[CURRENT_CAL_V2_RECORD_SIZE];
    u32 magic;
    u32 marker;

    magic = current_cal_get_u32_le(record->bytes);
    if (magic == CURRENT_CAL_V2_MAGIC)
    {
        marker = current_cal_get_u32_le(record->bytes + V2_OFF_VALID_MARKER);
        if (current_cal_marker_is_repair_guard(marker) != BOOL_TRUE)
        {
            return BOOL_FALSE;
        }
        memcpy(normalized, record->bytes, CURRENT_CAL_V2_RECORD_SIZE);
        current_cal_put_u32_le(normalized + V2_OFF_VALID_MARKER,
                               CURRENT_CAL_VALID_MARKER);
        if (current_cal_v2_record_valid(normalized) != BOOL_TRUE)
        {
            return BOOL_FALSE;
        }
        record->format = CURRENT_CAL_RECORD_V2;
        record->sequence = current_cal_get_u32_le(record->bytes + V2_OFF_SEQUENCE);
        return BOOL_TRUE;
    }
    if (magic == CURRENT_CAL_V1_MAGIC)
    {
        marker = current_cal_get_u32_le(record->bytes + 76U);
        if (current_cal_marker_is_repair_guard(marker) != BOOL_TRUE)
        {
            return BOOL_FALSE;
        }
        memcpy(normalized, record->bytes, CURRENT_CAL_V1_RECORD_SIZE);
        current_cal_put_u32_le(normalized + 76U, CURRENT_CAL_VALID_MARKER);
        if (current_cal_v1_record_valid(normalized) != BOOL_TRUE)
        {
            return BOOL_FALSE;
        }
        record->format = CURRENT_CAL_RECORD_V1;
        record->sequence = current_cal_get_u32_le(record->bytes + 8U);
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static boolean_en current_cal_record_is_repair_pair(
    const current_cal_record_t *record)
{
    return (record->format == CURRENT_CAL_RECORD_V2 &&
            current_cal_get_u32_le(record->bytes + V2_OFF_FLAGS) ==
                CURRENT_CAL_V2_FLAG_REPAIR_PAIR) ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en current_cal_guard_slot_b(void)
{
    current_cal_record_t record;
    u8 marker[4];
    u32 marker_offset;

    if (current_cal_storage_read(CURRENT_CAL_FLASH_SLOT_B_ADDR, &record) != BOOL_TRUE)
    {
        return current_cal_record_repair_guarded(&record);
    }
    marker_offset = (record.format == CURRENT_CAL_RECORD_V2) ?
                    V2_OFF_VALID_MARKER : 76U;
    current_cal_put_u32_le(marker, CURRENT_CAL_REPAIR_GUARD_MARKER);
    if (hw_flash_program_bytes_checked(CURRENT_CAL_FLASH_SLOT_B_ADDR + marker_offset,
                                       marker, sizeof(marker)) != HAL_OK)
    {
        /* A failed word program may still have cleared a subset of bits. */
        (void)current_cal_storage_read(CURRENT_CAL_FLASH_SLOT_B_ADDR, &record);
        return current_cal_record_repair_guarded(&record);
    }
    (void)current_cal_storage_read(CURRENT_CAL_FLASH_SLOT_B_ADDR, &record);
    return current_cal_record_repair_guarded(&record);
}

/* RFC-1982 style u32 serial comparison.  A half-range difference has no
 * deterministic winner and must be repaired rather than tie-broken. */
static int current_cal_sequence_compare(u32 lhs, u32 rhs)
{
    u32 difference;

    difference = lhs - rhs;
    if (difference == 0U)
    {
        return 0;
    }
    if (difference == 0x80000000UL)
    {
        return 2;
    }
    return (difference < 0x80000000UL) ? 1 : -1;
}

static u32 current_cal_meter_crc(const current_cal_meter_section_t *section)
{
    u8 serialized[4U + CURRENT_CAL_METER_PAYLOAD_SIZE];

    current_cal_put_u16_le(serialized, section->section_version);
    current_cal_put_u16_le(serialized + 2U, section->data_length);
    memset(serialized + 4U, 0, CURRENT_CAL_METER_PAYLOAD_SIZE);
    if (section->data_length != 0U)
    {
        memcpy(serialized + 4U, section->payload, section->data_length);
    }
    return current_cal_crc32(serialized, 4U + section->data_length);
}

static void current_cal_clear_meter(void)
{
    memset(&current_cal_active_meter, 0, sizeof(current_cal_active_meter));
    current_cal_active_meter.state = CURRENT_CAL_SECTION_EMPTY;
    current_cal_meter_valid = BOOL_FALSE;
}

static void current_cal_activate_meter_v2(const u8 *record)
{
    current_cal_meter_section_t section;
    u32 state;

    current_cal_clear_meter();
    state = current_cal_get_u32_le(record + V2_OFF_METER_STATE);
    if (state == CURRENT_CAL_SECTION_EMPTY)
    {
        return;
    }
    if (state != CURRENT_CAL_SECTION_VALID && state != CURRENT_CAL_SECTION_TOMBSTONE)
    {
        return;
    }
    memset(&section, 0, sizeof(section));
    section.state = (current_cal_section_state_en)state;
    section.context_crc = current_cal_get_u32_le(record + V2_OFF_METER_CONTEXT_CRC);
    section.data_crc = current_cal_get_u32_le(record + V2_OFF_METER_DATA_CRC);
    section.section_version = current_cal_get_u16_le(record + V2_OFF_METER_SECTION_VERSION);
    section.data_length = current_cal_get_u16_le(record + V2_OFF_METER_DATA_LENGTH);
    if (section.data_length > CURRENT_CAL_METER_PAYLOAD_SIZE)
    {
        return;
    }
    memcpy(section.payload, record + V2_OFF_METER_PAYLOAD, sizeof(section.payload));
    if (section.state == CURRENT_CAL_SECTION_TOMBSTONE)
    {
        if (section.data_length != 0U || section.data_crc != 0U)
        {
            return;
        }
    }
    else if (section.section_version == 0U ||
             section.data_crc != current_cal_meter_crc(&section))
    {
        return;
    }
    current_cal_active_meter = section;
    current_cal_meter_valid = BOOL_TRUE;
}

static void current_cal_activate_pwm_v2(const u8 *record)
{
    current_cal_curve_t curve;
    u32 state;
    u8 i;

    current_cal_active_valid = BOOL_FALSE;
    current_cal_active_legacy = BOOL_FALSE;
    memset(&current_cal_active_curve, 0, sizeof(current_cal_active_curve));
    state = current_cal_get_u32_le(record + V2_OFF_PWM_STATE);
    if (state != CURRENT_CAL_SECTION_VALID ||
        current_cal_get_u16_le(record + V2_OFF_PWM_SECTION_VERSION) != 1U)
    {
        return;
    }
    memset(&curve, 0, sizeof(curve));
    curve.context_crc = current_cal_get_u32_le(record + V2_OFF_PWM_CONTEXT_CRC);
    curve.curve_crc = current_cal_get_u32_le(record + V2_OFF_PWM_DATA_CRC);
    curve.curve_version = current_cal_get_u16_le(record + V2_OFF_PWM_CURVE_VERSION);
    curve.point_count = current_cal_get_u16_le(record + V2_OFF_PWM_POINT_COUNT);
    curve.calibration_max_current_ma = current_cal_get_u32_le(record + V2_OFF_PWM_CAL_MAX_MA);
    for (i = 0U; i < CURRENT_CAL_POINT_COUNT; ++i)
    {
        curve.logical_pwm[i] = current_cal_get_u16_le(record + V2_OFF_PWM_VALUES + (u32)i * 2U);
    }
    if (current_cal_curve_validate(&curve, current_cal_context_crc()) == CURRENT_CAL_CURVE_OK)
    {
        current_cal_active_curve = curve;
        current_cal_active_valid = BOOL_TRUE;
    }
}

static void current_cal_activate_v1(const u8 *record)
{
    u32 record_type;
    u8 i;

    current_cal_active_valid = BOOL_FALSE;
    current_cal_active_legacy = BOOL_FALSE;
    memset(&current_cal_active_curve, 0, sizeof(current_cal_active_curve));
    current_cal_clear_meter();
    record_type = current_cal_get_u32_le(record + 12U);
    if (record_type != CURRENT_CAL_V1_TYPE_CURVE ||
        current_cal_get_u32_le(record + 16U) != current_cal_legacy_profile_crc() ||
        SET_OUTCUR == 0U || SET_OUTCUR > HWMAX_OUTCUR)
    {
        return;
    }
    /* RAM-only normalization.  Flash remains untouched until the next commit. */
    current_cal_active_curve.curve_version = CURRENT_CAL_CURVE_VERSION;
    current_cal_active_curve.point_count = CURRENT_CAL_POINT_COUNT;
    current_cal_active_curve.calibration_max_current_ma = SET_OUTCUR;
    for (i = 0U; i < CURRENT_CAL_POINT_COUNT; ++i)
    {
        current_cal_active_curve.logical_pwm[i] =
            current_cal_get_u16_le(record + 28U + (u32)i * 2U);
    }
    current_cal_active_curve.context_crc = current_cal_context_crc();
    current_cal_active_curve.curve_crc = current_cal_curve_crc(&current_cal_active_curve);
    if (current_cal_curve_validate(&current_cal_active_curve,
                                   current_cal_context_crc()) == CURRENT_CAL_CURVE_OK)
    {
        current_cal_active_valid = BOOL_TRUE;
        current_cal_active_legacy = BOOL_TRUE;
    }
}

static void current_cal_storage_activate(const current_cal_record_t *record,
                                         current_cal_slot_en slot)
{
    current_cal_active_sequence = record->sequence;
    current_cal_active_slot_id = slot;
    if (record->format == CURRENT_CAL_RECORD_V2)
    {
        current_cal_activate_pwm_v2(record->bytes);
        current_cal_activate_meter_v2(record->bytes);
    }
    else
    {
        current_cal_activate_v1(record->bytes);
    }
}

static void current_cal_encode_pwm(u8 *record,
                                   current_cal_section_state_en state,
                                   const current_cal_curve_t *curve)
{
    u8 i;

    memset(record + V2_OFF_PWM_STATE, 0, V2_PWM_SECTION_END - V2_OFF_PWM_STATE);
    current_cal_put_u32_le(record + V2_OFF_PWM_STATE, state);
    if (state != CURRENT_CAL_SECTION_VALID || curve == NULL)
    {
        return;
    }
    current_cal_put_u32_le(record + V2_OFF_PWM_CONTEXT_CRC, curve->context_crc);
    current_cal_put_u32_le(record + V2_OFF_PWM_DATA_CRC, curve->curve_crc);
    current_cal_put_u16_le(record + V2_OFF_PWM_SECTION_VERSION, 1U);
    current_cal_put_u16_le(record + V2_OFF_PWM_CURVE_VERSION, curve->curve_version);
    current_cal_put_u16_le(record + V2_OFF_PWM_POINT_COUNT, curve->point_count);
    current_cal_put_u32_le(record + V2_OFF_PWM_CAL_MAX_MA,
                           curve->calibration_max_current_ma);
    for (i = 0U; i < CURRENT_CAL_POINT_COUNT; ++i)
    {
        current_cal_put_u16_le(record + V2_OFF_PWM_VALUES + (u32)i * 2U,
                               curve->logical_pwm[i]);
    }
}

static void current_cal_encode_meter(u8 *record,
                                     const current_cal_meter_section_t *section)
{
    memset(record + V2_OFF_METER_STATE, 0,
           V2_METER_SECTION_END - V2_OFF_METER_STATE);
    if (section == NULL || section->state == CURRENT_CAL_SECTION_EMPTY)
    {
        current_cal_put_u32_le(record + V2_OFF_METER_STATE,
                               CURRENT_CAL_SECTION_EMPTY);
        return;
    }
    current_cal_put_u32_le(record + V2_OFF_METER_STATE, section->state);
    current_cal_put_u32_le(record + V2_OFF_METER_CONTEXT_CRC, section->context_crc);
    current_cal_put_u32_le(record + V2_OFF_METER_DATA_CRC, section->data_crc);
    current_cal_put_u16_le(record + V2_OFF_METER_SECTION_VERSION,
                           section->section_version);
    current_cal_put_u16_le(record + V2_OFF_METER_DATA_LENGTH, section->data_length);
    memcpy(record + V2_OFF_METER_PAYLOAD, section->payload,
           CURRENT_CAL_METER_PAYLOAD_SIZE);
}

static boolean_en current_cal_write_v2(const u8 *record, current_cal_slot_en slot)
{
    u8 staged[CURRENT_CAL_V2_RECORD_SIZE];
    u8 readback[CURRENT_CAL_V2_RECORD_SIZE];
    u32 address;
    HAL_StatusTypeDef status;

    address = (slot == CURRENT_CAL_SLOT_A) ?
              CURRENT_CAL_FLASH_SLOT_A_ADDR : CURRENT_CAL_FLASH_SLOT_B_ADDR;
    memcpy(staged, record, sizeof(staged));
    current_cal_put_u32_le(staged + V2_OFF_VALID_MARKER, 0xffffffffUL);
    status = hw_flash_update_bytes_checked(address, staged, sizeof(staged));
    if (status != HAL_OK)
    {
        return BOOL_FALSE;
    }
    hw_flash_read_bytes(address, readback, sizeof(readback));
    if (memcmp(readback, staged, sizeof(readback)) != 0 ||
        current_cal_get_u32_le(readback + V2_OFF_RECORD_CRC) !=
        current_cal_crc32(readback, V2_OFF_RECORD_CRC))
    {
        return BOOL_FALSE;
    }
    status = hw_flash_program_bytes_checked(address + V2_OFF_VALID_MARKER,
                                            record + V2_OFF_VALID_MARKER, 4U);
    if (status != HAL_OK)
    {
        return BOOL_FALSE;
    }
    hw_flash_read_bytes(address, readback, sizeof(readback));
    return (memcmp(readback, record, sizeof(readback)) == 0 &&
            current_cal_v2_record_valid(readback) == BOOL_TRUE) ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en current_cal_storage_store(current_cal_section_state_en pwm_state,
                                            const current_cal_curve_t *curve,
                                            const current_cal_meter_section_t *meter)
{
    current_cal_record_t activated;
    current_cal_slot_en target_slot;
    u32 next_sequence;

    memset(&activated, 0, sizeof(activated));
    activated.format = CURRENT_CAL_RECORD_V2;
    if (current_cal_repair_required == BOOL_TRUE)
    {
        /* Use the guarded B record's sequence.  After A is written, A and B
         * are therefore equal-sequence/different-payload until B completes. */
        next_sequence = current_cal_repair_sequence;
    }
    else
    {
        next_sequence = current_cal_active_sequence + 1U;
        if (next_sequence == 0U)
        {
            next_sequence = 1U;
        }
    }
    activated.sequence = next_sequence;
    current_cal_put_u32_le(activated.bytes + V2_OFF_MAGIC, CURRENT_CAL_V2_MAGIC);
    current_cal_put_u16_le(activated.bytes + V2_OFF_FORMAT,
                           CURRENT_CAL_V2_FORMAT_VERSION);
    current_cal_put_u16_le(activated.bytes + V2_OFF_RECORD_SIZE,
                           CURRENT_CAL_V2_RECORD_SIZE);
    current_cal_put_u32_le(activated.bytes + V2_OFF_SEQUENCE, next_sequence);
    if (current_cal_repair_required == BOOL_TRUE)
    {
        current_cal_put_u32_le(activated.bytes + V2_OFF_FLAGS,
                               CURRENT_CAL_V2_FLAG_REPAIR_PAIR);
    }
    current_cal_encode_pwm(activated.bytes, pwm_state, curve);
    current_cal_encode_meter(activated.bytes, meter);
    current_cal_put_u32_le(activated.bytes + V2_OFF_RECORD_CRC,
                           current_cal_crc32(activated.bytes, V2_OFF_RECORD_CRC));
    current_cal_put_u32_le(activated.bytes + V2_OFF_VALID_MARKER,
                           CURRENT_CAL_VALID_MARKER);
    if (current_cal_repair_required == BOOL_TRUE)
    {
        if (current_cal_repair_barrier_present != BOOL_TRUE)
        {
            if (current_cal_guard_slot_b() != BOOL_TRUE)
            {
                return BOOL_FALSE;
            }
            current_cal_repair_barrier_present = BOOL_TRUE;
        }
        if (current_cal_write_v2(activated.bytes, CURRENT_CAL_SLOT_A) != BOOL_TRUE ||
            current_cal_write_v2(activated.bytes, CURRENT_CAL_SLOT_B) != BOOL_TRUE)
        {
            return BOOL_FALSE;
        }
        current_cal_repair_required = BOOL_FALSE;
        current_cal_repair_barrier_present = BOOL_FALSE;
        current_cal_storage_activate(&activated, CURRENT_CAL_SLOT_B);
        return BOOL_TRUE;
    }
    target_slot = (current_cal_active_slot_id == CURRENT_CAL_SLOT_A) ?
                  CURRENT_CAL_SLOT_B : CURRENT_CAL_SLOT_A;
    if (current_cal_write_v2(activated.bytes, target_slot) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    current_cal_storage_activate(&activated, target_slot);
    return BOOL_TRUE;
}

void current_cal_storage_init(void)
{
    current_cal_record_t record_a;
    current_cal_record_t record_b;
    const current_cal_record_t *selected;
    current_cal_slot_en selected_slot;
    boolean_en valid_a;
    boolean_en valid_b;
    boolean_en sequence_conflict;
    boolean_en guarded_a;
    boolean_en guarded_b;
    u32 repair_sequence;
    int sequence_order;

    valid_a = current_cal_storage_read(CURRENT_CAL_FLASH_SLOT_A_ADDR, &record_a);
    valid_b = current_cal_storage_read(CURRENT_CAL_FLASH_SLOT_B_ADDR, &record_b);
    selected = NULL;
    selected_slot = CURRENT_CAL_SLOT_NONE;
    sequence_conflict = BOOL_FALSE;
    guarded_a = (valid_a != BOOL_TRUE) ?
                current_cal_record_repair_guarded(&record_a) : BOOL_FALSE;
    guarded_b = (valid_b != BOOL_TRUE) ?
                current_cal_record_repair_guarded(&record_b) : BOOL_FALSE;
    repair_sequence = 0U;
    if (guarded_a == BOOL_TRUE || guarded_b == BOOL_TRUE)
    {
        sequence_conflict = BOOL_TRUE;
        repair_sequence = (guarded_b == BOOL_TRUE) ?
                          record_b.sequence : record_a.sequence;
    }
    else if (valid_a == BOOL_TRUE && valid_b == BOOL_TRUE)
    {
        if (record_a.sequence == record_b.sequence)
        {
            u32 compare_size;

            compare_size = (record_a.format == CURRENT_CAL_RECORD_V1) ?
                           CURRENT_CAL_V1_RECORD_SIZE : CURRENT_CAL_V2_RECORD_SIZE;
            if (record_a.format != record_b.format ||
                memcmp(record_a.bytes, record_b.bytes, compare_size) != 0)
            {
                /* Same sequence with different payload has no safe winner. */
                selected = NULL;
                sequence_conflict = BOOL_TRUE;
                repair_sequence = record_b.sequence;
            }
            else
            {
                selected = &record_a;
                selected_slot = CURRENT_CAL_SLOT_A;
            }
        }
        else
        {
            sequence_order = current_cal_sequence_compare(record_a.sequence,
                                                          record_b.sequence);
            if (sequence_order == 1)
            {
                selected = &record_a;
                selected_slot = CURRENT_CAL_SLOT_A;
            }
            else if (sequence_order == -1)
            {
                selected = &record_b;
                selected_slot = CURRENT_CAL_SLOT_B;
            }
            else
            {
                /* Exactly half the u32 range has no safe ordering. */
                selected = NULL;
                sequence_conflict = BOOL_TRUE;
                repair_sequence = record_b.sequence;
            }
        }
    }
    else if (valid_a == BOOL_TRUE)
    {
        if (current_cal_record_is_repair_pair(&record_a) == BOOL_TRUE)
        {
            sequence_conflict = BOOL_TRUE;
            repair_sequence = record_a.sequence;
        }
        else
        {
            selected = &record_a;
            selected_slot = CURRENT_CAL_SLOT_A;
        }
    }
    else if (valid_b == BOOL_TRUE)
    {
        if (current_cal_record_is_repair_pair(&record_b) == BOOL_TRUE)
        {
            sequence_conflict = BOOL_TRUE;
            repair_sequence = record_b.sequence;
        }
        else
        {
            selected = &record_b;
            selected_slot = CURRENT_CAL_SLOT_B;
        }
    }

    current_cal_active_valid = BOOL_FALSE;
    current_cal_active_legacy = BOOL_FALSE;
    current_cal_active_sequence = 0U;
    current_cal_active_slot_id = CURRENT_CAL_SLOT_NONE;
    current_cal_repair_required = BOOL_FALSE;
    current_cal_repair_barrier_present = BOOL_FALSE;
    current_cal_repair_sequence = 0U;
    memset(&current_cal_active_curve, 0, sizeof(current_cal_active_curve));
    current_cal_clear_meter();
    if (selected != NULL)
    {
        /* Boot is read-only, including v1 migration and context mismatch. */
        current_cal_storage_activate(selected, selected_slot);
    }
    else if (sequence_conflict == BOOL_TRUE)
    {
        /* No record is active until an explicit repair reconstructs both
         * slots as one byte-identical repair pair. */
        current_cal_active_sequence = repair_sequence;
        current_cal_active_slot_id = CURRENT_CAL_SLOT_A;
        current_cal_repair_required = BOOL_TRUE;
        current_cal_repair_barrier_present =
            (guarded_a == BOOL_TRUE || guarded_b == BOOL_TRUE ||
             (valid_a == BOOL_TRUE && valid_b != BOOL_TRUE &&
              current_cal_record_is_repair_pair(&record_a) == BOOL_TRUE) ||
             (valid_b == BOOL_TRUE && valid_a != BOOL_TRUE &&
              current_cal_record_is_repair_pair(&record_b) == BOOL_TRUE)) ?
            BOOL_TRUE : BOOL_FALSE;
        current_cal_repair_sequence = repair_sequence;
    }
}

boolean_en current_cal_storage_prepare_shared_page_update(void)
{
    current_cal_record_t active_record;
    current_cal_record_t mirror_record;
    current_cal_slot_en mirror_slot;
    u32 compare_size;

    if (current_cal_repair_required == BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (current_cal_active_slot_id == CURRENT_CAL_SLOT_NONE)
    {
        return BOOL_TRUE;
    }
    if (current_cal_storage_read(
            (current_cal_active_slot_id == CURRENT_CAL_SLOT_A) ?
            CURRENT_CAL_FLASH_SLOT_A_ADDR : CURRENT_CAL_FLASH_SLOT_B_ADDR,
            &active_record) != BOOL_TRUE ||
        active_record.sequence != current_cal_active_sequence)
    {
        return BOOL_FALSE;
    }

    mirror_slot = (current_cal_active_slot_id == CURRENT_CAL_SLOT_A) ?
                  CURRENT_CAL_SLOT_B : CURRENT_CAL_SLOT_A;
    if (current_cal_storage_read(
            (mirror_slot == CURRENT_CAL_SLOT_A) ?
            CURRENT_CAL_FLASH_SLOT_A_ADDR : CURRENT_CAL_FLASH_SLOT_B_ADDR,
            &mirror_record) == BOOL_TRUE &&
        mirror_record.format == active_record.format &&
        mirror_record.sequence == active_record.sequence)
    {
        compare_size = (active_record.format == CURRENT_CAL_RECORD_V1) ?
                       CURRENT_CAL_V1_RECORD_SIZE : CURRENT_CAL_V2_RECORD_SIZE;
        if (memcmp(mirror_record.bytes, active_record.bytes, compare_size) == 0)
        {
            return BOOL_TRUE;
        }
    }

    if (active_record.format == CURRENT_CAL_RECORD_V2)
    {
        return current_cal_write_v2(active_record.bytes, mirror_slot);
    }

    compare_size = CURRENT_CAL_V1_RECORD_SIZE;
    if (hw_flash_update_bytes_checked(
            (mirror_slot == CURRENT_CAL_SLOT_A) ?
            CURRENT_CAL_FLASH_SLOT_A_ADDR : CURRENT_CAL_FLASH_SLOT_B_ADDR,
            active_record.bytes, compare_size) != HAL_OK ||
        current_cal_storage_read(
            (mirror_slot == CURRENT_CAL_SLOT_A) ?
            CURRENT_CAL_FLASH_SLOT_A_ADDR : CURRENT_CAL_FLASH_SLOT_B_ADDR,
            &mirror_record) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    return (mirror_record.format == active_record.format &&
            mirror_record.sequence == active_record.sequence &&
            memcmp(mirror_record.bytes, active_record.bytes, compare_size) == 0) ?
           BOOL_TRUE : BOOL_FALSE;
}

boolean_en current_cal_storage_has_active_curve(void)
{
    return current_cal_active_valid;
}

boolean_en current_cal_storage_active_curve_is_legacy(void)
{
    return (current_cal_active_valid == BOOL_TRUE &&
            current_cal_active_legacy == BOOL_TRUE) ? BOOL_TRUE : BOOL_FALSE;
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
    const current_cal_meter_section_t *meter;

    if (current_cal_curve_validate(curve, current_cal_context_crc()) != CURRENT_CAL_CURVE_OK)
    {
        return BOOL_FALSE;
    }
    meter = (current_cal_meter_valid == BOOL_TRUE) ? &current_cal_active_meter : NULL;
    return current_cal_storage_store(CURRENT_CAL_SECTION_VALID, curve, meter);
}

boolean_en current_cal_storage_ensure_v2(void)
{
    if (current_cal_active_legacy != BOOL_TRUE)
    {
        return BOOL_TRUE;
    }
    if (current_cal_active_valid != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    return current_cal_storage_commit(&current_cal_active_curve);
}

boolean_en current_cal_storage_invalidate(void)
{
    const current_cal_meter_section_t *meter;

    meter = (current_cal_meter_valid == BOOL_TRUE) ? &current_cal_active_meter : NULL;
    return current_cal_storage_store(CURRENT_CAL_SECTION_TOMBSTONE, NULL, meter);
}

boolean_en current_cal_storage_get_meter_section(current_cal_meter_section_t *section)
{
    if (section == NULL || current_cal_meter_valid != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    *section = current_cal_active_meter;
    return BOOL_TRUE;
}

boolean_en current_cal_storage_commit_meter_section(const current_cal_meter_section_t *section)
{
    current_cal_section_state_en pwm_state;
    const current_cal_curve_t *curve;
    current_cal_meter_section_t checked;

    if (section == NULL || section->data_length > CURRENT_CAL_METER_PAYLOAD_SIZE ||
        (section->state != CURRENT_CAL_SECTION_VALID &&
         section->state != CURRENT_CAL_SECTION_TOMBSTONE))
    {
        return BOOL_FALSE;
    }
    checked = *section;
    if (checked.state == CURRENT_CAL_SECTION_TOMBSTONE)
    {
        checked.section_version = 0U;
        checked.data_length = 0U;
        checked.data_crc = 0U;
        memset(checked.payload, 0, sizeof(checked.payload));
    }
    else
    {
        if (checked.section_version == 0U)
        {
            return BOOL_FALSE;
        }
        checked.data_crc = current_cal_meter_crc(&checked);
    }
    curve = current_cal_storage_active_curve();
    pwm_state = (curve != NULL) ? CURRENT_CAL_SECTION_VALID : CURRENT_CAL_SECTION_TOMBSTONE;
    return current_cal_storage_store(pwm_state, curve, &checked);
}
