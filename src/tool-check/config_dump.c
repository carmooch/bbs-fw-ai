// Writes a config_t full of distinctive values to stdout, so the C# config
// tool can be checked against the firmware's byte layout (see check.sh).

#include "cfgstore.h"
#include <stdio.h>
#include <string.h>
int main(void)
{
	config_t c;
	memset(&c, 0, sizeof(c));
	c.max_current_amps = 33; c.current_ramp_amps_s = 12;
	c.max_battery_x100v_u16l = (uint8_t)5880; c.max_battery_x100v_u16h = 5880 >> 8;
	c.low_cut_off_v = 42; c.pas_start_delay_pulses = 5; c.pas_stop_delay_x100s = 20;
	c.wheel_size_inch_x10_u16l = (uint8_t)275; c.wheel_size_inch_x10_u16h = 275 >> 8;
	c.assist_levels[0][3].flags = ASSIST_FLAG_PAS | ASSIST_FLAG_PAS_POWER;
	c.assist_levels[0][3].target_current_percent = 60;
	c.assist_levels[1][9].max_speed_percent = 77;
	c.pas_stop_predictive = 1; c.pas_start_delay_pulses_rolling = 1; c.launch_ramp_amps_s = 30;
	c.gear_boost_max_percent = 50; c.gear_ratio_low_x10 = 12; c.gear_ratio_high_x10 = 30;
	c.cadence_lock_margin_rpm = 15;
	c.assist_level_ext[0][3].power_start_w_div10 = 10; c.assist_level_ext[0][3].power_w_per_rpm_x10 = 20;
	c.assist_level_ext[0][3].current_ramp_amps_s = 5; c.assist_level_ext[1][9].power_w_per_rpm_x10 = 99;
	fwrite(&c, sizeof(c), 1, stdout);
	return 0;
}
