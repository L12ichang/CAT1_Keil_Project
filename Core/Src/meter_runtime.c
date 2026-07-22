#include "meter_runtime.h"
#include "current_cal_curve.h"
#include "current_cal_storage.h"
#include "flash_address_assignment.h"
#include "hw_flash.h"
#include "Portable.h"
#include "sys_data.h"
#include "sys_pwm.h"

#define METER_RUNTIME_ENERGY_RECORD_MAGIC       0x4d455231UL /* "MER1" */
#define METER_RUNTIME_ENERGY_RECORD_VERSION     1U
#define METER_RUNTIME_ENERGY_RECORD_SIZE        64U
#define METER_RUNTIME_ENERGY_RECORD_PREFIX_SIZE 56U
#define METER_RUNTIME_ENERGY_RECORD_MARKER      0xa55a3cc3UL
#define METER_RUNTIME_CHECKPOINT_OFFSET         12U
#define METER_RUNTIME_RECORD_CRC_OFFSET         56U
#define METER_RUNTIME_RECORD_MARKER_OFFSET      60U
#define METER_RUNTIME_CHECKPOINT_CONTEXT_OFFSET 8U
#define METER_RUNTIME_CHECKPOINT_COEFF_OFFSET   12U
#define METER_RUNTIME_CHECKPOINT_EPOCH_OFFSET   16U
#define METER_RUNTIME_CHECKPOINT_INTERVAL_MS    (6UL * 60UL * 60UL * 1000UL)
#define METER_RUNTIME_CHECKPOINT_RETRY_MS       (5UL * 60UL * 1000UL)
#define METER_RUNTIME_MAX_CONTINUOUS_GAP_MS     60000UL
#define METER_RUNTIME_MAX_INPUT_POWER_MW        2000000UL
#define METER_RUNTIME_CF_TRUST_MARGIN_COUNTS    8UL

#define METER_RUNTIME_ASSERT_JOIN_(a, b) a##b
#define METER_RUNTIME_ASSERT_JOIN(a, b) METER_RUNTIME_ASSERT_JOIN_(a, b)
#define METER_RUNTIME_STATIC_ASSERT(condition) \
    typedef char METER_RUNTIME_ASSERT_JOIN(meter_runtime_static_assert_, __LINE__)[(condition) ? 1 : -1]

METER_RUNTIME_STATIC_ASSERT(METER_RUNTIME_ENERGY_RECORD_SIZE ==
                            METER_RUNTIME_ENERGY_SLOT_RESERVED);
METER_RUNTIME_STATIC_ASSERT(METER_RUNTIME_CHECKPOINT_OFFSET +
                            METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE ==
                            METER_RUNTIME_ENERGY_RECORD_PREFIX_SIZE);
METER_RUNTIME_STATIC_ASSERT(METER_RUNTIME_RECORD_MARKER_OFFSET + 4U ==
                            METER_RUNTIME_ENERGY_RECORD_SIZE);
METER_RUNTIME_STATIC_ASSERT(METER_RUNTIME_ENERGY_SLOT_OFFSET >= 0x330UL);
METER_RUNTIME_STATIC_ASSERT(METER_RUNTIME_ENERGY_SLOT_OFFSET +
                            METER_RUNTIME_ENERGY_SLOT_RESERVED <=
                            CURRENT_CAL_FLASH_SLOT_OFFSET);

typedef enum
{
    METER_RUNTIME_ENERGY_SLOT_NONE = 0,
    METER_RUNTIME_ENERGY_SLOT_A,
    METER_RUNTIME_ENERGY_SLOT_B
} meter_runtime_energy_slot_en;

typedef enum
{
    METER_RUNTIME_ENERGY_STATE_NONE = 0,
    METER_RUNTIME_ENERGY_STATE_VALID,
    METER_RUNTIME_ENERGY_STATE_CONFLICT,
    METER_RUNTIME_ENERGY_STATE_INVALID
} meter_runtime_energy_state_en;

typedef struct
{
    u32 sequence;
    meter_cal_energy_checkpoint_t checkpoint;
    u8 bytes[METER_RUNTIME_ENERGY_RECORD_SIZE];
} meter_runtime_energy_record_t;

typedef struct
{
    meter_runtime_mode_en mode;
    meter_cal_result_en coefficient_result;
    meter_cal_coefficients_t coefficients;
    meter_runtime_snapshot_t snapshot;
    meter_cal_energy_accumulator_t accumulator;
    meter_runtime_energy_slot_en energy_slot;
    meter_runtime_energy_state_en energy_storage_state;
    u32 energy_sequence;
    u32 last_input_tick;
    u32 last_checkpoint_tick;
    u32 next_checkpoint_retry_tick;
    u32 continuity_epoch;
    u64 session_energy_base_uwh;
    boolean_en bl_continuous;
    boolean_en checkpoint_present;
    boolean_en checkpoint_dirty;
    boolean_en checkpoint_attempted;
    boolean_en persistence_fault_latched;
} meter_runtime_context_t;

static meter_runtime_context_t meter_runtime_ctx;

static boolean_en meter_runtime_persistence_fail(void)
{
    meter_runtime_ctx.persistence_fault_latched = BOOL_TRUE;
    meter_runtime_ctx.checkpoint_attempted = BOOL_TRUE;
    meter_runtime_ctx.next_checkpoint_retry_tick =
        Timer_GetTickCount() + METER_RUNTIME_CHECKPOINT_RETRY_MS;
    hw_flash_latch_update_fault();
    sys_pwm_force_off();
    return BOOL_FALSE;
}

static u32 meter_runtime_get_u24_le(const u8 *source)
{
    return (u32)source[0] |
           ((u32)source[1] << 8) |
           ((u32)source[2] << 16);
}

static u16 meter_runtime_get_u16_le(const u8 *source)
{
    return (u16)((u16)source[0] | ((u16)source[1] << 8));
}

static u32 meter_runtime_get_u32_le(const u8 *source)
{
    return (u32)source[0] |
           ((u32)source[1] << 8) |
           ((u32)source[2] << 16) |
           ((u32)source[3] << 24);
}

static void meter_runtime_put_u16_le(u8 *destination, u16 value)
{
    destination[0] = (u8)(value & 0xffU);
    destination[1] = (u8)((value >> 8) & 0xffU);
}

static void meter_runtime_put_u32_le(u8 *destination, u32 value)
{
    destination[0] = (u8)(value & 0xffUL);
    destination[1] = (u8)((value >> 8) & 0xffUL);
    destination[2] = (u8)((value >> 16) & 0xffUL);
    destination[3] = (u8)((value >> 24) & 0xffUL);
}

static u64 meter_runtime_saturating_mul_u32(u32 value, u32 multiplier)
{
    return (u64)value * (u64)multiplier;
}

meter_runtime_frame_result_en meter_runtime_parse_bl0942_frame(
    const u8 *bytes,
    u32 length,
    meter_runtime_bl0942_frame_t *frame)
{
    u8 checksum;
    u8 index;
    meter_runtime_bl0942_frame_t parsed;

    if (bytes == NULL || frame == NULL)
    {
        return METER_RUNTIME_FRAME_NULL;
    }
    memset(frame, 0, sizeof(*frame));
    if (length != METER_RUNTIME_BL0942_FRAME_SIZE)
    {
        return METER_RUNTIME_FRAME_BAD_LENGTH;
    }
    if (bytes[0] != METER_RUNTIME_BL0942_RESPONSE_HEADER)
    {
        return METER_RUNTIME_FRAME_BAD_HEADER;
    }
    /* FREQ and STATUS are 16- and 8-bit values padded to the full-read
     * register width.  Non-zero reserved bytes mean the response layout is
     * not the one this firmware was calibrated against. */
    if (bytes[18] != 0U || bytes[20] != 0U || bytes[21] != 0U)
    {
        return METER_RUNTIME_FRAME_BAD_RESERVED;
    }
    checksum = METER_RUNTIME_BL0942_READ_COMMAND;
    for (index = 0U; index <= 21U; ++index)
    {
        checksum = (u8)(checksum + bytes[index]);
    }
    checksum = (u8)(~checksum);
    if (checksum != bytes[22])
    {
        return METER_RUNTIME_FRAME_BAD_CHECKSUM;
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.current_rms_raw = meter_runtime_get_u24_le(bytes + 1U);
    parsed.voltage_rms_raw = meter_runtime_get_u24_le(bytes + 4U);
    parsed.current_fast_raw = meter_runtime_get_u24_le(bytes + 7U);
    parsed.raw_watt24 = meter_runtime_get_u24_le(bytes + 10U);
    parsed.signed_watt_raw =
        meter_calibration_sign_extend_s24(parsed.raw_watt24);
    parsed.cf_counter24 = meter_runtime_get_u24_le(bytes + 13U);
    parsed.frequency_period_raw = (u32)meter_runtime_get_u16_le(bytes + 16U);
    parsed.status = bytes[19];
    *frame = parsed;
    return METER_RUNTIME_FRAME_OK;
}

static int meter_runtime_sequence_compare(u32 left, u32 right)
{
    u32 difference;

    difference = left - right;
    if (difference == 0U || difference == 0x80000000UL)
    {
        return 0;
    }
    return (difference < 0x80000000UL) ? 1 : -1;
}

static boolean_en meter_runtime_energy_record_decode(
    const u8 *bytes,
    meter_runtime_energy_record_t *record)
{
    u32 stored_crc;
    u32 stored_context;
    u32 stored_coefficient_crc;
    u32 expected_epoch;
    meter_cal_result_en result;

    if (bytes == NULL || record == NULL)
    {
        return BOOL_FALSE;
    }
    if (meter_runtime_get_u32_le(bytes) != METER_RUNTIME_ENERGY_RECORD_MAGIC ||
        meter_runtime_get_u16_le(bytes + 4U) !=
            METER_RUNTIME_ENERGY_RECORD_VERSION ||
        meter_runtime_get_u16_le(bytes + 6U) !=
            METER_RUNTIME_ENERGY_RECORD_SIZE ||
        meter_runtime_get_u32_le(bytes + METER_RUNTIME_RECORD_MARKER_OFFSET) !=
            METER_RUNTIME_ENERGY_RECORD_MARKER)
    {
        return BOOL_FALSE;
    }
    stored_crc = meter_runtime_get_u32_le(
        bytes + METER_RUNTIME_RECORD_CRC_OFFSET);
    if (stored_crc != current_cal_crc32(
            bytes, METER_RUNTIME_ENERGY_RECORD_PREFIX_SIZE))
    {
        return BOOL_FALSE;
    }
    expected_epoch = meter_runtime_get_u32_le(
        bytes + METER_RUNTIME_CHECKPOINT_OFFSET +
        METER_RUNTIME_CHECKPOINT_EPOCH_OFFSET);
    stored_context = meter_runtime_get_u32_le(
        bytes + METER_RUNTIME_CHECKPOINT_OFFSET +
        METER_RUNTIME_CHECKPOINT_CONTEXT_OFFSET);
    stored_coefficient_crc = meter_runtime_get_u32_le(
        bytes + METER_RUNTIME_CHECKPOINT_OFFSET +
        METER_RUNTIME_CHECKPOINT_COEFF_OFFSET);
    result = meter_calibration_energy_checkpoint_decode(
        bytes + METER_RUNTIME_CHECKPOINT_OFFSET,
        METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE,
        stored_context,
        stored_coefficient_crc,
        expected_epoch,
        &record->checkpoint);
    if (result != METER_CAL_OK)
    {
        return BOOL_FALSE;
    }
    record->sequence = meter_runtime_get_u32_le(bytes + 8U);
    memcpy(record->bytes, bytes, sizeof(record->bytes));
    return BOOL_TRUE;
}

static boolean_en meter_runtime_energy_record_read(
    u32 address,
    meter_runtime_energy_record_t *record)
{
    u8 bytes[METER_RUNTIME_ENERGY_RECORD_SIZE];

    hw_flash_read_bytes(address, bytes, sizeof(bytes));
    return meter_runtime_energy_record_decode(bytes, record);
}

static boolean_en meter_runtime_energy_slot_erased(u32 address)
{
    u8 bytes[METER_RUNTIME_ENERGY_RECORD_SIZE];
    u32 index;

    hw_flash_read_bytes(address, bytes, sizeof(bytes));
    for (index = 0U; index < sizeof(bytes); ++index)
    {
        if (bytes[index] != 0xffU)
        {
            return BOOL_FALSE;
        }
    }
    return BOOL_TRUE;
}

static meter_runtime_energy_state_en meter_runtime_energy_select(
    meter_runtime_energy_record_t *record_a,
    meter_runtime_energy_record_t *record_b,
    const meter_runtime_energy_record_t **selected,
    meter_runtime_energy_slot_en *selected_slot,
    boolean_en *redundant)
{
    boolean_en valid_a;
    boolean_en valid_b;
    int order;

    if (record_a == NULL || record_b == NULL || selected == NULL ||
        selected_slot == NULL || redundant == NULL)
    {
        return METER_RUNTIME_ENERGY_STATE_INVALID;
    }
    valid_a = meter_runtime_energy_record_read(
        METER_RUNTIME_ENERGY_SLOT_A_ADDR, record_a);
    valid_b = meter_runtime_energy_record_read(
        METER_RUNTIME_ENERGY_SLOT_B_ADDR, record_b);
    *selected = NULL;
    *selected_slot = METER_RUNTIME_ENERGY_SLOT_NONE;
    *redundant = BOOL_FALSE;
    if (valid_a == BOOL_TRUE && valid_b == BOOL_TRUE)
    {
        if (record_a->sequence == record_b->sequence)
        {
            if (memcmp(record_a->bytes, record_b->bytes,
                       sizeof(record_a->bytes)) != 0)
            {
                return METER_RUNTIME_ENERGY_STATE_CONFLICT;
            }
            *selected = record_a;
            *selected_slot = METER_RUNTIME_ENERGY_SLOT_A;
            *redundant = BOOL_TRUE;
            return METER_RUNTIME_ENERGY_STATE_VALID;
        }
        order = meter_runtime_sequence_compare(record_a->sequence,
                                               record_b->sequence);
        if (order == 0)
        {
            return METER_RUNTIME_ENERGY_STATE_CONFLICT;
        }
        *selected = (order > 0) ? record_a : record_b;
        *selected_slot = (order > 0) ?
                         METER_RUNTIME_ENERGY_SLOT_A :
                         METER_RUNTIME_ENERGY_SLOT_B;
        return METER_RUNTIME_ENERGY_STATE_VALID;
    }
    if (valid_a == BOOL_TRUE || valid_b == BOOL_TRUE)
    {
        *selected = (valid_a == BOOL_TRUE) ? record_a : record_b;
        *selected_slot = (valid_a == BOOL_TRUE) ?
                         METER_RUNTIME_ENERGY_SLOT_A :
                         METER_RUNTIME_ENERGY_SLOT_B;
        return METER_RUNTIME_ENERGY_STATE_VALID;
    }
    if (meter_runtime_energy_slot_erased(
            METER_RUNTIME_ENERGY_SLOT_A_ADDR) == BOOL_TRUE &&
        meter_runtime_energy_slot_erased(
            METER_RUNTIME_ENERGY_SLOT_B_ADDR) == BOOL_TRUE)
    {
        return METER_RUNTIME_ENERGY_STATE_NONE;
    }
    return METER_RUNTIME_ENERGY_STATE_INVALID;
}

static boolean_en meter_runtime_energy_record_write_exact(
    u32 address,
    const u8 *complete_record)
{
    meter_runtime_energy_record_t verify;
    u8 staged[METER_RUNTIME_ENERGY_RECORD_SIZE];
    u8 marker[4];

    if (complete_record == NULL)
    {
        return meter_runtime_persistence_fail();
    }
    memcpy(staged, complete_record, sizeof(staged));
    memset(staged + METER_RUNTIME_RECORD_MARKER_OFFSET, 0xff, 4U);
    meter_runtime_put_u32_le(marker, METER_RUNTIME_ENERGY_RECORD_MARKER);
    if (hw_flash_update_bytes_checked(address, staged, sizeof(staged)) !=
            HAL_OK ||
        hw_flash_program_bytes_checked(
            address + METER_RUNTIME_RECORD_MARKER_OFFSET,
            marker, sizeof(marker)) != HAL_OK ||
        meter_runtime_energy_record_read(address, &verify) != BOOL_TRUE ||
        memcmp(verify.bytes, complete_record,
               METER_RUNTIME_ENERGY_RECORD_SIZE) != 0)
    {
        return meter_runtime_persistence_fail();
    }
    return BOOL_TRUE;
}

/* Registered with current_cal_storage as the one non-recursive coordinator.
 * It calls only the calibration-only primitive; that primitive never calls
 * this callback.  Cross-unique states cannot be repaired with only two pages
 * and therefore fail closed without erasing either page. */
static boolean_en meter_runtime_prepare_shared_page_update(void)
{
    meter_runtime_energy_record_t record_a;
    meter_runtime_energy_record_t record_b;
    const meter_runtime_energy_record_t *selected;
    meter_runtime_energy_slot_en energy_slot;
    meter_runtime_energy_state_en energy_state;
    current_cal_slot_en cal_slot;
    current_cal_slot_en repair_target;
    boolean_en energy_redundant;
    boolean_en cal_redundant;
    boolean_en same_source;
    u32 target_address;

    /* A bad decoded coefficient section must not make its own replacement
     * impossible.  Keep PWM latched off until reboot, but let the physical
     * CAL/ENERGY checks below decide whether this shared-page write is safe. */
    energy_state = meter_runtime_energy_select(
        &record_a, &record_b, &selected, &energy_slot,
        &energy_redundant);
    if (energy_state == METER_RUNTIME_ENERGY_STATE_CONFLICT ||
        energy_state == METER_RUNTIME_ENERGY_STATE_INVALID)
    {
        return meter_runtime_persistence_fail();
    }
    cal_redundant = current_cal_storage_calibration_redundant();
    if (current_cal_storage_repair_required() == BOOL_TRUE)
    {
        if (energy_state == METER_RUNTIME_ENERGY_STATE_NONE ||
            energy_redundant == BOOL_TRUE)
        {
            return BOOL_TRUE;
        }
        /* An initial ambiguous CAL conflict has no page that may be erased.
         * Once a deliberate guard/repair-pair exists, storage exposes the
         * one interrupted-transaction page that is safe to reconstruct. */
        repair_target = current_cal_storage_repair_safe_peer_target();
        if (repair_target == CURRENT_CAL_SLOT_NONE ||
            (repair_target == CURRENT_CAL_SLOT_A &&
             energy_slot == METER_RUNTIME_ENERGY_SLOT_A) ||
            (repair_target == CURRENT_CAL_SLOT_B &&
             energy_slot == METER_RUNTIME_ENERGY_SLOT_B))
        {
            return meter_runtime_persistence_fail();
        }
        target_address = (repair_target == CURRENT_CAL_SLOT_A) ?
                         METER_RUNTIME_ENERGY_SLOT_A_ADDR :
                         METER_RUNTIME_ENERGY_SLOT_B_ADDR;
        if (meter_runtime_energy_record_write_exact(
                target_address, selected->bytes) != BOOL_TRUE)
        {
            return BOOL_FALSE;
        }
        meter_runtime_ctx.energy_storage_state =
            METER_RUNTIME_ENERGY_STATE_VALID;
        return BOOL_TRUE;
    }
    cal_slot = current_cal_storage_active_slot();
    same_source =
        ((cal_slot == CURRENT_CAL_SLOT_A &&
          energy_slot == METER_RUNTIME_ENERGY_SLOT_A) ||
         (cal_slot == CURRENT_CAL_SLOT_B &&
          energy_slot == METER_RUNTIME_ENERGY_SLOT_B)) ?
        BOOL_TRUE : BOOL_FALSE;
    if (cal_redundant != BOOL_TRUE &&
        energy_state == METER_RUNTIME_ENERGY_STATE_VALID &&
        energy_redundant != BOOL_TRUE && same_source != BOOL_TRUE)
    {
        return meter_runtime_persistence_fail();
    }
    if (cal_redundant != BOOL_TRUE &&
        current_cal_storage_prepare_calibration_only() != BOOL_TRUE)
    {
        return meter_runtime_persistence_fail();
    }
    if (energy_state != METER_RUNTIME_ENERGY_STATE_VALID ||
        energy_redundant == BOOL_TRUE)
    {
        return BOOL_TRUE;
    }
    target_address = (energy_slot == METER_RUNTIME_ENERGY_SLOT_A) ?
                     METER_RUNTIME_ENERGY_SLOT_B_ADDR :
                     METER_RUNTIME_ENERGY_SLOT_A_ADDR;
    if (meter_runtime_energy_record_write_exact(
            target_address, selected->bytes) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    meter_runtime_ctx.energy_storage_state =
        METER_RUNTIME_ENERGY_STATE_VALID;
    return BOOL_TRUE;
}

static void meter_runtime_restore_checkpoint(void)
{
    meter_runtime_energy_record_t record_a;
    meter_runtime_energy_record_t record_b;
    const meter_runtime_energy_record_t *selected;
    boolean_en redundant;

    meter_runtime_ctx.energy_storage_state = meter_runtime_energy_select(
        &record_a, &record_b, &selected,
        &meter_runtime_ctx.energy_slot, &redundant);
    (void)redundant;
    if (meter_runtime_ctx.energy_storage_state ==
            METER_RUNTIME_ENERGY_STATE_CONFLICT ||
        meter_runtime_ctx.energy_storage_state ==
            METER_RUNTIME_ENERGY_STATE_INVALID)
    {
        meter_runtime_ctx.snapshot.energy_valid = BOOL_FALSE;
        meter_runtime_ctx.snapshot.session_energy_uwh = 0ULL;
        meter_runtime_ctx.snapshot.total_energy_uwh = 0ULL;
        (void)meter_runtime_persistence_fail();
        return;
    }

    if (selected != NULL &&
        selected->checkpoint.context_crc == current_cal_context_crc() &&
        selected->checkpoint.coefficient_data_crc ==
            meter_runtime_ctx.coefficients.data_crc &&
        meter_calibration_energy_accumulator_restore(
            &meter_runtime_ctx.accumulator,
            &selected->checkpoint,
            current_cal_context_crc(),
            meter_runtime_ctx.coefficients.data_crc,
            selected->checkpoint.continuity_epoch) == METER_CAL_OK)
    {
        meter_runtime_ctx.energy_sequence = selected->sequence;
        meter_runtime_ctx.continuity_epoch =
            selected->checkpoint.continuity_epoch + 1U;
        if (meter_runtime_ctx.continuity_epoch == 0U)
        {
            meter_runtime_ctx.continuity_epoch = 1U;
        }
        meter_runtime_ctx.checkpoint_present = BOOL_TRUE;
        return;
    }
    if (selected != NULL)
    {
        /* A coefficient/context replacement changes CF scale and invalidates
         * the saved baseline/remainder, but never discards already measured
         * lifetime energy.  Re-baseline under a new epoch and rewrite using
         * the current coefficient CRC after the first sample. */
        meter_runtime_ctx.continuity_epoch =
            selected->checkpoint.continuity_epoch + 1U;
        if (meter_runtime_ctx.continuity_epoch == 0U)
        {
            meter_runtime_ctx.continuity_epoch = 1U;
        }
        meter_calibration_energy_accumulator_init(
            &meter_runtime_ctx.accumulator,
            selected->checkpoint.total_energy_uwh,
            meter_runtime_ctx.continuity_epoch);
        meter_runtime_ctx.energy_sequence = selected->sequence;
        meter_runtime_ctx.checkpoint_present = BOOL_FALSE;
        meter_runtime_ctx.checkpoint_dirty = BOOL_TRUE;
        return;
    }

    if (meter_runtime_ctx.energy_storage_state !=
        METER_RUNTIME_ENERGY_STATE_NONE)
    {
        meter_runtime_ctx.snapshot.energy_valid = BOOL_FALSE;
        (void)meter_runtime_persistence_fail();
        return;
    }

    /* One-time legacy migration.  Before a first valid checkpoint exists,
     * the old 0.01 Wh field is an absolute starting value, not a delta, so a
     * restart cannot add it twice. */
    meter_calibration_energy_accumulator_init(
        &meter_runtime_ctx.accumulator,
        meter_runtime_saturating_mul_u32(sys_data.ac_EnergyP, 10000U),
        1U);
    meter_runtime_ctx.continuity_epoch = 1U;
    meter_runtime_ctx.energy_sequence = 0U;
    meter_runtime_ctx.checkpoint_present = BOOL_FALSE;
    meter_runtime_ctx.energy_storage_state = METER_RUNTIME_ENERGY_STATE_NONE;
}

void meter_runtime_init(void)
{
    current_cal_meter_section_t section;
    current_cal_meter_status_en section_status;
    meter_cal_result_en result;

    memset(&meter_runtime_ctx, 0, sizeof(meter_runtime_ctx));
    meter_runtime_ctx.mode = METER_RUNTIME_MODE_FALLBACK;
    meter_runtime_ctx.coefficient_result = METER_CAL_OK;
    meter_runtime_ctx.snapshot.mode = METER_RUNTIME_MODE_FALLBACK;
    meter_runtime_ctx.snapshot.coefficient_result = METER_CAL_OK;
    meter_runtime_ctx.snapshot.energy_valid = BOOL_TRUE;
    meter_runtime_ctx.snapshot.total_energy_uwh =
        meter_runtime_saturating_mul_u32(sys_data.ac_EnergyP, 10000U);
    meter_runtime_ctx.snapshot.session_energy_uwh = 0ULL;
    current_cal_storage_register_shared_peer(
        meter_runtime_prepare_shared_page_update);

    section_status = current_cal_storage_meter_status();
    if (section_status == CURRENT_CAL_METER_STATUS_ABSENT ||
        section_status == CURRENT_CAL_METER_STATUS_TOMBSTONE)
    {
        return;
    }
    if (section_status != CURRENT_CAL_METER_STATUS_VALID ||
        current_cal_storage_get_meter_section(&section) != BOOL_TRUE)
    {
        meter_runtime_ctx.mode = METER_RUNTIME_MODE_INVALID;
        meter_runtime_ctx.snapshot.mode = METER_RUNTIME_MODE_INVALID;
        meter_runtime_ctx.snapshot.energy_valid = BOOL_FALSE;
        (void)meter_runtime_persistence_fail();
        return;
    }
    result = meter_calibration_coefficients_decode(
        section.payload,
        section.data_length,
        current_cal_context_crc(),
        &meter_runtime_ctx.coefficients);
    meter_runtime_ctx.coefficient_result = result;
    meter_runtime_ctx.snapshot.coefficient_result = result;
    if (result != METER_CAL_OK ||
        section.context_crc != current_cal_context_crc() ||
        section.data_crc == 0U)
    {
        meter_runtime_ctx.mode = METER_RUNTIME_MODE_INVALID;
        meter_runtime_ctx.snapshot.mode = METER_RUNTIME_MODE_INVALID;
        meter_runtime_ctx.snapshot.energy_valid = BOOL_FALSE;
        (void)meter_runtime_persistence_fail();
        return;
    }

    meter_runtime_ctx.mode = METER_RUNTIME_MODE_CALIBRATED;
    meter_runtime_ctx.snapshot.mode = METER_RUNTIME_MODE_CALIBRATED;
    if (meter_runtime_ctx.coefficients.flags == METER_CAL_FLAGS_ENERGY_CF24)
    {
        meter_runtime_restore_checkpoint();
        if (meter_runtime_ctx.persistence_fault_latched != BOOL_TRUE)
        {
            meter_runtime_ctx.session_energy_base_uwh =
                meter_runtime_ctx.accumulator.total_energy_uwh;
            meter_runtime_ctx.snapshot.total_energy_uwh =
                meter_runtime_ctx.accumulator.total_energy_uwh;
            meter_runtime_ctx.snapshot.energy_valid = BOOL_TRUE;
            meter_runtime_ctx.snapshot.continuity_epoch =
                meter_runtime_ctx.continuity_epoch;
        }
        else
        {
            meter_runtime_ctx.snapshot.energy_valid = BOOL_FALSE;
        }
    }
    else
    {
        meter_runtime_ctx.snapshot.energy_valid = BOOL_FALSE;
    }
    /* A reset or a BL0942 power cycle invalidates counter continuity even
     * when a checkpoint was restored successfully. */
    meter_runtime_ctx.bl_continuous = BOOL_FALSE;
    meter_runtime_ctx.last_checkpoint_tick = Timer_GetTickCount();
}

meter_runtime_mode_en meter_runtime_mode(void)
{
    return meter_runtime_ctx.mode;
}

static boolean_en meter_runtime_cf_trust_bound(u32 elapsed_ms,
                                               u32 *maximum_delta)
{
    u64 maximum_energy_uwh;
    u64 numerator;
    u64 quotient;
    u64 gain_q24;

    if (maximum_delta == NULL || elapsed_ms == 0U ||
        elapsed_ms > METER_RUNTIME_MAX_CONTINUOUS_GAP_MS)
    {
        return BOOL_FALSE;
    }
    gain_q24 = meter_runtime_ctx.coefficients.energy_gain_q24;
    if (gain_q24 == 0ULL)
    {
        return BOOL_FALSE;
    }
    maximum_energy_uwh =
        ((u64)METER_RUNTIME_MAX_INPUT_POWER_MW * (u64)elapsed_ms +
         3599ULL) / 3600ULL;
    if (maximum_energy_uwh > ((~(u64)0) >> METER_CAL_Q24_SHIFT))
    {
        return BOOL_FALSE;
    }
    numerator = maximum_energy_uwh << METER_CAL_Q24_SHIFT;
    quotient = (numerator + gain_q24 - 1ULL) / gain_q24;
    quotient += METER_RUNTIME_CF_TRUST_MARGIN_COUNTS;
    /* The accumulator API requires proof that fewer than one complete
     * hidden 24-bit revolution was physically possible. */
    if (quotient >= (u64)METER_CAL_CF_COUNTER_MASK)
    {
        return BOOL_FALSE;
    }
    *maximum_delta = (u32)quotient;
    return BOOL_TRUE;
}

void meter_runtime_mark_bl_discontinuous(void)
{
    if (meter_runtime_ctx.mode != METER_RUNTIME_MODE_CALIBRATED)
    {
        return;
    }
    meter_runtime_ctx.bl_continuous = BOOL_FALSE;
    ++meter_runtime_ctx.continuity_epoch;
    if (meter_runtime_ctx.continuity_epoch == 0U)
    {
        meter_runtime_ctx.continuity_epoch = 1U;
    }
}

void meter_runtime_publish_bl0942(
    const meter_runtime_bl0942_frame_t *frame,
    const meter_runtime_legacy_input_t *legacy)
{
    meter_runtime_snapshot_t candidate;
    meter_cal_result_en result;
    boolean_en continuous;
    u32 maximum_delta;
    u32 elapsed_ms;
    u32 delta_uwh;
    u32 primask;

    if (frame == NULL || legacy == NULL)
    {
        return;
    }
    candidate = meter_runtime_ctx.snapshot;
    candidate.mode = meter_runtime_ctx.mode;
    candidate.coefficient_result = meter_runtime_ctx.coefficient_result;
    candidate.input_sequence = frame->sequence;
    candidate.input_sample_tick = frame->sample_tick;
    candidate.meter_status = frame->status;
    candidate.input_valid = BOOL_FALSE;
    maximum_delta = 0U;

    if (meter_runtime_ctx.mode == METER_RUNTIME_MODE_FALLBACK)
    {
        candidate.input_voltage_mv = legacy->voltage_01v * 100U;
        candidate.input_current_ua = legacy->current_ma * 1000U;
        candidate.input_active_power_mw = legacy->active_power_001w * 10U;
        candidate.input_frequency_millihz = legacy->frequency_001hz * 10U;
        candidate.input_pf_ppm =
            (legacy->pf_percent > 100U ? 100U : legacy->pf_percent) * 10000U;
        candidate.session_energy_uwh =
            meter_runtime_saturating_mul_u32(
                legacy->session_energy_001wh, 10000U);
        candidate.total_energy_uwh =
            meter_runtime_saturating_mul_u32(
                legacy->total_energy_001wh, 10000U);
        candidate.energy_valid = BOOL_TRUE;
        candidate.input_valid = BOOL_TRUE;
    }
    else if (meter_runtime_ctx.mode == METER_RUNTIME_MODE_CALIBRATED)
    {
        result = meter_calibration_convert(
            &meter_runtime_ctx.coefficients, current_cal_context_crc(),
            METER_CAL_CHANNEL_INPUT_VOLTAGE_MV,
            frame->voltage_rms_raw, &candidate.input_voltage_mv);
        if (result == METER_CAL_OK)
        {
            result = meter_calibration_convert(
                &meter_runtime_ctx.coefficients, current_cal_context_crc(),
                METER_CAL_CHANNEL_INPUT_CURRENT_UA,
                frame->current_rms_raw, &candidate.input_current_ua);
        }
        if (result == METER_CAL_OK)
        {
            result = meter_calibration_convert(
                &meter_runtime_ctx.coefficients, current_cal_context_crc(),
                METER_CAL_CHANNEL_INPUT_ACTIVE_POWER_MW,
                frame->raw_watt24, &candidate.input_active_power_mw);
        }
        if (result == METER_CAL_OK)
        {
            result = meter_calibration_convert(
                &meter_runtime_ctx.coefficients, current_cal_context_crc(),
                METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ,
                frame->frequency_period_raw,
                &candidate.input_frequency_millihz);
        }
        if (result == METER_CAL_OK)
        {
            candidate.input_pf_ppm = meter_calibration_input_pf_ppm(
                candidate.input_active_power_mw,
                candidate.input_voltage_mv,
                candidate.input_current_ua);
            candidate.input_valid = BOOL_TRUE;
        }

        if (meter_runtime_ctx.coefficients.flags ==
            METER_CAL_FLAGS_ENERGY_CF24)
        {
            elapsed_ms = frame->sample_tick - meter_runtime_ctx.last_input_tick;
            continuous = meter_runtime_ctx.bl_continuous;
            if (continuous == BOOL_TRUE &&
                meter_runtime_cf_trust_bound(elapsed_ms,
                                             &maximum_delta) != BOOL_TRUE)
            {
                meter_runtime_mark_bl_discontinuous();
                continuous = BOOL_FALSE;
            }
            if (continuous != BOOL_TRUE)
            {
                maximum_delta = 0U;
            }
            result = meter_calibration_energy_accumulate_cf24(
                &meter_runtime_ctx.coefficients,
                current_cal_context_crc(),
                frame->cf_counter24,
                maximum_delta,
                meter_runtime_ctx.continuity_epoch,
                continuous,
                &meter_runtime_ctx.accumulator,
                &delta_uwh);
            if (result == METER_CAL_OK ||
                result == METER_CAL_COUNTER_DISCONTINUITY ||
                result == METER_CAL_CONVERSION_SATURATED ||
                result == METER_CAL_ACCUMULATOR_OVERFLOW)
            {
                meter_runtime_ctx.checkpoint_dirty = BOOL_TRUE;
                candidate.total_energy_uwh =
                    meter_runtime_ctx.accumulator.total_energy_uwh;
                candidate.session_energy_uwh =
                    (candidate.total_energy_uwh >=
                     meter_runtime_ctx.session_energy_base_uwh) ?
                    candidate.total_energy_uwh -
                        meter_runtime_ctx.session_energy_base_uwh : 0ULL;
                candidate.energy_valid = BOOL_TRUE;
                candidate.continuity_epoch =
                    meter_runtime_ctx.continuity_epoch;
            }
        }
        meter_runtime_ctx.bl_continuous = BOOL_TRUE;
        meter_runtime_ctx.last_input_tick = frame->sample_tick;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    meter_runtime_ctx.snapshot = candidate;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

void meter_runtime_publish_output(
    const meter_runtime_output_sample_t *sample)
{
    meter_runtime_snapshot_t candidate;
    meter_cal_result_en result;
    u32 primask;

    if (sample == NULL)
    {
        return;
    }
    candidate = meter_runtime_ctx.snapshot;
    candidate.output_sequence = sample->sequence;
    candidate.output_sample_tick = sample->sample_tick;
    candidate.protect_code = sample->protect_code;
    candidate.output_valid = BOOL_FALSE;
    if (meter_runtime_ctx.mode == METER_RUNTIME_MODE_FALLBACK)
    {
        candidate.output_voltage_mv = sample->voltage_01v * 100U;
        candidate.output_current_ua = sample->current_ma * 1000U;
        candidate.output_power_mw = sample->power_01w * 100U;
        candidate.output_valid = BOOL_TRUE;
    }
    else if (meter_runtime_ctx.mode == METER_RUNTIME_MODE_CALIBRATED)
    {
        result = meter_calibration_convert(
            &meter_runtime_ctx.coefficients, current_cal_context_crc(),
            METER_CAL_CHANNEL_OUTPUT_VOLTAGE_MV,
            sample->voltage_adc_raw, &candidate.output_voltage_mv);
        if (result == METER_CAL_OK)
        {
            result = meter_calibration_convert(
                &meter_runtime_ctx.coefficients, current_cal_context_crc(),
                METER_CAL_CHANNEL_OUTPUT_CURRENT_UA,
                sample->current_adc_raw, &candidate.output_current_ua);
        }
        if (result == METER_CAL_OK)
        {
            candidate.output_power_mw =
                meter_calibration_output_power_mw(
                    candidate.output_voltage_mv,
                    candidate.output_current_ua);
            candidate.output_valid = BOOL_TRUE;
        }
    }
    primask = __get_PRIMASK();
    __disable_irq();
    meter_runtime_ctx.snapshot = candidate;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

boolean_en meter_runtime_get_snapshot(meter_runtime_snapshot_t *snapshot)
{
    u32 now;
    u32 primask;

    if (snapshot == NULL)
    {
        return BOOL_FALSE;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    *snapshot = meter_runtime_ctx.snapshot;
    if (primask == 0U)
    {
        __enable_irq();
    }
    now = Timer_GetTickCount();
    snapshot->input_sample_age_ms = now - snapshot->input_sample_tick;
    snapshot->output_sample_age_ms = now - snapshot->output_sample_tick;
    if (snapshot->input_sample_tick == 0U ||
        snapshot->input_sample_age_ms > METER_RUNTIME_SNAPSHOT_MAX_AGE_MS)
    {
        snapshot->input_valid = BOOL_FALSE;
    }
    if (snapshot->output_sample_tick == 0U ||
        snapshot->output_sample_age_ms > METER_RUNTIME_SNAPSHOT_MAX_AGE_MS)
    {
        snapshot->output_valid = BOOL_FALSE;
    }
    return (snapshot->mode != METER_RUNTIME_MODE_INVALID &&
            (snapshot->input_valid == BOOL_TRUE ||
             snapshot->output_valid == BOOL_TRUE)) ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en meter_runtime_checkpoint_store(void)
{
    meter_cal_energy_checkpoint_t checkpoint;
    meter_cal_result_en result;
    meter_runtime_energy_slot_en target_slot;
    u32 first_address;
    u32 second_address;
    u32 next_sequence;
    u8 bytes[METER_RUNTIME_ENERGY_RECORD_SIZE];

    if (meter_runtime_ctx.mode != METER_RUNTIME_MODE_CALIBRATED ||
        meter_runtime_ctx.coefficients.flags !=
            METER_CAL_FLAGS_ENERGY_CF24)
    {
        return meter_runtime_persistence_fail();
    }
    result = meter_calibration_energy_checkpoint_capture(
        &meter_runtime_ctx.coefficients,
        current_cal_context_crc(),
        &meter_runtime_ctx.accumulator,
        &checkpoint);
    if (result != METER_CAL_OK)
    {
        return meter_runtime_persistence_fail();
    }
    memset(bytes, 0xff, sizeof(bytes));
    next_sequence = meter_runtime_ctx.energy_sequence + 1U;
    meter_runtime_put_u32_le(bytes, METER_RUNTIME_ENERGY_RECORD_MAGIC);
    meter_runtime_put_u16_le(bytes + 4U,
                            METER_RUNTIME_ENERGY_RECORD_VERSION);
    meter_runtime_put_u16_le(bytes + 6U,
                            METER_RUNTIME_ENERGY_RECORD_SIZE);
    meter_runtime_put_u32_le(bytes + 8U, next_sequence);
    result = meter_calibration_energy_checkpoint_encode(
        &checkpoint,
        current_cal_context_crc(),
        meter_runtime_ctx.coefficients.data_crc,
        checkpoint.continuity_epoch,
        bytes + METER_RUNTIME_CHECKPOINT_OFFSET,
        METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE);
    if (result != METER_CAL_OK)
    {
        return meter_runtime_persistence_fail();
    }
    meter_runtime_put_u32_le(
        bytes + METER_RUNTIME_RECORD_CRC_OFFSET,
        current_cal_crc32(bytes, METER_RUNTIME_ENERGY_RECORD_PREFIX_SIZE));
    meter_runtime_put_u32_le(bytes + METER_RUNTIME_RECORD_MARKER_OFFSET,
                            METER_RUNTIME_ENERGY_RECORD_MARKER);

    target_slot = (meter_runtime_ctx.energy_slot ==
                   METER_RUNTIME_ENERGY_SLOT_A) ?
                  METER_RUNTIME_ENERGY_SLOT_B :
                  METER_RUNTIME_ENERGY_SLOT_A;
    first_address = (target_slot == METER_RUNTIME_ENERGY_SLOT_A) ?
                    METER_RUNTIME_ENERGY_SLOT_A_ADDR :
                    METER_RUNTIME_ENERGY_SLOT_B_ADDR;
    second_address = (target_slot == METER_RUNTIME_ENERGY_SLOT_A) ?
                     METER_RUNTIME_ENERGY_SLOT_B_ADDR :
                     METER_RUNTIME_ENERGY_SLOT_A_ADDR;
    if (current_cal_storage_prepare_shared_page_update() != BOOL_TRUE)
    {
        return meter_runtime_persistence_fail();
    }
    if (meter_runtime_energy_record_write_exact(
            first_address, bytes) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    /* The first marker is already a complete new winner.  Commit this fact
     * before touching the former active page so a second-write failure never
     * causes a later in-RAM rollback to the old sequence. */
    meter_runtime_ctx.energy_slot = target_slot;
    meter_runtime_ctx.energy_sequence = next_sequence;
    meter_runtime_ctx.checkpoint_present = BOOL_TRUE;
    meter_runtime_ctx.energy_storage_state = METER_RUNTIME_ENERGY_STATE_VALID;
    if (meter_runtime_energy_record_write_exact(
            second_address, bytes) != BOOL_TRUE)
    {
        meter_runtime_ctx.checkpoint_dirty = BOOL_TRUE;
        return BOOL_FALSE;
    }
    meter_runtime_ctx.checkpoint_dirty = BOOL_FALSE;
    meter_runtime_ctx.checkpoint_attempted = BOOL_FALSE;
    meter_runtime_ctx.last_checkpoint_tick = Timer_GetTickCount();
    return BOOL_TRUE;
}

void meter_runtime_process(void)
{
    u32 now;

    if (meter_runtime_ctx.persistence_fault_latched == BOOL_TRUE)
    {
        sys_pwm_force_off();
    }
    if (meter_runtime_ctx.checkpoint_dirty != BOOL_TRUE)
    {
        return;
    }
    now = Timer_GetTickCount();
    if (meter_runtime_ctx.checkpoint_attempted == BOOL_TRUE)
    {
        if ((u32)(now - meter_runtime_ctx.next_checkpoint_retry_tick) >=
            0x80000000UL)
        {
            return;
        }
    }
    else if (meter_runtime_ctx.checkpoint_present == BOOL_TRUE &&
        (now - meter_runtime_ctx.last_checkpoint_tick) <
            METER_RUNTIME_CHECKPOINT_INTERVAL_MS)
    {
        return;
    }
    if (meter_runtime_checkpoint_store() != BOOL_TRUE)
    {
        (void)meter_runtime_persistence_fail();
    }
}

boolean_en meter_runtime_power_down_save(void)
{
    if (meter_runtime_ctx.mode == METER_RUNTIME_MODE_INVALID ||
        meter_runtime_ctx.persistence_fault_latched == BOOL_TRUE)
    {
        return meter_runtime_persistence_fail();
    }
    if (meter_runtime_ctx.mode != METER_RUNTIME_MODE_CALIBRATED ||
        meter_runtime_ctx.coefficients.flags != METER_CAL_FLAGS_ENERGY_CF24)
    {
        return BOOL_TRUE;
    }
    if (meter_runtime_ctx.checkpoint_dirty != BOOL_TRUE &&
        meter_runtime_ctx.checkpoint_present == BOOL_TRUE)
    {
        return BOOL_TRUE;
    }
    return meter_runtime_checkpoint_store();
}

boolean_en meter_runtime_energy_clear(void)
{
    u32 primask;

    if (meter_runtime_ctx.mode == METER_RUNTIME_MODE_INVALID ||
        meter_runtime_ctx.persistence_fault_latched == BOOL_TRUE)
    {
        return meter_runtime_persistence_fail();
    }
    if (meter_runtime_ctx.mode != METER_RUNTIME_MODE_CALIBRATED ||
        meter_runtime_ctx.coefficients.flags != METER_CAL_FLAGS_ENERGY_CF24)
    {
        return BOOL_TRUE;
    }
    meter_runtime_mark_bl_discontinuous();
    meter_calibration_energy_accumulator_init(
        &meter_runtime_ctx.accumulator, 0ULL,
        meter_runtime_ctx.continuity_epoch);
    meter_runtime_ctx.session_energy_base_uwh = 0ULL;
    meter_runtime_ctx.checkpoint_dirty = BOOL_TRUE;
    primask = __get_PRIMASK();
    __disable_irq();
    meter_runtime_ctx.snapshot.session_energy_uwh = 0ULL;
    meter_runtime_ctx.snapshot.total_energy_uwh = 0ULL;
    meter_runtime_ctx.snapshot.continuity_epoch =
        meter_runtime_ctx.continuity_epoch;
    if (primask == 0U)
    {
        __enable_irq();
    }
    return meter_runtime_checkpoint_store();
}
