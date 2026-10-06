// Pin and register variables for the real bbsx/sensors.c (ride simulator only).
// See fake/bbsx/stc15.h.

#include "bbsx/stc15.h"
#include "bbsx/timers.h"

bool ET0;

// Brake and shift inputs are active-low with pull-ups, so idle is high.
bool P2_2;
bool P2_4 = true;
bool P2_6 = true;
bool P4_5;
bool P4_6;

uint8_t P2M0, P2M1, P4M0, P4M1;

void timer0_init_sensors() {}
