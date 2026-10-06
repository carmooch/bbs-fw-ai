#include "test.h"
#include "fake_hw.h"
#include "app.h"
#include "cfgstore.h"
#include "throttle.h"

// main.c runs app_process() every 5ms.
#define APP_TICK_MS 5

// Boots with the default BBSHD config: assist level 3 = PAS + throttle,
// 14% target current, ramping up at 10A/s of a 30A max (30ms per 1%).
static void boot(void)
{
	fake_hw_reset();
	cfgstore_init();
	throttle_init(1000, 3600);
	app_init();
}

static void run_ms(uint32_t ms)
{
	for (uint32_t t = 0; t < ms; t += APP_TICK_MS)
	{
		g_hw.now_ms += APP_TICK_MS;
		app_process();
	}
}

static void pedal(uint16_t cadence_rpm)
{
	g_hw.pas_forwards = true;
	g_hw.pas_cadence_rpm_x10 = cadence_rpm * 10;
	g_hw.pas_pulse_counter = 100;
}

static void stop_pedalling(void)
{
	g_hw.pas_forwards = false;
	g_hw.pas_cadence_rpm_x10 = 0;
	g_hw.pas_pulse_counter = 0;
}

static int test_app_no_power_when_parked(void)
{
	boot();
	run_ms(1000);

	ASSERT_EQ(0, g_hw.motor_target_current);
	ASSERT_TRUE(!g_hw.motor_enabled);

	return 1;
}

static int test_app_pas_waits_for_start_delay_pulses(void)
{
	boot();

	// The default start delay is 5 pulses; at 5 there's still no assist.
	g_hw.pas_forwards = true;
	g_hw.pas_cadence_rpm_x10 = 600;
	g_hw.pas_pulse_counter = 5;
	run_ms(500);
	ASSERT_EQ(0, g_hw.motor_target_current);

	g_hw.pas_pulse_counter = 6;
	run_ms(APP_TICK_MS);
	ASSERT_TRUE(g_hw.motor_target_current > 0);
	ASSERT_TRUE(g_hw.motor_enabled);

	return 1;
}

static int test_app_pas_ramps_up_to_level_current(void)
{
	boot();
	pedal(60);

	// 30ms per percent: halfway through the ramp after ~210ms...
	run_ms(210);
	ASSERT_TRUE(g_hw.motor_target_current >= 6 && g_hw.motor_target_current <= 8);

	// ...and at the level's 14% after ~420ms, where it holds.
	run_ms(300);
	ASSERT_EQ(14, g_hw.motor_target_current);
	run_ms(1000);
	ASSERT_EQ(14, g_hw.motor_target_current);

	return 1;
}

static int test_app_power_drops_when_pedalling_stops(void)
{
	boot();
	pedal(60);
	run_ms(1000);
	ASSERT_EQ(14, g_hw.motor_target_current);

	stop_pedalling();
	run_ms(100);
	ASSERT_EQ(0, g_hw.motor_target_current);
	ASSERT_TRUE(!g_hw.motor_enabled);

	return 1;
}

static int test_app_brake_cuts_power_immediately(void)
{
	boot();
	pedal(60);
	run_ms(1000);
	ASSERT_EQ(14, g_hw.motor_target_current);

	g_hw.brake = true;
	run_ms(APP_TICK_MS);
	ASSERT_EQ(0, g_hw.motor_target_current);

	return 1;
}

void test_app_run(void)
{
	RUN_TEST(test_app_no_power_when_parked);
	RUN_TEST(test_app_pas_waits_for_start_delay_pulses);
	RUN_TEST(test_app_pas_ramps_up_to_level_current);
	RUN_TEST(test_app_power_drops_when_pedalling_stops);
	RUN_TEST(test_app_brake_cuts_power_immediately);
}
