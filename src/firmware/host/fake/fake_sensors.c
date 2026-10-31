// Sensor functions that return g_hw values directly, for unit tests.
//
// The ride simulator links the real bbsx/sensors.c instead and drives its
// input pins (see fake_pins.c), so PAS and speed timing come from real
// firmware code there.

#include "fake_hw.h"

#include "sensors.h"

#include <string.h>

bool brake_is_activated() { return g_hw.brake; }
bool shift_sensor_is_activated() { return g_hw.shift; }

bool pas_is_pedaling_forwards() { return g_hw.pas_forwards; }
bool pas_is_pedaling_backwards() { return g_hw.pas_backwards; }
uint16_t pas_get_cadence_rpm_x10() { return g_hw.pas_cadence_rpm_x10; }
uint16_t pas_get_pulse_counter() { return g_hw.pas_pulse_counter; }

uint16_t speed_sensor_get_rpm_x10() { return g_hw.wheel_rpm_x10; }
bool speed_sensor_is_moving() { return g_hw.wheel_rpm_x10 > 0; }

bool torque_sensor_ok() { return true; }
uint16_t torque_sensor_get_nm_x100() { return 0; }

int16_t temperature_contr_x100() { return g_hw.temperature_contr_x100; }
int16_t temperature_motor_x100() { return g_hw.temperature_motor_x100; }


// Hands out the edges a test queued in g_hw.pas_edges (most tests feed
// effort.c directly with effort_add_edge() instead).
uint8_t pas_read_edges(uint16_t* dst, uint8_t max, bool* overflow)
{
	uint8_t n = 0;
	while (n < max && n < g_hw.pas_edge_count)
	{
		dst[n] = g_hw.pas_edges[n];
		n++;
	}
	g_hw.pas_edge_count -= n;
	memmove(g_hw.pas_edges, g_hw.pas_edges + n, g_hw.pas_edge_count * sizeof(uint16_t));

	*overflow = g_hw.pas_edge_overflow;
	g_hw.pas_edge_overflow = false;
	return n;
}
