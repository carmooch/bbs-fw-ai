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

// Tells effort sensing whether the throttle or cruise is driving the motor.
// Then the motor can turn the chainring faster than the rider's feet and the
// cranks freewheel, so their speed no longer says anything about the bike:
// no estimate and no learning while set, and a fresh start after.
void effort_set_motor_driven(bool driven);

// True while the estimate can be trusted: magnet pattern learned and
// aligned, pedalling forwards steadily, below effort_max_speed_kph.
bool effort_is_valid();

// Estimated rider power (W) and torque (Nm x10); 0 when not valid.
uint16_t effort_get_rider_power_w();
uint16_t effort_get_rider_torque_nm_x10();

// Rider power for effort assist: the estimate while valid, else the last one
// for up to 5 s after it stopped being valid. False when there's neither.
bool effort_get_assist_power_w(uint16_t* power_w);

// Magnet pattern learning progress, 0-100 (100 = learned).
uint8_t effort_get_learned_percent();

// --- bench testing (config tool's Bench page) ---

// No learning until the controller is switched off: on a stand, with no
// bike to push, the cranks wobble far more than on the road and would spoil
// the pattern. The config tool's Bench page triggers this (status reads and
// PAS captures).
void effort_block_learning();

// Forgets the learned pattern (in EEPROM too); learning starts over.
bool effort_reset_pattern();

// Records the raw PAS edges (as pas_read_edges() gives them) of the next
// EFFORT_CAPTURE_EDGES edges, for the tool to check the sensor's steadiness.
#define EFFORT_CAPTURE_EDGES		(EFFORT_EDGES_PER_REV * 6)

#define EFFORT_CAPTURE_IDLE			0
#define EFFORT_CAPTURE_RUNNING		1
#define EFFORT_CAPTURE_DONE			2
#define EFFORT_CAPTURE_OVERFLOW		3	// edges were lost: start again

void effort_start_capture();

// Copies up to n captured edges from index first into dst. Returns the
// capture state; *count is how many edges have been captured so far.
uint8_t effort_read_capture(uint16_t first, uint16_t* dst, uint8_t n, uint16_t* count) __reentrant;

#endif
