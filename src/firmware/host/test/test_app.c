#include "test.h"
#include "fake_hw.h"
#include "app.h"
#include "cfgstore.h"
#include "throttle.h"
#include "effort.h"
#include "sensors.h"

// main.c runs app_process() every 5ms.
#define APP_TICK_MS 5

// Boots with the default BBSHD config: assist level 3 = PAS + throttle,
// 14% target current, ramping up at 10A/s of a 30A max (30ms per 1%).
static void boot(void)
{
	fake_hw_reset();
	cfgstore_init();
	throttle_init(1000, 3600);
	effort_init();
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

// A standing start as the PAS sensor sees it: a long gap, then forward edges
// (PAS1 rise, both, PAS2, neither) every ticks_per_edge 100us ticks, the
// bike standing until then.
static uint8_t launch_state;
static void launch_edges(int n, uint16_t ticks_per_edge, bool first)
{
	static const uint16_t bits[4] = { PAS_EDGE_PAS1, PAS_EDGE_PAS1 | PAS_EDGE_PAS2, PAS_EDGE_PAS2, 0 };
	effort_set_wheel_rpm_x10(0);
	if (first)
	{
		effort_add_edge(bits[3]);	// where the pedals stood
		effort_add_edge(PAS_EDGE_TICKS_MASK | bits[0]);
		launch_state = 1;
	}
	for (int i = 0; i < n; ++i)
	{
		g_hw.now_ms += ticks_per_edge / 10;
		effort_add_edge(ticks_per_edge | bits[launch_state++ & 3]);
	}
}

static int test_app_launch_sensing_starts_assist_capped_by_the_level(void)
{
	boot();
	g_config.launch_sensing = 1;
	g_config.assist_effort_x10[0][3] = 15;
	g_config.assist_levels[0][3].target_current_percent = 20;
	app_set_assist_level(0);	// (level 3 is already set: switch to reload its settings)
	app_set_assist_level(3);
	run_ms(100);

	// a hard push from a standstill: the pedal pulse count hasn't reached the
	// start delay, so plain PAS wouldn't help yet
	g_hw.pas_forwards = true;
	g_hw.pas_pulse_counter = 1;
	launch_edges(4, 300, true);	// two pulses, the second quicker: speeding up
	launch_edges(4, 200, false);
	ASSERT_EQ(0, g_hw.motor_target_current);
	for (int i = 0; i < 12; ++i)
	{
		launch_edges(1, 200, false);
		run_ms(50);
	}
	ASSERT_TRUE(g_hw.motor_target_current > 0);

	// keeps pushing hard: up to the level's cap and no further
	for (int i = 0; i < 20; ++i)
	{
		launch_edges(1, 200, false);
		run_ms(50);
	}
	ASSERT_EQ(20, g_hw.motor_target_current);

	return 1;
}

static int test_app_no_launch_sensing_unless_switched_on(void)
{
	boot();
	g_config.assist_effort_x10[0][3] = 15;
	app_set_assist_level(0);	// (level 3 is already set: switch to reload its settings)
	app_set_assist_level(3);
	run_ms(100);

	g_hw.pas_forwards = true;
	g_hw.pas_pulse_counter = 1;
	launch_edges(4, 300, true);
	launch_edges(4, 200, false);
	for (int i = 0; i < 12; ++i)
	{
		launch_edges(1, 200, false);
		run_ms(50);
	}
	ASSERT_EQ(0, g_hw.motor_target_current);

	return 1;
}

// the wheel sensor shows the cranks drifting off the gear: freewheeling legs
static void make_freewheeling(void)
{
	effort_add_wheel_pulse(960);
	effort_add_wheel_pulse(960);
	effort_add_wheel_pulse(700);
	effort_add_wheel_pulse(690);
}

static int test_app_no_effort_assist_for_freewheeling_legs(void)
{
	boot();
	g_config.assist_effort_x10[0][3] = 15;
	app_set_assist_level(0);
	app_set_assist_level(3);
	make_freewheeling();
	ASSERT_TRUE(effort_is_freewheeling());

	pedal(60);
	run_ms(1500);
	ASSERT_EQ(0, g_hw.motor_target_current);

	return 1;
}

static int test_app_freewheeling_leaves_other_levels_alone(void)
{
	boot();
	app_set_assist_level(3);	// no effort assist on it: plain PAS, 14%
	make_freewheeling();
	ASSERT_TRUE(effort_is_freewheeling());

	pedal(60);
	run_ms(1500);
	ASSERT_EQ(14, g_hw.motor_target_current);

	return 1;
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

// 52V (14s) pack: 58.8V full, 42V cutoff. The low-voltage limit eases power
// off between ~44.8V and ~43.3V. Level 9 asks for more than that limit allows.
static void boot_52v_level_9(void)
{
	fake_hw_reset();
	cfgstore_init();
	g_config.max_battery_x100v_u16l = (uint8_t)5880;
	g_config.max_battery_x100v_u16h = (uint8_t)(5880 >> 8);
	g_config.low_cut_off_v = 42;
	throttle_init(1000, 3600);
	app_init();
	app_set_assist_level(9);

	g_hw.battery_voltage_x10 = 480;
	g_hw.battery_current_x10 = 200;
	pedal(70);

	// the voltage filter starts high and settles over a few seconds
	run_ms(8000);
}

static int test_app_low_voltage_limit_recovers_under_load(void)
{
	boot_52v_level_9();
	uint8_t full = g_hw.motor_target_current;
	ASSERT_TRUE(full > 50);

	// a 3 s sag on a climb limits power...
	g_hw.battery_voltage_x10 = 440;
	run_ms(3000);
	ASSERT_TRUE(g_hw.motor_target_current < full);

	// ...and power comes back once the sag is over
	g_hw.battery_voltage_x10 = 480;
	run_ms(3000);
	ASSERT_EQ(full, g_hw.motor_target_current);

	return 1;
}

static int test_app_low_voltage_limit_holds_without_load(void)
{
	boot_52v_level_9();
	uint8_t full = g_hw.motor_target_current;

	g_hw.battery_voltage_x10 = 440;
	run_ms(3000);
	uint8_t limited = g_hw.motor_target_current;
	ASSERT_TRUE(limited < full);

	// The voltage recovers but the battery isn't supplying current (as when
	// stopped): the limit holds, so the next pull-away can't surge.
	g_hw.battery_voltage_x10 = 480;
	g_hw.battery_current_x10 = 0;
	run_ms(5000);
	ASSERT_EQ(limited, g_hw.motor_target_current);

	return 1;
}

void test_app_run(void)
{
	RUN_TEST(test_app_low_voltage_limit_recovers_under_load);
	RUN_TEST(test_app_low_voltage_limit_holds_without_load);
	RUN_TEST(test_app_no_power_when_parked);
	RUN_TEST(test_app_pas_waits_for_start_delay_pulses);
	RUN_TEST(test_app_pas_ramps_up_to_level_current);
	RUN_TEST(test_app_power_drops_when_pedalling_stops);
	RUN_TEST(test_app_brake_cuts_power_immediately);
	RUN_TEST(test_app_launch_sensing_starts_assist_capped_by_the_level);
	RUN_TEST(test_app_no_launch_sensing_unless_switched_on);
	RUN_TEST(test_app_no_effort_assist_for_freewheeling_legs);
	RUN_TEST(test_app_freewheeling_leaves_other_levels_alone);
}
