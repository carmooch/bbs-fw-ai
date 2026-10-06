/*
 * bbs-fw
 *
 * Copyright (C) Daniel Nilsson, 2022.
 *
 * Released under the GPL License, Version 3
 */

#ifndef _CFGSTORE_H_
#define _CFGSTORE_H_

#include "intellisense.h"
#include <stdint.h>
#include <stdbool.h>

#define ASSIST_FLAG_PAS					0x01
#define ASSIST_FLAG_THROTTLE			0x02
#define ASSIST_FLAG_CRUISE				0x04
#define ASSIST_FLAG_PAS_VARIABLE		0x08	// pas mode using throttle to set power level
#define ASSIST_FLAG_PAS_TORQUE			0x10	// pas mode using torque sensor reading
#define ASSIST_FLAG_OVERRIDE_CADENCE	0x20	// pas option where max cadence is set to 100% when throttle overrides pas
#define ASSIST_FLAG_OVERRIDE_SPEED		0x40	// pas option where max speed is set to 100% when throttle overrides pas
#define ASSIST_FLAG_PAS_POWER			0x80	// pas mode targeting power from cadence, see assist_level_ext_t

#define ASSIST_MODE_SELECT_OFF			0x00
#define ASSIST_MODE_SELECT_STANDARD		0x01
#define ASSIST_MODE_SELECT_LIGHTS		0x02
#define ASSIST_MODE_SELECT_PAS0_LIGHT	0x03
#define ASSIST_MODE_SELECT_PAS1_LIGHT	0x04
#define ASSIST_MODE_SELECT_PAS2_LIGHT	0x05
#define ASSIST_MODE_SELECT_PAS3_LIGHT	0x06
#define ASSIST_MODE_SELECT_PAS4_LIGHT	0x07
#define ASSIST_MODE_SELECT_PAS5_LIGHT	0x08
#define ASSIST_MODE_SELECT_PAS6_LIGHT	0x09
#define ASSIST_MODE_SELECT_PAS7_LIGHT	0x0A
#define ASSIST_MODE_SELECT_PAS8_LIGHT	0x0B
#define ASSIST_MODE_SELECT_PAS9_LIGHT	0x0C
#define ASSIST_MODE_SELECT_BRAKE_BOOT	0x0D


#define TEMPERATURE_SENSOR_CONTR		0x01
#define TEMPERATURE_SENSOR_MOTOR		0x02

#define WALK_MODE_DATA_SPEED			0
#define WALK_MODE_DATA_TEMPERATURE		1
#define WALK_MODE_DATA_REQUESTED_POWER	2
#define WALK_MODE_DATA_BATTERY_PERCENT	3

#define THROTTLE_GLOBAL_SPEED_LIMIT_DISABLED	0
#define THROTTLE_GLOBAL_SPEED_LIMIT_ENABLED		1
#define THROTTLE_GLOBAL_SPEED_LIMIT_STD_LVLS	2

#define LIGHTS_MODE_DEFAULT				0
#define LIGHTS_MODE_DISABLED			1
#define LIGHTS_MODE_ALWAYS_ON			2
#define LIGHTS_MODE_BRAKE_LIGHT			3

#define CONFIG_VERSION					6
#define PSTATE_VERSION					1


typedef struct
{
	uint8_t flags;
	uint8_t target_current_percent;
	uint8_t max_throttle_current_percent;
	uint8_t max_cadence_percent;
	uint8_t max_speed_percent;

	// 10 => 1.0: 100w human power gives and additional 100w motor power
	uint8_t torque_amplification_factor_x10;
}  assist_level_t;

// Per assist level settings added in config version 6. They live in their own
// array at the end of config_t so the version 5 layout stays a prefix of
// version 6, which lets cfgstore migrate a version 5 config in place.
typedef struct
{
	// ASSIST_FLAG_PAS_POWER: target power (W) =
	//   power_start_w_div10 * 10 + power_w_per_rpm_x10 / 10 * cadence (rpm),
	// turned into current using the battery voltage and capped at the level's
	// target_current_percent.
	uint8_t power_start_w_div10;
	uint8_t power_w_per_rpm_x10;

	// current ramp up rate for this level in A/s, 0 = use current_ramp_amps_s
	uint8_t current_ramp_amps_s;
} assist_level_ext_t;

// SDCC uses little endian for MCS51 and big endian for STM8...
typedef struct
{
	// hmi units
	uint8_t use_freedom_units;

	// global
	uint8_t max_current_amps;
	uint8_t current_ramp_amps_s;
	uint8_t max_battery_x100v_u16l;
	uint8_t max_battery_x100v_u16h;
	uint8_t low_cut_off_v;
	uint8_t max_speed_kph;

	// externals
	uint8_t use_speed_sensor;
	uint8_t use_shift_sensor;
	uint8_t use_push_walk;
	uint8_t use_temperature_sensor;
	uint8_t lights_mode;
	uint8_t use_pretension;
	uint8_t pretension_speed_cutoff_kph;

	// speed sensor
	uint8_t wheel_size_inch_x10_u16l;
	uint8_t wheel_size_inch_x10_u16h;
	uint8_t speed_sensor_signals;

	// pas options
	uint8_t pas_start_delay_pulses;
	uint8_t pas_stop_delay_x100s;
	uint8_t pas_keep_current_percent;
	uint8_t pas_keep_current_cadence_rpm;

	// throttle options
	uint8_t throttle_start_voltage_mv_u16l;
	uint8_t throttle_start_voltage_mv_u16h;
	uint8_t throttle_end_voltage_mv_u16l;
	uint8_t throttle_end_voltage_mv_u16h;
	uint8_t throttle_start_percent;
	uint8_t throttle_global_spd_lim_opt;
	uint8_t throttle_global_spd_lim_percent;

	// shift interrupt options
	uint8_t shift_interrupt_duration_ms_u16l;
	uint8_t shift_interrupt_duration_ms_u16h;
	uint8_t shift_interrupt_current_threshold_percent;

	// misc
	uint8_t walk_mode_data_display;

	// assist levels
	uint8_t assist_mode_select;
	uint8_t assist_startup_level;
	assist_level_t assist_levels[2][10];

	// --- added in config version 6, see CONFIG_V5_SIZE ---

	// 1: treat pedalling as stopped once the next PAS pulse is overdue
	// (twice the last pulse period), instead of only after the stop delay
	uint8_t pas_stop_predictive;

	// PAS start delay (pulses) when the bike is already rolling
	uint8_t pas_start_delay_pulses_rolling;

	// current ramp up rate (A/s) below LAUNCH_SPEED_KPH, 0 = off
	uint8_t launch_ramp_amps_s;

	// extra PAS assist in low gears: gear is the wheel/crank revolution ratio
	// from the speed and PAS sensors; full boost at or below gear_ratio_low,
	// none at or above gear_ratio_high. 0 = off
	uint8_t gear_boost_max_percent;
	uint8_t gear_ratio_low_x10;
	uint8_t gear_ratio_high_x10;

	// cap the motor's speed at the pedals' cadence + this margin (rpm), so it
	// can't run ahead of the rider. 0 = off
	uint8_t cadence_lock_margin_rpm;

	assist_level_ext_t assist_level_ext[2][10];
} config_t;

// Size of a version 5 config: everything before the version 6 additions.
#define CONFIG_V5_SIZE		154

typedef struct
{
	uint8_t adc_voltage_calibration_steps_x100_i16l;
	uint8_t adc_voltage_calibration_steps_x100_i16h;
} pstate_t;


extern config_t g_config;
extern pstate_t g_pstate;

void cfgstore_init();

bool cfgstore_reset_config();
bool cfgstore_save_config();

bool cfgstore_reset_pstate();
bool cfgstore_save_pstate();

#endif
