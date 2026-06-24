#include "test.h"
#include "util.h"

static int test_map16_scales_linearly(void)
{
	ASSERT_EQ(0, MAP16(0, 0, 100, 0, 200));
	ASSERT_EQ(200, MAP16(100, 0, 100, 0, 200));
	ASSERT_EQ(100, MAP16(50, 0, 100, 0, 200));
	return 1;
}

static int test_map32_scales_linearly(void)
{
	ASSERT_EQ(0, MAP32(4334, 4334, 5746, 0, 100));
	ASSERT_EQ(100, MAP32(5746, 4334, 5746, 0, 100));
	return 1;
}

static int test_clamp_bounds_value(void)
{
	ASSERT_EQ(0, CLAMP(-5, 0, 100));
	ASSERT_EQ(100, CLAMP(150, 0, 100));
	ASSERT_EQ(42, CLAMP(42, 0, 100));
	return 1;
}

static int test_expand_u16_combines_bytes(void)
{
	ASSERT_EQ(0x16F8, EXPAND_U16(0x16, 0xF8));
	ASSERT_EQ(0, EXPAND_U16(0, 0));
	return 1;
}

void test_util_run(void)
{
	RUN_TEST(test_map16_scales_linearly);
	RUN_TEST(test_map32_scales_linearly);
	RUN_TEST(test_clamp_bounds_value);
	RUN_TEST(test_expand_u16_combines_bytes);
}
