/*
 * bbs-fw
 *
 * Released under the GPL License, Version 3
 */

// Rider effort from PAS edge timing.
//
// Rider torque peaks twice per crank revolution (once per leg). With the chain
// engaged that makes the bike, and so the crank, speed up and slow down twice
// per revolution, by a fraction proportional to the torque. For a relative
// wobble r of the crank speed at twice the crank frequency:
//
//     rider power = r * 2 * m * v^2 * w / a
//
// m = bike + rider mass, v = speed, w = crank speed (rad/s), a = how far the
// rider's torque swings around its mean (pedal stroke shape, ~0.8). Derivation
// and the model behind the numbers: research/effort/README.md.
//
// Measuring a wobble under 1% needs every edge of both PAS channels (96 per
// revolution) and the exact spacing of those edges, which differs per sensor:
// the magnets aren't perfectly even and the two channels' duty and phase vary.
// That spacing is learned while cruising fast in a high gear, where the
// rider's wobble is too small to disturb it, and kept in EEPROM. After a
// power-on the stored pattern is matched against live revolutions to find
// where the crank is (alignment).
//
// The rider's own wobble while learning is locked to the crank too, so a
// little of it ends up in the pattern and is subtracted from every later
// estimate. It shrinks with v^2 w, so the learning conditions are stored
// with the pattern and an assumed cruising effort added back (see
// LEARN_ASSUMED_RIDER_W).

#include "effort.h"
#include "cfgstore.h"
#include "fwconfig.h"
#include "sensors.h"
#include "system.h"
#include "util.h"

#include <string.h>

#define N	EFFORT_EDGES_PER_REV

typedef char effort_edges_check[(N == 96) ? 1 : -1];

// Learning happens only where the rider's own wobble is negligible.
#define LEARN_MIN_SPEED_KPH			20
#define LEARN_MIN_CADENCE_RPM		60
#define LEARN_MIN_GEAR_X10			25		// wheel revolutions per crank revolution
#define LEARN_REVS					200		// revolutions until the pattern counts as learned
#define LEARN_SAVE_EVERY_REVS		2000	// re-save after this much more learning

// The rider's power while the pattern is learned, assumed: it's added back
// to estimates, scaled by v^2 w against the learning conditions. Light
// efforts read up to twice this scaled amount too high, hard ones are off by
// however much the real cruising effort differed.
#define LEARN_ASSUMED_RIDER_W		100

typedef char learn_revs_check[(LEARN_REVS % 100 == 0) ? 1 : -1];

// Effort assist keeps the last estimate through gaps this short (throttle
// blips, gear changes, the first turn after slowing), rather than falling
// back to the level's fixed current.
#define ASSIST_HOLD_MS				5000

// Estimates are only trusted in this range.
#define VALID_MIN_CADENCE_RPM		25
#define VALID_MIN_SPEED_KPH			2

// Pattern shares are a segment's part of one revolution, in 1/65536ths.
#define SHARE_SCALE_SHIFT			16

// cos/sin of twice each segment's nominal mid-angle, x1024 (generated):
// mid-angle of segment q = (q + 0.5) * 360/96 degrees.
static const int16_t cos2_table[N] =
{
	1022, 1004, 970, 918, 851, 770, 675, 569, 453, 329, 200, 67, -67, -200, -329, -453,
	-569, -675, -770, -851, -918, -970, -1004, -1022, -1022, -1004, -970, -918, -851, -770, -675, -569,
	-453, -329, -200, -67, 67, 200, 329, 453, 569, 675, 770, 851, 918, 970, 1004, 1022,
	1022, 1004, 970, 918, 851, 770, 675, 569, 453, 329, 200, 67, -67, -200, -329, -453,
	-569, -675, -770, -851, -918, -970, -1004, -1022, -1022, -1004, -970, -918, -851, -770, -675, -569,
	-453, -329, -200, -67, 67, 200, 329, 453, 569, 675, 770, 851, 918, 970, 1004, 1022
};
static const int16_t sin2_table[N] =
{
	67, 200, 329, 453, 569, 675, 770, 851, 918, 970, 1004, 1022, 1022, 1004, 970, 918,
	851, 770, 675, 569, 453, 329, 200, 67, -67, -200, -329, -453, -569, -675, -770, -851,
	-918, -970, -1004, -1022, -1022, -1004, -970, -918, -851, -770, -675, -569, -453, -329, -200, -67,
	67, 200, 329, 453, 569, 675, 770, 851, 918, 970, 1004, 1022, 1022, 1004, 970, 918,
	851, 770, 675, 569, 453, 329, 200, 67, -67, -200, -329, -453, -569, -675, -770, -851,
	-918, -970, -1004, -1022, -1022, -1004, -970, -918, -851, -770, -675, -569, -453, -329, -200, -67
};

// --- learned pattern, as stored ---
typedef struct
{
	uint16_t shares[N];		// each segment's part of a revolution
	float v2w;				// mean v^2 w while learning (m^2/s^2 rad/s)
} learned_t;

static learned_t learned;
static uint16_t learned_revs;
static uint16_t revs_since_save;
static bool save_pending;

// --- edge tracking ---
static uint8_t prev_state;
static bool have_prev_state;
static uint8_t seg_index;		// segment that ended at the latest edge, in sensor space
static bool aligned;
static uint8_t offset;			// pattern index = (sensor index + offset) % N

// --- sliding window over the last N segments ---
static uint16_t period[N];		// 100us ticks, by sensor index
static uint16_t speed[N];		// share / period, by sensor index
static uint32_t period_sum;
static uint32_t speed_sum;
static int32_t re_sum;
static int32_t im_sum;
static uint8_t window_count;
static uint8_t edges_since_estimate;
static uint32_t sum_history[2];	// speed_sum a quarter and half a revolution ago
static uint8_t history_count;

// --- alignment search, a few rotations per call ---
static bool aligning;
static uint8_t align_rot;
static uint8_t best_rot;
static uint32_t best_err;
static uint32_t err_total;
static uint16_t snapshot[N];

// --- inputs and outputs ---
static uint16_t wheel_rpm_x10;
static uint16_t rider_power_w;
static uint16_t rider_torque_nm_x10;
static bool valid;
static uint16_t held_power_w;		// last valid estimate, for effort assist
static uint32_t held_at_ms;
static int32_t cos_sum;
static int32_t sin_sum;


static void reset_window()
{
	window_count = 0;
	period_sum = 0;
	speed_sum = 0;
	re_sum = 0;
	im_sum = 0;
	edges_since_estimate = 0;
	history_count = 0;
	valid = false;
	rider_power_w = 0;
	rider_torque_nm_x10 = 0;
}

static void lose_alignment()
{
	aligned = false;
	aligning = false;
	reset_window();
}

void effort_init() __reentrant
{
	memset(period, 0, sizeof(period));
	memset(speed, 0, sizeof(speed));

	have_prev_state = false;
	seg_index = 0;
	wheel_rpm_x10 = 0;
	save_pending = false;
	revs_since_save = 0;
	held_at_ms = 0;

	// the trig tables are rounded, so their sums aren't exactly 0: keep them
	// so the mean can be taken out exactly (see estimate())
	cos_sum = 0;
	sin_sum = 0;
	for (uint8_t i = 0; i < N; ++i)
	{
		cos_sum += cos2_table[i];
		sin_sum += sin2_table[i];
	}

	if (cfgstore_read_effort_pattern((uint8_t*)&learned, sizeof(learned)))
	{
		learned_revs = LEARN_REVS;
		aligned = false;	// where the crank is in the pattern is unknown after power-on
	}
	else
	{
		memset(&learned, 0, sizeof(learned));
		learned_revs = 0;
		aligned = true;		// nothing learned yet: the first revolution defines the frame
		offset = 0;
	}

	aligning = false;
	reset_window();
}

void effort_set_motor_driven(bool driven)
{
	if (driven)
	{
		// called every app tick: the window never fills while driven
		reset_window();
	}
}

void effort_set_wheel_rpm_x10(uint16_t rpm_x10)
{
	wheel_rpm_x10 = rpm_x10;
}

static uint8_t pattern_index(uint8_t sensor_index) __reentrant
{
	uint16_t i = (uint16_t)sensor_index + offset;
	return (uint8_t)(i >= N ? i - N : i);
}

// speed x trig table entry, scaled down to keep the harmonic sums in 32 bits
static int32_t harmonic_term(uint16_t spd, int16_t trig)
{
	return ((int32_t)spd * trig) >> 6;
}

static void window_remove(uint8_t i, uint8_t q) __reentrant
{
	period_sum -= period[i];
	speed_sum -= speed[i];
	re_sum -= harmonic_term(speed[i], cos2_table[q]);
	im_sum -= harmonic_term(speed[i], sin2_table[q]);
}

static void window_add_speed(uint8_t i, uint8_t q, uint16_t spd) __reentrant
{
	speed[i] = spd;
	speed_sum += spd;
	re_sum += harmonic_term(spd, cos2_table[q]);
	im_sum += harmonic_term(spd, sin2_table[q]);
}

static uint16_t share_of(uint16_t ticks, uint32_t sum)
{
	return (uint16_t)(((uint32_t)ticks << SHARE_SCALE_SHIFT) / sum);
}

// crank rpm x10 from the window's revolution time (100us ticks)
static uint16_t cadence_rpm_x10()
{
	return period_sum > 0 ? (uint16_t)(6000000ul / period_sum) : 0;
}

static uint16_t kph_to_wheel_rpm_x10(uint8_t kph)
{
	// wheel circumference (m) = size_inch x10 * 0.0254 / 10 * pi
	uint16_t size_inch_x10 = EXPAND_U16(g_config.wheel_size_inch_x10_u16h, g_config.wheel_size_inch_x10_u16l);
	return (uint16_t)((uint32_t)kph * 10u * 1000000ul / 60u / ((uint32_t)size_inch_x10 * 798u / 100u));
}

// v^2 w now: wheel speed (m/s) squared times crank speed (rad/s)
static float speed2_crank() __reentrant
{
	float w = cadence_rpm_x10() * (3.14159f / 300.0f);		// rpm x10 -> rad/s
	uint16_t size_inch_x10 = EXPAND_U16(g_config.wheel_size_inch_x10_u16h, g_config.wheel_size_inch_x10_u16l);
	float v = wheel_rpm_x10 * (size_inch_x10 * 0.00798f / 600.0f);	// m/s
	return v * v * w;
}

static void learn_revolution() __reentrant
{
	float v2w = speed2_crank();
	learned.v2w = learned_revs == 0 ? v2w : learned.v2w + (v2w - learned.v2w) / 32;

	for (uint8_t s = 0; s < N; ++s)
	{
		uint16_t share = share_of(period[s], period_sum);
		uint8_t q = pattern_index(s);
		if (learned_revs == 0)
		{
			learned.shares[q] = share;
		}
		else
		{
			learned.shares[q] = (uint16_t)((int32_t)learned.shares[q] + ((int32_t)share - (int32_t)learned.shares[q]) / 32);
		}
	}

	if (learned_revs < 0xffff)
	{
		learned_revs++;
	}
	if (++revs_since_save >= LEARN_SAVE_EVERY_REVS || learned_revs == LEARN_REVS)
	{
		revs_since_save = 0;
		save_pending = true;
	}
}

static bool learning_conditions() __reentrant
{
	uint16_t cadence = cadence_rpm_x10();
	return cadence >= LEARN_MIN_CADENCE_RPM * 10u &&
		wheel_rpm_x10 >= kph_to_wheel_rpm_x10(LEARN_MIN_SPEED_KPH) &&
		(uint32_t)wheel_rpm_x10 * 10u >= (uint32_t)cadence * LEARN_MIN_GEAR_X10;
}

// Matches one revolution against the stored pattern, a few rotations per
// call. A clear best match sets the offset; a pattern with no distinctive
// shape (all rotations alike) can't be misaligned in a way that matters.
static void align_step() __reentrant
{
	for (uint8_t k = 0; k < 12 && align_rot < N; ++k, ++align_rot)
	{
		uint32_t err = 0;
		for (uint8_t s = 0; s < N; ++s)
		{
			uint16_t i = (uint16_t)s + align_rot;
			uint16_t p = learned.shares[i >= N ? i - N : i];
			err += snapshot[s] > p ? snapshot[s] - p : p - snapshot[s];
		}

		err_total += err;
		if (err < best_err)
		{
			best_err = err;
			best_rot = align_rot;
		}
	}

	if (align_rot >= N)
	{
		uint32_t mean_err = err_total / N;
		aligning = false;

		if (best_err * 2 <= mean_err || mean_err < (uint32_t)N * 3u)
		{
			offset = best_rot;
			aligned = true;
			reset_window();
		}
	}
}

static void estimate() __reentrant
{
	// speed trend: how the window's speed sum moved over half a revolution,
	// and over the last quarter (to catch sudden changes as they start)
	int32_t trend = (int32_t)(speed_sum - sum_history[1]);
	int32_t recent = (int32_t)(speed_sum - sum_history[0]);
	bool have_trend = history_count >= 2;
	sum_history[1] = sum_history[0];
	sum_history[0] = speed_sum;
	if (history_count < 2)
	{
		history_count++;
	}

	uint16_t cadence = cadence_rpm_x10();
	uint16_t min_wheel = kph_to_wheel_rpm_x10(VALID_MIN_SPEED_KPH);
	uint16_t max_wheel = kph_to_wheel_rpm_x10(g_config.effort_max_speed_kph);

	if (!have_trend || learned_revs < LEARN_REVS || cadence < VALID_MIN_CADENCE_RPM * 10u ||
		wheel_rpm_x10 < min_wheel || wheel_rpm_x10 > max_wheel ||
		g_config.effort_stroke_x100 == 0)
	{
		valid = false;
		rider_power_w = 0;
		rider_torque_nm_x10 = 0;
		return;
	}

	// The trend removal below is exact for a steady change of speed; a gear
	// change or a hard launch moves the speed more than 5% in half a
	// revolution and not steadily: keep the last estimate until it settles.
	if ((uint32_t)(trend < 0 ? -trend : trend) * 20u > speed_sum ||
		(uint32_t)(recent < 0 ? -recent : recent) * 40u > speed_sum)
	{
		return;
	}

	// 2nd harmonic of segment speed, with the mean taken out exactly
	int32_t mean = (int32_t)(speed_sum / N);
	int32_t re = re_sum - ((mean * cos_sum) >> 6);
	int32_t im = im_sum - ((mean * sin_sum) >> 6);

	// Speeding up or slowing down across the window leaks into the harmonic
	// as if it were pedal wobble. For a linear trend with slope k per edge,
	// the leak is k * 16 * N / (2 sin(2pi/N)) at an angle set by where the
	// window starts (q0, the oldest edge's pattern index): sin and -cos of
	// 4pi q0 / N, which the tables give as the mean of entries q0-1 and q0.
	// The trend measured over half a revolution is k * N * N / 2, so the leak
	// is trend * 16 / (N sin(2pi/N)) = trend * 2.548 times that sin, -cos.
	{
		uint8_t q_last = pattern_index(seg_index);
		uint8_t q0 = q_last == N - 1 ? 0 : q_last + 1;
		float k = trend * (2.548f / 2048.0f);	// /2 for the mean, /1024 for the table scale
		re -= (int32_t)(k * (sin2_table[q_last] + sin2_table[q0]));
		im += (int32_t)(k * (cos2_table[q_last] + cos2_table[q0]));
	}

	if (re < 0) re = -re;
	if (im < 0) im = -im;

	// |re, im| by alpha-max-plus-beta-min (within ~4%)
	int32_t mag = re > im ? re + (im * 3) / 8 : im + (re * 3) / 8;

	// relative wobble: amplitude (2|X|/N, in speed x16 units) over mean speed
	float ripple = (2.0f * mag / N) / (16.0f * mean);

	float v2w = speed2_crank();
	float a = g_config.effort_stroke_x100 / 100.0f;

	float power = ripple * 2.0f * g_config.effort_mass_kg * v2w / a;
	if (learned.v2w > 0)
	{
		power += LEARN_ASSUMED_RIDER_W * v2w / learned.v2w;
	}
	if (power > 2000.0f)
	{
		power = 2000.0f;
	}

	// light smoothing across quarter-revolution updates
	uint16_t p = (uint16_t)power;
	rider_power_w = valid ? (uint16_t)((rider_power_w + p) / 2) : p;
	rider_torque_nm_x10 = (uint16_t)(rider_power_w * 10.0f / (cadence * (3.14159f / 300.0f)));
	valid = true;
	held_power_w = rider_power_w;
	held_at_ms = system_ms();
}

// Returns true when an estimate is due (every quarter revolution).
static bool add_edge(uint16_t entry) __reentrant
{
	// forward order of (PAS1 << 1 | PAS2) states: 2 -> 3 -> 1 -> 0 -> 2
	static const uint8_t next_forward[4] = { 2, 0, 3, 1 };
	static const uint8_t next_backward[4] = { 1, 3, 0, 2 };

	uint16_t ticks = entry & PAS_EDGE_TICKS_MASK;
	uint8_t state = ((entry & PAS_EDGE_PAS1) ? 2 : 0) | ((entry & PAS_EDGE_PAS2) ? 1 : 0);

	if (!have_prev_state)
	{
		have_prev_state = true;
		prev_state = state;
		return false;
	}

	bool forward = state == next_forward[prev_state];
	bool backward = state == next_backward[prev_state];
	prev_state = state;

	if (!forward && !backward)
	{
		// a missed or spurious edge: the crank position is lost
		lose_alignment();
		return false;
	}

	if (backward)
	{
		// keep counting so the position stays known, but restart the window
		seg_index = seg_index == 0 ? N - 1 : seg_index - 1;
		reset_window();
		return false;
	}

	seg_index = seg_index == N - 1 ? 0 : seg_index + 1;

	if (ticks >= PAS_EDGE_TICKS_MASK)
	{
		// first edge after the pedals stood still
		reset_window();
		return false;
	}

	// slide the oldest segment out (the same sensor index, one revolution ago)
	uint8_t q = pattern_index(seg_index);
	if (window_count == N)
	{
		window_remove(seg_index, q);
	}
	else
	{
		window_count++;
	}

	period[seg_index] = ticks;
	period_sum += ticks;

	// segment speed = its angle (learned share) over its time
	if (aligned && learned_revs >= LEARN_REVS)
	{
		// share (~683) << 11 over ticks (>= ~40 at 150 rpm) stays within 16 bits
		window_add_speed(seg_index, q, (uint16_t)(((uint32_t)learned.shares[q] << 11) / ticks));
	}
	else
	{
		speed[seg_index] = 0;
	}

	if (window_count < N)
	{
		return false;
	}

	// a full revolution is in the window
	if (aligned && q == N - 1 && learning_conditions())
	{
		learn_revolution();
		if (learned_revs == LEARN_REVS)
		{
			// just learned: the window was filled without the pattern
			reset_window();
			return false;
		}
	}

	if (!aligned && !aligning && learned_revs >= LEARN_REVS)
	{
		for (uint8_t s = 0; s < N; ++s)
		{
			snapshot[s] = share_of(period[s], period_sum);
		}
		aligning = true;
		align_rot = 0;
		best_err = 0xffffffff;
		err_total = 0;
	}

	if (aligned && learned_revs >= LEARN_REVS && ++edges_since_estimate >= N / 4)
	{
		edges_since_estimate = 0;
		return true;
	}
	return false;
}

// estimate() runs here rather than inside add_edge(), one call level less
// deep: its stack frame is the biggest in the firmware.
void effort_add_edge(uint16_t entry)
{
	if (add_edge(entry))
	{
		estimate();
	}
}

void effort_process()
{
#if HAS_PAS_EDGE_TIMING
	uint16_t edges[8];
	bool overflow = false;
	uint8_t n;

	effort_set_wheel_rpm_x10(speed_sensor_get_rpm_x10());

	do
	{
		n = pas_read_edges(edges, 8, &overflow);
		if (overflow)
		{
			lose_alignment();
		}
		for (uint8_t i = 0; i < n; ++i)
		{
			effort_add_edge(edges[i]);
		}
	} while (n == 8);
#endif

	if (aligning)
	{
		align_step();
	}

	// save learning only while stopped: an EEPROM write stalls the main loop
	if (save_pending && wheel_rpm_x10 == 0 && !pas_is_pedaling_forwards())
	{
		save_pending = false;
		cfgstore_write_effort_pattern((uint8_t*)&learned, sizeof(learned));
	}
}

bool effort_is_valid()
{
	return valid;
}

uint16_t effort_get_rider_power_w()
{
	return valid ? rider_power_w : 0;
}

bool effort_get_assist_power_w(uint16_t* power_w)
{
	if (valid)
	{
		*power_w = rider_power_w;
		return true;
	}
	if (held_at_ms != 0 && system_ms() - held_at_ms < ASSIST_HOLD_MS)
	{
		*power_w = held_power_w;
		return true;
	}
	return false;
}

uint16_t effort_get_rider_torque_nm_x10()
{
	return valid ? rider_torque_nm_x10 : 0;
}

uint8_t effort_get_learned_percent()
{
	return learned_revs >= LEARN_REVS ? 100 : (uint8_t)(learned_revs / (LEARN_REVS / 100));
}
