#ifndef _MOCK_HW_H_
#define _MOCK_HW_H_

#include <stdint.h>

// Resets all mocked hardware state to defaults. Call at the start of every test.
void mock_reset(void);

void mock_set_ms(uint32_t ms);
void mock_advance_ms(uint32_t delta);

void mock_set_throttle_adc(uint8_t value);

void mock_set_battery_voltage_x10(uint16_t value);
void mock_set_target_current(uint8_t value);

#endif
