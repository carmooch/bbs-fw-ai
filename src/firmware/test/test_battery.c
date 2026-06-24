#include "test.h"
#include "battery.h"
#include "cfgstore.h"
#include "mock_hw.h"

// Mirrors the worked example in the comment above compute_battery_percent()
// in battery.c: 58.80V max, 42.0V low cutoff, 8% padding each side.
static void configure_battery(void)
{
	g_config.low_cut_off_v = 42;
	g_config.max_battery_x100v_u16h = 0x16;
	g_config.max_battery_x100v_u16l = 0xF8; // 0x16F8 = 5880 => 58.80V
	battery_init();
}

static int test_battery_percent_at_voltage_extremes(void)
{
	mock_reset();
	configure_battery();

	// ~57.5V: documented as ~100% SOC.
	mock_set_battery_voltage_x10(575);
	battery_process();
	ASSERT_EQ(100, battery_get_percent());

	return 1;
}

static int test_battery_percent_before_first_reading(void)
{
	mock_reset();
	configure_battery();

	// No voltage reading yet: battery_init()'s 70% placeholder holds.
	mock_set_battery_voltage_x10(0);
	battery_process();
	ASSERT_EQ(70, battery_get_percent());

	return 1;
}

static int test_battery_percent_ignored_under_load(void)
{
	mock_reset();
	configure_battery();

	mock_set_battery_voltage_x10(575);
	battery_process();
	ASSERT_EQ(100, battery_get_percent());

	// Motor under load: a sagged reading should not be applied yet,
	// even though it would otherwise compute to 0%.
	mock_set_target_current(50);
	mock_set_battery_voltage_x10(400);
	battery_process();
	ASSERT_EQ(100, battery_get_percent());

	return 1;
}

static int test_battery_percent_updates_after_no_load_delay(void)
{
	mock_reset();
	configure_battery();

	mock_set_battery_voltage_x10(575);
	battery_process();
	ASSERT_EQ(100, battery_get_percent());

	mock_set_target_current(50);
	mock_set_battery_voltage_x10(400);
	battery_process();
	ASSERT_EQ(100, battery_get_percent());

	// Motor stops: the no-load delay (2000ms) starts counting.
	mock_set_target_current(0);
	mock_set_ms(5000);
	battery_process();
	ASSERT_EQ(100, battery_get_percent());

	// Still within the delay window.
	mock_set_ms(6000);
	battery_process();
	ASSERT_EQ(100, battery_get_percent());

	// Past the delay window: the resting voltage reading is applied.
	mock_set_ms(7500);
	battery_process();
	ASSERT_EQ(0, battery_get_percent());

	return 1;
}

void test_battery_run(void)
{
	RUN_TEST(test_battery_percent_at_voltage_extremes);
	RUN_TEST(test_battery_percent_before_first_reading);
	RUN_TEST(test_battery_percent_ignored_under_load);
	RUN_TEST(test_battery_percent_updates_after_no_load_delay);
}
