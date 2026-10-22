/*
 * bbs-fw
 *
 * Released under the GPL License, Version 3
 */

#ifndef _EFFORT_H_
#define _EFFORT_H_

#include "intellisense.h"
#include "fwconfig.h"

#include <stdint.h>
#include <stdbool.h>

// Rider effort from PAS edge timing, see effort.c and research/effort.

#define EFFORT_EDGES_PER_REV		(PAS_PULSES_REVOLUTION * 4)

void effort_init() __reentrant;

// Drains queued PAS edges and updates the estimate. Call every app tick.
void effort_process();

// Feeds one PAS edge: ticks since the previous edge plus pin states, as
// pas_read_edges() delivers them. effort_process() calls it; tests may too.
void effort_add_edge(uint16_t entry);

// Wheel speed and its magnets don't come with the edges; effort_process()
// reads them, tests set them here.
void effort_set_wheel_rpm_x10(uint16_t rpm_x10);

// True while the estimate can be trusted: magnet pattern learned and
// aligned, pedalling forwards steadily, below effort_max_speed_kph.
bool effort_is_valid();

// Estimated rider power (W) and torque (Nm x10); 0 when not valid.
uint16_t effort_get_rider_power_w();
uint16_t effort_get_rider_torque_nm_x10();

// Magnet pattern learning progress, 0-100 (100 = learned).
uint8_t effort_get_learned_percent();

#endif
