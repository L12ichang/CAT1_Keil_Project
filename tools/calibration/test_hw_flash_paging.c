#include <stdio.h>
#include <string.h>

#include "hw_flash_page_writer.h"

enum
{
    TEST_PAGE_SIZE = 16U,
    TEST_FLASH_SIZE = TEST_PAGE_SIZE * 3U
};

typedef struct
{
    u8 storage[TEST_FLASH_SIZE];
    u8 initial[TEST_FLASH_SIZE];
    u32 read_calls;
    u32 erase_calls;
    u32 program_calls;
    u32 check_calls;
    u32 fail_erase_call;
    u32 fail_program_call;
    u32 fail_check_call;
} test_flash_st;

static test_flash_st *g_test_flash;

static boolean_en test_flash_range_valid(u32 address, u32 length)
{
    return (address <= (u32)TEST_FLASH_SIZE &&
            length <= ((u32)TEST_FLASH_SIZE - address)) ?
           BOOL_TRUE : BOOL_FALSE;
}

static boolean_en test_flash_read(u32 page_addr,
                                  u8 *page_buffer,
                                  u32 page_size)
{
    if (page_size != (u32)TEST_PAGE_SIZE ||
        test_flash_range_valid(page_addr, page_size) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    ++g_test_flash->read_calls;
    memcpy(page_buffer, &g_test_flash->storage[page_addr], page_size);
    return BOOL_TRUE;
}

static boolean_en test_flash_erase(u32 page_addr)
{
    ++g_test_flash->erase_calls;
    if (g_test_flash->fail_erase_call == g_test_flash->erase_calls ||
        test_flash_range_valid(page_addr, (u32)TEST_PAGE_SIZE) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    memset(&g_test_flash->storage[page_addr], 0xFF, TEST_PAGE_SIZE);
    return BOOL_TRUE;
}

static boolean_en test_flash_program(u32 page_addr,
                                     const u8 *page_buffer,
                                     u32 page_size)
{
    ++g_test_flash->program_calls;
    if (g_test_flash->fail_program_call == g_test_flash->program_calls ||
        page_size != (u32)TEST_PAGE_SIZE ||
        test_flash_range_valid(page_addr, page_size) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    memcpy(&g_test_flash->storage[page_addr], page_buffer, page_size);
    return BOOL_TRUE;
}

static boolean_en test_flash_check(u32 flash_addr,
                                   const u8 *buffer,
                                   u32 length)
{
    ++g_test_flash->check_calls;
    if (g_test_flash->fail_check_call == g_test_flash->check_calls ||
        test_flash_range_valid(flash_addr, length) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    return (memcmp(&g_test_flash->storage[flash_addr], buffer, length) == 0) ?
           BOOL_TRUE : BOOL_FALSE;
}

static void test_flash_reset(test_flash_st *test_flash)
{
    u32 i;

    for (i = 0U; i < (u32)TEST_FLASH_SIZE; ++i)
    {
        test_flash->storage[i] = (u8)(0x30U + i);
    }
    memcpy(test_flash->initial, test_flash->storage, TEST_FLASH_SIZE);
    test_flash->read_calls = 0U;
    test_flash->erase_calls = 0U;
    test_flash->program_calls = 0U;
    test_flash->check_calls = 0U;
    test_flash->fail_erase_call = 0U;
    test_flash->fail_program_call = 0U;
    test_flash->fail_check_call = 0U;
}

static void test_flash_context_init(
    hw_flash_page_writer_context_st *context,
    u8 *page_buffer)
{
    context->page_size = TEST_PAGE_SIZE;
    context->page_buffer = page_buffer;
    context->read_page = test_flash_read;
    context->erase_page = test_flash_erase;
    context->program_page = test_flash_program;
    context->check_range = test_flash_check;
}

static int test_expect(boolean_en condition, const char *message)
{
    if (condition != BOOL_TRUE)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

static int test_nonaligned_cross_page(void)
{
    test_flash_st test_flash;
    hw_flash_page_writer_context_st context;
    u8 page_buffer[TEST_PAGE_SIZE];
    u8 source[8];
    u32 i;

    g_test_flash = &test_flash;
    test_flash_reset(&test_flash);
    test_flash_context_init(&context, page_buffer);
    for (i = 0U; i < (u32)sizeof(source); ++i)
    {
        source[i] = (u8)(0x90U + i);
    }

    if (!test_expect(hw_flash_write_bytes_paged(13U, source, sizeof(source),
                                                &context) == BOOL_TRUE,
                     "nonaligned cross-page write succeeds") ||
        !test_expect(test_flash.read_calls == 2U &&
                     test_flash.erase_calls == 2U &&
                     test_flash.program_calls == 2U &&
                     test_flash.check_calls == 2U,
                     "nonaligned cross-page callback counts"))
    {
        return 0;
    }
    for (i = 0U; i < (u32)TEST_FLASH_SIZE; ++i)
    {
        u8 expected = test_flash.initial[i];
        if (i >= 13U && i < 21U)
        {
            expected = source[i - 13U];
        }
        if (!test_expect(test_flash.storage[i] == expected,
                         "nonaligned cross-page data and neighbors"))
        {
            return 0;
        }
    }
    return 1;
}

static int test_two_full_pages(void)
{
    test_flash_st test_flash;
    hw_flash_page_writer_context_st context;
    u8 page_buffer[TEST_PAGE_SIZE];
    u8 source[TEST_PAGE_SIZE * 2U];
    u32 i;

    g_test_flash = &test_flash;
    test_flash_reset(&test_flash);
    test_flash_context_init(&context, page_buffer);
    for (i = 0U; i < (u32)sizeof(source); ++i)
    {
        source[i] = (u8)(0xC0U + i);
    }

    if (!test_expect(hw_flash_write_bytes_paged(0U, source, sizeof(source),
                                                &context) == BOOL_TRUE,
                     "two full pages write succeeds") ||
        !test_expect(test_flash.read_calls == 0U &&
                     test_flash.erase_calls == 2U &&
                     test_flash.program_calls == 2U &&
                     test_flash.check_calls == 2U,
                     "two full pages callback counts") ||
        !test_expect(memcmp(test_flash.storage, source, sizeof(source)) == 0,
                     "two full pages use corresponding source data") ||
        !test_expect(memcmp(&test_flash.storage[TEST_PAGE_SIZE * 2U],
                            &test_flash.initial[TEST_PAGE_SIZE * 2U],
                            TEST_PAGE_SIZE) == 0,
                     "two full pages preserve adjacent page"))
    {
        return 0;
    }
    return 1;
}

static int test_erase_failure_is_visible(void)
{
    test_flash_st test_flash;
    hw_flash_page_writer_context_st context;
    u8 page_buffer[TEST_PAGE_SIZE];
    u8 source[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};

    g_test_flash = &test_flash;
    test_flash_reset(&test_flash);
    test_flash_context_init(&context, page_buffer);
    test_flash.fail_erase_call = 2U;

    return test_expect(hw_flash_write_bytes_paged(13U, source, sizeof(source),
                                                  &context) == BOOL_FALSE,
                       "erase failure is not reported as success") &&
           test_expect(test_flash.erase_calls == 2U &&
                       test_flash.program_calls == 1U &&
                       test_flash.check_calls == 1U,
                       "erase failure stops the current page");
}

static int test_program_failure_is_visible(void)
{
    test_flash_st test_flash;
    hw_flash_page_writer_context_st context;
    u8 page_buffer[TEST_PAGE_SIZE];
    u8 source[8] = {11U, 12U, 13U, 14U, 15U, 16U, 17U, 18U};

    g_test_flash = &test_flash;
    test_flash_reset(&test_flash);
    test_flash_context_init(&context, page_buffer);
    test_flash.fail_program_call = 2U;

    return test_expect(hw_flash_write_bytes_paged(13U, source, sizeof(source),
                                                  &context) == BOOL_FALSE,
                       "program failure is not reported as success") &&
           test_expect(test_flash.program_calls == 2U &&
                       test_flash.check_calls == 1U,
                       "program failure stops before readback");
}

static int test_check_failure_is_visible(void)
{
    test_flash_st test_flash;
    hw_flash_page_writer_context_st context;
    u8 page_buffer[TEST_PAGE_SIZE];
    u8 source[8] = {21U, 22U, 23U, 24U, 25U, 26U, 27U, 28U};

    g_test_flash = &test_flash;
    test_flash_reset(&test_flash);
    test_flash_context_init(&context, page_buffer);
    test_flash.fail_check_call = 2U;

    return test_expect(hw_flash_write_bytes_paged(13U, source, sizeof(source),
                                                  &context) == BOOL_FALSE,
                       "readback failure is not reported as success") &&
           test_expect(test_flash.check_calls == 2U,
                       "readback failure is visible on the failing page");
}

int main(void)
{
    if (!test_expect(HW_FLASH_PAGE_WRITER_VERSION == 1U,
                     "paging helper version is frozen") ||
        !test_nonaligned_cross_page() ||
        !test_two_full_pages() ||
        !test_erase_failure_is_visible() ||
        !test_program_failure_is_visible() ||
        !test_check_failure_is_visible())
    {
        return 1;
    }
    puts("hw flash paged regression v1: PASS");
    return 0;
}
