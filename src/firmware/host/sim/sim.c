// Ride simulator: runs the real BBSHD control logic (app.c) and sensor code
// (bbsx/sensors.c) against a scripted ride, and writes what the firmware
// asked the motor for.
//
//   ride_sim <scenario.sim> <out.csv>
//
// Timing matches the controller: the sensor interrupt runs every 100us,
// the clock ticks every 1ms, and main.c's processing (battery, sensors,
// app) runs every 5ms.
//
// Bike model (deliberately simple, see "Limits" in BUILDING.md):
// - The crank turns at the scripted cadence. PAS1/PAS2 are generated as a
//   quadrature pair, PAS_PULSES_REVOLUTION pulses per crank revolution.
// - While pedalling, the wheel turns at cadence x gear; otherwise it
//   coasts down at a fixed deceleration. Motor power does not feed back
//   into speed or cadence: the rider's cadence is an input, not a result.
// - Temperature reads as 0 C (no thermal limiting).

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "scenario.h"
#include "fake_hw.h"
#include "bbsx/stc15.h"

#include "app.h"
#include "battery.h"
#include "cfgstore.h"
#include "eventlog.h"
#include "fwconfig.h"
#include "sensors.h"
#include "throttle.h"
#include "util.h"

#define ISR_PERIOD_US			100
#define APP_PROCESS_INTERVAL_MS	5		// main.c
#define BOOT_MS					1000	// fake_hw_reset() starts the clock here

// bbsx/sensors.c; on the controller bbsx/timers.c calls it every 100us
extern void sensors_timer0_isr();

#define PIN_PAS1		P4_5
#define PIN_PAS2		P4_6
#define PIN_SPEED		P2_2
#define PIN_BRAKE		P2_4	// active low

// ---------------------------------------------------------------------------
// config overrides

typedef struct { const char* name; size_t offset; } u8_field_t;
typedef struct { const char* name; size_t lo; size_t hi; } u16_field_t;

#define U8(f)	{ #f, offsetof(config_t, f) }
#define U16(f)	{ #f, offsetof(config_t, f##_u16l), offsetof(config_t, f##_u16h) }

static const u8_field_t u8_fields[] =
{
	U8(max_current_amps), U8(current_ramp_amps_s), U8(low_cut_off_v), U8(max_speed_kph),
	U8(use_speed_sensor), U8(use_shift_sensor), U8(use_push_walk), U8(use_temperature_sensor),
	U8(lights_mode), U8(use_pretension), U8(pretension_speed_cutoff_kph), U8(speed_sensor_signals),
	U8(pas_start_delay_pulses), U8(pas_stop_delay_x100s), U8(pas_keep_current_percent),
	U8(pas_keep_current_cadence_rpm), U8(throttle_start_percent), U8(throttle_global_spd_lim_opt),
	U8(throttle_global_spd_lim_percent), U8(shift_interrupt_current_threshold_percent),
	U8(assist_mode_select), U8(assist_startup_level),
};

static const u16_field_t u16_fields[] =
{
	U16(max_battery_x100v), U16(wheel_size_inch_x10), U16(throttle_start_voltage_mv),
	U16(throttle_end_voltage_mv), U16(shift_interrupt_duration_ms),
};

static const u8_field_t level_fields[] =
{
	{ "flags", offsetof(assist_level_t, flags) },
	{ "target_current_percent", offsetof(assist_level_t, target_current_percent) },
	{ "max_throttle_current_percent", offsetof(assist_level_t, max_throttle_current_percent) },
	{ "max_cadence_percent", offsetof(assist_level_t, max_cadence_percent) },
	{ "max_speed_percent", offsetof(assist_level_t, max_speed_percent) },
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static bool apply_override(const scenario_t* s, const config_override_t* o)
{
	uint8_t* cfg = (uint8_t*)&g_config;

	if (o->level >= 0)
	{
		uint8_t* level = (uint8_t*)&g_config.assist_levels[OPERATION_MODE_DEFAULT][o->level];
		for (size_t i = 0; i < COUNT(level_fields); ++i)
		{
			if (strcmp(o->field, level_fields[i].name) == 0)
			{
				level[level_fields[i].offset] = (uint8_t)o->value;
				return true;
			}
		}
	}
	else
	{
		for (size_t i = 0; i < COUNT(u8_fields); ++i)
		{
			if (strcmp(o->field, u8_fields[i].name) == 0)
			{
				cfg[u8_fields[i].offset] = (uint8_t)o->value;
				return true;
			}
		}
		for (size_t i = 0; i < COUNT(u16_fields); ++i)
		{
			if (strcmp(o->field, u16_fields[i].name) == 0)
			{
				cfg[u16_fields[i].lo] = (uint8_t)o->value;
				cfg[u16_fields[i].hi] = (uint8_t)(o->value >> 8);
				return true;
			}
		}
	}

	fprintf(stderr, "%s:%d: unknown config field '%s'\n", s->path, o->line, o->field);
	return false;
}

// ---------------------------------------------------------------------------
// rider inputs, with linear ramps

typedef struct
{
	double value;
	double ramp_from;
	double ramp_to;
	uint32_t ramp_start_ms;
	uint32_t ramp_ms;	// 0 = not ramping
} input_state_t;

static input_state_t inputs[INPUT_COUNT];

static void input_set(input_t input, double value, uint32_t now_ms, uint32_t over_ms)
{
	input_state_t* in = &inputs[input];
	if (over_ms == 0)
	{
		in->value = value;
		in->ramp_ms = 0;
	}
	else
	{
		in->ramp_from = in->value;
		in->ramp_to = value;
		in->ramp_start_ms = now_ms;
		in->ramp_ms = over_ms;
	}
}

static void inputs_update(uint32_t now_ms)
{
	for (int i = 0; i < INPUT_COUNT; ++i)
	{
		input_state_t* in = &inputs[i];
		if (in->ramp_ms > 0)
		{
			uint32_t elapsed = now_ms - in->ramp_start_ms;
			if (elapsed >= in->ramp_ms)
			{
				in->value = in->ramp_to;
				in->ramp_ms = 0;
			}
			else
			{
				in->value = in->ramp_from + (in->ramp_to - in->ramp_from) * elapsed / in->ramp_ms;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// bike model

typedef struct
{
	double pas_duty;		// fraction of each PAS pulse period PAS1 is high
	double coast_decel;		// m/s^2 when not driven by the pedals
	double speed_pulse;		// fraction of a revolution the speed magnet reads high
	double battery_r;		// ohms; 0 = the voltage input is applied as-is

	double wheel_circumference_m;
	int speed_magnets;

	double crank_revs;		// cumulative, signed
	double wheel_revs;		// cumulative
	double wheel_rps;
} bike_t;

static bike_t bike;

static bool apply_bike_param(const scenario_t* s, const bike_param_t* p)
{
	if (strcmp(p->param, "pas_duty") == 0 && p->value > 0 && p->value < 1)
		bike.pas_duty = p->value;
	else if (strcmp(p->param, "coast_decel") == 0 && p->value >= 0)
		bike.coast_decel = p->value;
	else if (strcmp(p->param, "battery_r") == 0 && p->value >= 0 && p->value < 1)
		bike.battery_r = p->value;
	else
	{
		fprintf(stderr, "%s:%d: unknown or out-of-range bike parameter '%s'\n", s->path, p->line, p->param);
		return false;
	}
	return true;
}

static double frac(double x) { return x - floor(x); }

static void bike_step(double dt_s)
{
	double cadence_rpm = inputs[INPUT_CADENCE].value;

	bike.crank_revs += cadence_rpm / 60.0 * dt_s;

	// The freewheel lets the wheel outrun the pedals but not the reverse.
	double driven_rps = cadence_rpm > 0 ? cadence_rpm / 60.0 * inputs[INPUT_GEAR].value : 0;
	double coast_rps = bike.wheel_rps - bike.coast_decel / bike.wheel_circumference_m * dt_s;
	if (coast_rps < 0)
	{
		coast_rps = 0;
	}
	bike.wheel_rps = MAX(driven_rps, coast_rps);
	bike.wheel_revs += bike.wheel_rps * dt_s;

	// PAS quadrature: forwards, PAS2 reads low on PAS1's rising edge, which is
	// what sensors.c takes as "not backwards".
	double phase = frac(bike.crank_revs * PAS_PULSES_REVOLUTION);
	PIN_PAS1 = phase < bike.pas_duty;
	PIN_PAS2 = frac(phase - 0.25) < bike.pas_duty;

	PIN_SPEED = frac(bike.wheel_revs * bike.speed_magnets) < bike.speed_pulse;
	PIN_BRAKE = inputs[INPUT_BRAKE].value == 0;

	// Battery: with battery_r set, the voltage input is the resting voltage and
	// the pack sags by current x resistance. Battery current is taken to be
	// the current the firmware commanded (the motor controller's current loop
	// isn't modelled).
	double battery_a = g_hw.motor_enabled ? g_hw.motor_target_current * g_config.max_current_amps / 100.0 : 0;
	double volts = inputs[INPUT_VOLTAGE].value - battery_a * bike.battery_r;
	g_hw.battery_voltage_x10 = (uint16_t)(volts * 10 + 0.5);
	g_hw.battery_current_x10 = (uint16_t)(battery_a * 10 + 0.5);

	// throttle % -> mV across the configured range -> 8-bit ADC (5V reference).
	// Released, a hall throttle sits a little below its start voltage.
	double start_mv = EXPAND_U16(g_config.throttle_start_voltage_mv_u16h, g_config.throttle_start_voltage_mv_u16l);
	double end_mv = EXPAND_U16(g_config.throttle_end_voltage_mv_u16h, g_config.throttle_end_voltage_mv_u16l);
	double throttle = inputs[INPUT_THROTTLE].value;
	double mv = throttle > 0 ? start_mv + (end_mv - start_mv) * throttle / 100.0 : MAX(start_mv - 200, 600);
	g_hw.throttle_adc = (uint8_t)MIN(255.0, mv * 256 / 5000);
}

// ---------------------------------------------------------------------------
// recorded samples, one per app tick

typedef struct
{
	uint32_t t_ms;
	double cadence_rpm;
	double crank_deg;
	double speed_kph;
	double throttle;
	int brake;
	int level;
	double fw_cadence_rpm;
	int fw_pas_pulses;
	int fw_pedaling;
	double fw_speed_kph;
	int current_pct;
	double current_a;
	int target_speed_pct;
	int motor_on;
	double voltage_v;
	int fw_battery_pct;
} sample_t;

static sample_t* samples;
static int num_samples;

static double rpm_x10_to_kph(uint16_t rpm_x10)
{
	return rpm_x10 / 10.0 * bike.wheel_circumference_m * 60.0 / 1000.0;
}

static void record(uint32_t t_ms)
{
	sample_t* x = &samples[num_samples++];
	x->t_ms = t_ms;
	x->cadence_rpm = inputs[INPUT_CADENCE].value;
	x->crank_deg = bike.crank_revs * 360.0;
	x->speed_kph = bike.wheel_rps * bike.wheel_circumference_m * 3.6;
	x->throttle = inputs[INPUT_THROTTLE].value;
	x->brake = inputs[INPUT_BRAKE].value != 0;
	x->level = app_get_assist_level();
	x->fw_cadence_rpm = pas_get_cadence_rpm_x10() / 10.0;
	x->fw_pas_pulses = pas_get_pulse_counter();
	x->fw_pedaling = pas_is_pedaling_forwards();
	x->fw_speed_kph = rpm_x10_to_kph(speed_sensor_get_rpm_x10());
	x->current_pct = g_hw.motor_target_current;
	x->current_a = g_hw.motor_target_current * g_config.max_current_amps / 100.0;
	x->target_speed_pct = g_hw.motor_target_speed;
	x->motor_on = g_hw.motor_enabled;
	x->voltage_v = g_hw.battery_voltage_x10 / 10.0;
	x->fw_battery_pct = battery_get_percent();
}

static bool write_csv(const scenario_t* s, const char* path)
{
	FILE* f = fopen(path, "w");
	if (!f)
	{
		fprintf(stderr, "%s: cannot write\n", path);
		return false;
	}

	fprintf(f, "# %s\n", s->title[0] ? s->title : s->path);
	fprintf(f, "t_ms,cadence_rpm,crank_deg,speed_kph,throttle_pct,brake,level,"
		"fw_cadence_rpm,fw_pas_pulses,fw_pedaling,fw_speed_kph,"
		"current_pct,current_a,target_speed_pct,motor_on,voltage_v,fw_battery_pct\n");

	for (int i = 0; i < num_samples; ++i)
	{
		const sample_t* x = &samples[i];
		fprintf(f, "%u,%.1f,%.1f,%.2f,%.0f,%d,%d,%.1f,%d,%d,%.2f,%d,%.2f,%d,%d,%.1f,%d\n",
			x->t_ms, x->cadence_rpm, x->crank_deg, x->speed_kph, x->throttle, x->brake, x->level,
			x->fw_cadence_rpm, x->fw_pas_pulses, x->fw_pedaling, x->fw_speed_kph,
			x->current_pct, x->current_a, x->target_speed_pct, x->motor_on, x->voltage_v, x->fw_battery_pct);
	}

	fclose(f);
	return true;
}

// ---------------------------------------------------------------------------
// summary: how assist responds each time the rider starts and stops pedalling

static void print_response_summary(void)
{
	printf("  pedalling     start->assist   crank turned   start->90%% peak   peak     stop->zero\n");

	int i = 0;
	while (i < num_samples)
	{
		// find the next start of forward pedalling
		while (i < num_samples && samples[i].cadence_rpm <= 0)
		{
			i++;
		}
		if (i >= num_samples)
		{
			break;
		}

		int start = i;
		while (i < num_samples && samples[i].cadence_rpm > 0)
		{
			i++;
		}
		int stop = i; // first sample after pedalling stopped (or end)

		int peak = 0;
		for (int k = start; k < stop; ++k)
		{
			peak = MAX(peak, samples[k].current_pct);
		}

		int onset = -1, rise = -1;
		for (int k = start; k < stop; ++k)
		{
			if (onset < 0 && samples[k].current_pct > 0)
				onset = k;
			if (rise < 0 && peak > 0 && samples[k].current_pct * 10 >= peak * 9)
				rise = k;
		}

		int release = -1;
		for (int k = stop; k < num_samples && samples[k].cadence_rpm <= 0; ++k)
		{
			if (samples[k].current_pct == 0)
			{
				release = k;
				break;
			}
		}

		printf("  %5u-%-6u ", samples[start].t_ms, stop < num_samples ? samples[stop].t_ms : samples[num_samples - 1].t_ms);

		if (onset >= 0)
			printf("  %6u ms      %6.0f deg   ", samples[onset].t_ms - samples[start].t_ms,
				samples[onset].crank_deg - samples[start].crank_deg);
		else
			printf("  %9s      %10s   ", "none", "-");

		if (rise >= 0)
			printf("  %8u ms     %3d%% %4.1fA", samples[rise].t_ms - samples[start].t_ms,
				peak, peak * g_config.max_current_amps / 100.0);
		else
			printf("  %11s     %10s", "-", "-");

		if (stop >= num_samples)
			printf("   (still pedalling)\n");
		else if (release >= 0)
			printf("   %6u ms\n", samples[release].t_ms - samples[stop].t_ms);
		else
			printf("   %9s\n", "never");
	}
}

static bool check_expects(const scenario_t* s)
{
	bool ok = true;

	for (int e = 0; e < s->num_expects; ++e)
	{
		const expect_t* x = &s->expects[e];

		// the last sample at or before the expected time
		const sample_t* at = NULL;
		for (int i = 0; i < num_samples && samples[i].t_ms <= x->at_ms; ++i)
		{
			at = &samples[i];
		}
		if (!at)
		{
			printf("  FAIL %s:%d: no sample at %u ms\n", s->path, x->line, x->at_ms);
			ok = false;
			continue;
		}

		double actual = 0;
		switch (x->output)
		{
		case OUTPUT_CURRENT: actual = at->current_pct; break;
		case OUTPUT_MOTOR: actual = at->motor_on; break;
		case OUTPUT_FW_CADENCE: actual = at->fw_cadence_rpm; break;
		}

		bool pass = false;
		switch (x->op)
		{
		case OP_EQ: pass = actual == x->value; break;
		case OP_NE: pass = actual != x->value; break;
		case OP_LT: pass = actual < x->value; break;
		case OP_LE: pass = actual <= x->value; break;
		case OP_GT: pass = actual > x->value; break;
		case OP_GE: pass = actual >= x->value; break;
		}

		if (!pass)
		{
			printf("  FAIL %s:%d: at %u ms expected %s %s %g, got %g\n", s->path, x->line,
				x->at_ms, output_name(x->output), op_name(x->op), x->value, actual);
			ok = false;
		}
	}

	if (s->num_expects > 0 && ok)
	{
		printf("  %d expectation(s) met\n", s->num_expects);
	}

	return ok;
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
	if (argc != 3)
	{
		fprintf(stderr, "usage: %s <scenario.sim> <out.csv>\n", argv[0]);
		return 2;
	}

	static scenario_t s;
	if (!scenario_load(argv[1], &s))
	{
		return 2;
	}

	// --- boot, in main.c's order (motor, uart and lights init have nothing to simulate) ---

	fake_hw_reset();
	eventlog_init(false);
	cfgstore_init();

	for (int i = 0; i < s.num_overrides; ++i)
	{
		if (!apply_override(&s, &s.overrides[i]))
		{
			return 2;
		}
	}

	bike.pas_duty = 0.5;
	bike.coast_decel = 0.3;
	bike.speed_pulse = 0.05;
	bike.wheel_circumference_m = EXPAND_U16(g_config.wheel_size_inch_x10_u16h, g_config.wheel_size_inch_x10_u16l)
		/ 10.0 * 0.0254 * 3.14159265358979;
	bike.speed_magnets = MAX(1, g_config.speed_sensor_signals);

	for (int i = 0; i < s.num_bike_params; ++i)
	{
		if (!apply_bike_param(&s, &s.bike_params[i]))
		{
			return 2;
		}
	}

	inputs[INPUT_GEAR].value = 2.0;
	inputs[INPUT_VOLTAGE].value = 52.0;
	inputs[INPUT_LEVEL].value = g_config.assist_startup_level;

	bike_step(0);
	sensors_init();
	speed_sensor_set_signals_per_rpm(g_config.speed_sensor_signals);
	pas_set_stop_delay((uint16_t)g_config.pas_stop_delay_x100s * 10);

	battery_init();
	throttle_init(
		EXPAND_U16(g_config.throttle_start_voltage_mv_u16h, g_config.throttle_start_voltage_mv_u16l),
		EXPAND_U16(g_config.throttle_end_voltage_mv_u16h, g_config.throttle_end_voltage_mv_u16l)
	);

	app_init();

	// --- run ---

	samples = calloc(s.end_ms / APP_PROCESS_INTERVAL_MS + 2, sizeof(sample_t));
	num_samples = 0;

	int next_event = 0;
	uint32_t next_app_process = g_hw.now_ms;
	const int isr_per_ms = 1000 / ISR_PERIOD_US;

	for (uint32_t t_ms = 0; t_ms <= s.end_ms; ++t_ms)
	{
		g_hw.now_ms = BOOT_MS + t_ms;

		while (next_event < s.num_events && s.events[next_event].at_ms <= t_ms)
		{
			const event_t* e = &s.events[next_event++];
			input_set(e->input, e->value, t_ms, e->over_ms);
		}
		inputs_update(t_ms);
		bike_step(0);	// apply this millisecond's inputs to the pins before the firmware looks

		uint8_t level = (uint8_t)inputs[INPUT_LEVEL].value;
		if (level != app_get_assist_level())
		{
			app_set_assist_level(level);
		}

		// main loop work, as in main.c
		if (g_hw.now_ms >= next_app_process)
		{
			next_app_process = g_hw.now_ms + APP_PROCESS_INTERVAL_MS;

			battery_process();
			sensors_process();
			app_process();
			record(t_ms);
		}

		for (int k = 0; k < isr_per_ms; ++k)
		{
			bike_step(ISR_PERIOD_US / 1e6);
			sensors_timer0_isr();
		}
	}

	// --- report ---

	printf("%s\n", s.title[0] ? s.title : s.path);
	print_response_summary();
	bool ok = check_expects(&s);

	bool written = write_csv(&s, argv[2]);
	free(samples);

	return ok && written ? 0 : 1;
}
