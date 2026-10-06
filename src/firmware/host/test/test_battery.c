#include "test.h"
#include "fake_hw.h"
#include "battery.h"
#include "cfgstore.h"

// 58.80V full, 42V cutoff: the worked example above compute_battery_percent().
static void configure_battery(void)
{
	g_config.low_cut_off_v = 42;
	g_config.max_battery_x100v_u16h = (uint8_t)(5880 >> 8);
	g_config.max_battery_x100v_u16l = (uint8_t)5880;
	battery_init();
}

static int test_battery_placeholder_until_first_reading(void)
{
	fake_hw_reset();
	configure_battery();

	g_hw.battery_voltage_x10 = 0;
	battery_process();
	ASSERT_EQ(70, battery_get_percent());

	return 1;
}

static int test_battery_reads_full_when_charged(void)
{
	fake_hw_reset();
	configure_battery();

	g_hw.battery_voltage_x10 = 575;
	battery_process();
	ASSERT_EQ(100, battery_get_percent());

	return 1;
}

static int test_battery_ignores_voltage_sag_under_load(void)
{
	fake_hw_reset();
	configure_battery();

	g_hw.battery_voltage_x10 = 575;
	battery_process();
	ASSERT_EQ(100, battery_get_percent());

	// Motor pulling current: the sagged voltage must not move the reading.
	g_hw.motor_target_current = 50;
	g_hw.battery_voltage_x10 = 400;
	battery_process();
	ASSERT_EQ(100, battery_get_percent());

	// Motor stops. The reading only updates after 2s of no load.
	g_hw.motor_target_current = 0;
	battery_process();
	g_hw.now_ms += 1999;
	battery_process();
	ASSERT_EQ(100, battery_get_percent());

	g_hw.now_ms += 2;
	battery_process();
	ASSERT_EQ(0, battery_get_percent());

	return 1;
}

void test_battery_run(void)
{
	RUN_TEST(test_battery_placeholder_until_first_reading);
	RUN_TEST(test_battery_reads_full_when_charged);
	RUN_TEST(test_battery_ignores_voltage_sag_under_load);
}
