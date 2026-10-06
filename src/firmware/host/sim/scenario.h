// Ride scenario files for the simulator.
//
// One statement per line; '#' starts a comment. Times are milliseconds
// from boot.
//
//   title <text>                      shown in the summary and plot
//   end <ms>                          how long to simulate (required)
//   config <field> <value>            override a config_t field before boot
//   config level <n> <field> <value>  override an assist level (standard mode)
//   bike <param> <value>              bike model parameter (see sim.c)
//   at <ms> <input> <value> [over <ms>]
//                                     set a rider input at a time, optionally
//                                     ramping linearly from its previous value
//   expect <ms> <output> <op> <value> check an output at a time; op is one of
//                                     == != < <= > >=
//
// Inputs:  cadence (crank rpm, negative = backwards), gear (wheel revs per
//          crank rev), speed (km/h, sets the wheel speed instantly),
//          throttle (%), brake (0/1), level (0-9, 10 = walk), voltage (V),
//          rider_w (W), grade (%)
//
// Two modes: by default cadence is an input and the wheel follows it. Using
// rider_w anywhere switches the scenario to physics: speed comes from rider
// and motor power against rolling resistance, air and the grade, and cadence
// follows from speed and gear while rider_w > 0.
// Outputs: current (target current, %), motor (enabled 0/1),
//          fw_cadence (rpm, as measured by the firmware)

#ifndef _SCENARIO_H_
#define _SCENARIO_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
	INPUT_CADENCE,
	INPUT_GEAR,
	INPUT_SPEED,
	INPUT_THROTTLE,
	INPUT_BRAKE,
	INPUT_LEVEL,
	INPUT_VOLTAGE,
	INPUT_RIDER_W,
	INPUT_GRADE,
	INPUT_COUNT
} input_t;

typedef enum
{
	OUTPUT_CURRENT,
	OUTPUT_MOTOR,
	OUTPUT_FW_CADENCE,
} output_t;

typedef enum { OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE } op_t;

typedef struct
{
	uint32_t at_ms;
	input_t input;
	double value;
	uint32_t over_ms;
	int line;
} event_t;

typedef struct
{
	uint32_t at_ms;
	output_t output;
	op_t op;
	double value;
	int line;
} expect_t;

typedef struct
{
	char field[48];
	int level;	// -1 for a global config field
	long value;
	int line;
} config_override_t;

typedef struct
{
	char param[32];
	double value;
	int line;
} bike_param_t;

#define SCENARIO_MAX_ITEMS 256

typedef struct
{
	char path[256];
	char title[128];
	uint32_t end_ms;

	event_t events[SCENARIO_MAX_ITEMS];
	int num_events;

	expect_t expects[SCENARIO_MAX_ITEMS];
	int num_expects;

	config_override_t overrides[SCENARIO_MAX_ITEMS];
	int num_overrides;

	bike_param_t bike_params[SCENARIO_MAX_ITEMS];
	int num_bike_params;
} scenario_t;

// Parses a scenario file. On failure prints "path:line: message" to stderr
// and returns false. Events are sorted by time (stable).
bool scenario_load(const char* path, scenario_t* s);

const char* input_name(input_t input);
const char* output_name(output_t output);
const char* op_name(op_t op);

#endif
