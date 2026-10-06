// Host-side stand-in for the controller hardware.
//
// Implements every hardware function the logic modules (app.c, throttle.c,
// battery.c, cfgstore.c, eventlog.c) call, backed by one plain struct.
// Tests and the ride simulator set the inputs directly and read back what
// the firmware commanded.

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
	bool brake;
	bool shift;

	bool pas_forwards;
	bool pas_backwards;
	uint16_t pas_cadence_rpm_x10;
	uint16_t pas_pulse_counter;

	uint16_t wheel_rpm_x10;

	uint16_t battery_voltage_x10;
	uint16_t battery_current_x10;
	uint16_t motor_status;

	int16_t temperature_contr_x100;
	int16_t temperature_motor_x100;

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
