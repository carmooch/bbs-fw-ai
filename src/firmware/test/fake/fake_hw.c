#include "fake_hw.h"

#include <string.h>

#include "adc.h"
#include "eeprom.h"
#include "lights.h"
#include "motor.h"
#include "sensors.h"
#include "system.h"
#include "uart.h"

fake_hw_t g_hw;

void fake_hw_reset(void)
{
	memset(&g_hw, 0, sizeof(g_hw));

	// Start the clock off zero: app.c and throttle.c use a timestamp of 0
	// to mean "not started".
	g_hw.now_ms = 1000;
	g_hw.battery_voltage_x10 = 520;
	g_hw.temperature_contr_x100 = 2500;
	g_hw.temperature_motor_x100 = 2500;

	memset(g_hw.eeprom, 0xff, sizeof(g_hw.eeprom));
}

// --- system ---

uint32_t system_ms() { return g_hw.now_ms; }

// --- adc ---

uint8_t adc_get_throttle() { return g_hw.throttle_adc; }

// --- sensors ---

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

// --- motor ---

void motor_enable() { g_hw.motor_enabled = true; }
void motor_disable() { g_hw.motor_enabled = false; }

uint16_t motor_status() { return g_hw.motor_status; }

uint8_t motor_get_target_current() { return g_hw.motor_target_current; }
uint8_t motor_get_target_speed() { return g_hw.motor_target_speed; }
void motor_set_target_current(uint8_t percent) { g_hw.motor_target_current = percent; }
void motor_set_target_speed(uint8_t percent) { g_hw.motor_target_speed = percent; }

uint16_t motor_get_battery_voltage_x10() { return g_hw.battery_voltage_x10; }
uint16_t motor_get_battery_current_x10() { return g_hw.battery_current_x10; }

// --- lights ---

void lights_enable() { g_hw.lights_enabled = true; }
void lights_disable() { g_hw.lights_enabled = false; }
void lights_set(bool on) { g_hw.lights_on = on; }

// --- uart (event log output goes nowhere) ---

void uart_write(uint8_t byte) { (void)byte; }

// --- eeprom: behaves like flash, erase sets 0xff and programming only clears bits ---

bool eeprom_select_page(int page)
{
	if (page < 0 || page >= FAKE_EEPROM_PAGES)
	{
		return false;
	}

	g_hw.eeprom_page = page;
	return true;
}

bool eeprom_erase_page()
{
	memset(g_hw.eeprom[g_hw.eeprom_page], 0xff, FAKE_EEPROM_PAGE_SIZE);
	return true;
}

int eeprom_read_byte(int offset)
{
	if (offset < 0 || offset >= FAKE_EEPROM_PAGE_SIZE)
	{
		return -1;
	}

	return g_hw.eeprom[g_hw.eeprom_page][offset];
}

bool eeprom_write_byte(int offset, uint8_t value)
{
	if (offset < 0 || offset >= FAKE_EEPROM_PAGE_SIZE)
	{
		return false;
	}

	g_hw.eeprom[g_hw.eeprom_page][offset] &= value;
	return true;
}

bool eeprom_end_write() { return true; }
