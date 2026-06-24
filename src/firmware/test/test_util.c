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

static int test_max_min_abs_basic(void)
{
	ASSERT_EQ(5, MAX(5, 3));
	ASSERT_EQ(5, MAX(3, 5));
	ASSERT_EQ(3, MIN(5, 3));
	ASSERT_EQ(3, MIN(3, 5));
	ASSERT_EQ(5, ABS(-5));
	ASSERT_EQ(5, ABS(5));
	return 1;
}

// MAX/MIN/ABS used to be missing their outer parens, e.g.
// #define MAX(x, y) (x) > (y) ? (x) : (y)
// which breaks when the macro is used as a sub-expression: operator
// precedence binds the surrounding "+"/"-" to the first operand before the
// ternary is evaluated. These pin the now-fixed, properly parenthesized
// behavior so it can't regress silently.
static int test_max_min_abs_as_subexpression(void)
{
	ASSERT_EQ(4, 1 + MAX(2, 3));
	ASSERT_EQ(8, 10 - MIN(2, 3));
	ASSERT_EQ(6, 1 + ABS(-5));
	return 1;
}

static int test_compute_checksum_is_additive_with_wraparound(void)
{
	uint8_t buf1[] = { 1, 2, 3 };
	ASSERT_EQ(6, compute_checksum(buf1, 3));

	uint8_t buf2[] = { 255, 255 };
	ASSERT_EQ(254, compute_checksum(buf2, 2)); // 255 + 255 = 510, wraps to 254

	ASSERT_EQ(0, compute_checksum(buf1, 0));

	return 1;
}

void test_util_run(void)
{
	RUN_TEST(test_map16_scales_linearly);
	RUN_TEST(test_map32_scales_linearly);
	RUN_TEST(test_clamp_bounds_value);
	RUN_TEST(test_expand_u16_combines_bytes);
	RUN_TEST(test_max_min_abs_basic);
	RUN_TEST(test_max_min_abs_as_subexpression);
	RUN_TEST(test_compute_checksum_is_additive_with_wraparound);
}
