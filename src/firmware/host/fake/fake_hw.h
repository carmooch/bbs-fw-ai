// Host-side stand-in for the controller hardware.
//
// Implements every hardware function the logic modules (app.c, throttle.c,
// battery.c, cfgstore.c, eventlog.c) call, backed by one plain struct.
// Tests and the ride simulator set the inputs directly and read back what
// the firmware commanded.
//
// Sensors come in two flavours:
// - Unit tests link fake_sensors.c, which returns the "sensor values" below
//   (pas_*, wheel_rpm_x10, brake, ...) as-is.
// - The ride simulator links the real bbsx/sensors.c and drives its input
//   pins instead (fake_pins.c), so those fields are unused there.

#ifndef _FAKE_HW_H_
#define _FAKE_HW_H_

#include <stdint.h>
#include <stdbool.h>

#define FAKE_EEPROM_PAGES		4
#define FAKE_EEPROM_PAGE_SIZE	512

typedef struct
{
	// --- inputs ---
	uint32_t now_ms;

	uint8_t throttle_adc;

	// raw ADC counts read by the real sensors.c; 0 reads as "no sensor" (0 C)
	uint16_t adc_temperature_contr;
	uint16_t adc_temperature_motor;

	// --- sensor values (fake_sensors.c only) ---
	bool brake;
	bool shift;

	bool pas_forwards;
	bool pas_backwards;
	uint16_t pas_cadence_rpm_x10;
	uint16_t pas_pulse_counter;

	uint16_t wheel_rpm_x10;

	int16_t temperature_contr_x100;
	int16_t temperature_motor_x100;

	// --- motor controller readings ---
	uint16_t battery_voltage_x10;
	uint16_t battery_current_x10;
	uint16_t motor_status;

	// --- outputs (what the firmware commanded) ---
	bool motor_enabled;
	uint8_t motor_target_current;
	uint8_t motor_target_speed;

	bool lights_enabled;
	bool lights_on;

	// --- storage ---
	uint8_t eeprom[FAKE_EEPROM_PAGES][FAKE_EEPROM_PAGE_SIZE];
	int eeprom_page;
} fake_hw_t;

extern fake_hw_t g_hw;

// Resets all inputs to a parked, healthy bike: 48V battery, room
// temperature, no pedalling, throttle released, erased EEPROM.
void fake_hw_reset(void);

#endif
