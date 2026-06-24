// Host-side stand-ins for the hardware-dependent functions that the
// pure-logic firmware modules (throttle.c, battery.c, ...) call into.
// Only the symbols those modules actually reference are provided here.

#include "mock_hw.h"
#include "cfgstore.h"

// g_config / g_pstate are now provided by the real cfgstore.c, which is
// part of the test build (see test_cfgstore.c).

static uint32_t mock_ms;
static uint8_t mock_throttle_adc;
static uint16_t mock_battery_voltage_x10;
static uint8_t mock_target_current;

void mock_reset(void)
{
	mock_ms = 0;
	mock_throttle_adc = 0;
	mock_battery_voltage_x10 = 0;
	mock_target_current = 0;

	config_t zero = { 0 };
	g_config = zero;
}

void mock_set_ms(uint32_t ms) { mock_ms = ms; }
void mock_advance_ms(uint32_t delta) { mock_ms += delta; }

void mock_set_throttle_adc(uint8_t value) { mock_throttle_adc = value; }

void mock_set_battery_voltage_x10(uint16_t value) { mock_battery_voltage_x10 = value; }
void mock_set_target_current(uint8_t value) { mock_target_current = value; }

uint32_t system_ms(void)
{
	return mock_ms;
}

uint8_t adc_get_throttle(void)
{
	return mock_throttle_adc;
}

void eventlog_write(uint8_t evt)
{
	(void)evt;
}

void eventlog_write_data(uint8_t evt, int16_t data)
{
	(void)evt;
	(void)data;
}

uint16_t motor_get_battery_voltage_x10(void)
{
	return mock_battery_voltage_x10;
}

uint8_t motor_get_target_current(void)
{
	return mock_target_current;
}
