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

// Averaging of the wobble: until its timing noise is worth about this many
// watts, over at most this many quarter-revolution windows.
#define NOISE_TARGET_W				12.0f
#define MAX_AVERAGING				16.0f

// Launch sensing: the time 4 segments (one pulse, exactly 15 degrees whatever
// the sensor's duty and phase) take from a standing start, and for how long
// the result stands in for the estimate.
#define LAUNCH_SEGMENTS				4
#define LAUNCH_HOLD_MS				2500
#define LAUNCH_MAX_NM_X10			2500

// Estimates are only trusted in this range.
#define VALID_MIN_CADENCE_RPM		25
#define VALID_MIN_SPEED_KPH			2

// Pattern shares are a segment's part of one revolution, in 1/2^20ths: fine
// enough that averaging them (learning) isn't stopped by rounding. A segment
// is at most a pulse (1/24 revolution), so a share fits 16 bits.
#define SHARE_SCALE_SHIFT			20

// Segments longer than this (0.4 s) count as standing still; it also keeps
// the share arithmetic within 32 bits.
#define MAX_SEGMENT_TICKS			4095

// A pattern learned for at least this many revolutions is good enough to find
// the crank's position with again, if it's lost before learning has finished.
#define ALIGN_MIN_REVS				16

// Rotations of the pattern tried per call of effort_process(): few, as each
// costs about a millisecond.
#define ALIGN_ROTATIONS_PER_STEP	3

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
	uint16_t shares[N];		// each segment's part of a revolution (SHARE_SCALE_SHIFT)
	float v2w;				// mean v^2 w while learning (m^2/s^2 rad/s)
} learned_t;

static learned_t learned;
static uint16_t learned_revs;
static uint16_t revs_since_save;
static bool save_pending;
static bool learning_rev;		// this revolution's segments may go into the pattern
static uint32_t learning_rev_period;	// the revolution time when it began
static bool commit_pending;		// a steady revolution in snapshot[] goes into the pattern
static bool commit_first;		// as it is (the first one), not averaged in

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
static uint8_t dirty_edges;		// edges until a sudden change of speed has left the window
static uint8_t edges_since_estimate;
static uint32_t sum_history[2];	// speed_sum a quarter and half a revolution ago
static uint8_t history_count;

// --- alignment search, a few rotations per call ---
static bool aligning;
static uint8_t align_rot;
static uint8_t best_rot;
static uint32_t best_err;
static uint32_t err_total;
static uint16_t snapshot[N];	// one revolution's shares: by sensor index to find the
								// crank with, by pattern index while learning
static uint8_t snapshot_count;	// segments of the current revolution in it

// --- inputs and outputs ---
static uint16_t wheel_rpm_x10;
static uint16_t rider_power_w;
static uint16_t rider_torque_nm_x10;
static bool valid;
static uint16_t held_power_w;		// last valid estimate, for effort assist
static uint32_t held_at_ms;

// --- whether the cranks are driving the wheel (see effort_is_freewheeling) ---
#define LOCK_DRIVING		0
#define LOCK_PROBATION		1	// after a step in crank speed: a gear change, or the legs slowing?
#define LOCK_FREE			2
static uint8_t lock_state;
static uint16_t lock_ref_x16;		// PAS edges per wheel pulse in the gear last driven in, 0 = not known
static uint16_t lock_last_x16;
static uint16_t lock_start_x16;		// at the start of probation, or of a steady run while free
static uint16_t lock_start_wheel;	// wheel speed then
static uint16_t lock_before_x16;	// the pulse before a steady run while free
static uint8_t lock_count;

// --- launch sensing ---
static uint8_t launch_edges;		// forward edges of a standing start so far, 0 = none under way
static uint16_t launch_ticks[2];		// the first two segments, the last two
static uint16_t launch_shares[2];	// and their angles, when the pattern is known
static uint16_t launch_nm_x10;
static uint32_t launch_at_ms;		// 0 = no launch torque
static uint32_t launch_edge_ms;		// the latest edge of a launch
static uint16_t gear_x100;			// wheel turns per crank turn when last pedalling steadily

// --- bench ---
static bool learning_blocked;	// until power-off: not cleared by effort_init()
#if HAS_PAS_EDGE_TIMING
static uint16_t capture[EFFORT_CAPTURE_EDGES];
static uint16_t capture_count;
static uint8_t capture_state;
#endif
static int32_t cos_sum;
static int32_t sin_sum;
static float wobble_re;		// the crank-speed wobble (relative, as a vector
static float wobble_im;		// locked to the crank), averaged over windows


static void reset_window()
{
	window_count = 0;
	dirty_edges = 0;
	period_sum = 0;
	speed_sum = 0;
	re_sum = 0;
	im_sum = 0;
	edges_since_estimate = 0;
	history_count = 0;
	learning_rev = false;
	snapshot_count = 0;
	valid = false;
	rider_power_w = 0;
	rider_torque_nm_x10 = 0;
}

static void lose_alignment()
{
	commit_pending = false;		// snapshot[] is about to be reused for finding the crank
	if (learned_revs < ALIGN_MIN_REVS)
	{
		// too little learned to find the position with: start learning again,
		// with wherever the crank is now as the new frame
		learned_revs = 0;
		aligned = true;
		offset = 0;
	}
	else
	{
		aligned = false;
	}
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
	commit_pending = false;
	held_at_ms = 0;
	launch_edges = 0;
	launch_at_ms = 0;
	gear_x100 = 0;
	lock_state = LOCK_DRIVING;
	lock_ref_x16 = 0;
	lock_last_x16 = 0;
	lock_count = 0;

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

// A gear change steps the crank speed within a few edges, and a step in the
// window reads as a huge wobble. Compares the last pulse (4 segments, 15
// degrees) with the one before: the rider's wobble moves that by about 1%,
// timing noise by about 1%, a gear change by 8% or more.
static bool speed_stepped(uint8_t i) __reentrant
{
	uint16_t now = 0, before = 0;
	uint8_t k;

	for (k = 0; k < 8; ++k)
	{
		uint16_t spd = speed[i] >> 2;
		if (k < 4) now += spd; else before += spd;
		i = i == 0 ? N - 1 : i - 1;
	}
	// more than about 4.7%
	return (now > before ? now - before : before - now) > (before >> 5) + (before >> 6);
}

static uint16_t share_of(uint16_t ticks, uint32_t sum)
{
	// ticks <= MAX_SEGMENT_TICKS, so the shift fits 32 bits
	uint32_t share = ((uint32_t)ticks << SHARE_SCALE_SHIFT) / sum;
	return share > 0xffff ? 0xffff : (uint16_t)share;
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

// Learning is spread over the edges: each segment's share goes into a
// scratch copy as it's measured (done all at once per revolution, the
// divisions stalled the main loop, and with it the brake check, for tens of
// milliseconds). The revolution is only taken into the pattern if it turned
// out steady: a gear change inside it would bend the pattern.
// A steady revolution goes into the pattern a segment per edge over the next
// revolution, each entry just before snapshot[] overwrites it (see add_edge):
// all 96 at once stalled the main loop, and with it the brake check.
static void learn_revolution() __reentrant
{
	float v2w = speed2_crank();

	learned.v2w = learned_revs == 0 ? v2w : learned.v2w + (v2w - learned.v2w) / 32;
	commit_first = learned_revs == 0;
	commit_pending = true;

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
	if (learning_blocked)
	{
		return false;
	}
	return cadence >= LEARN_MIN_CADENCE_RPM * 10u &&
		wheel_rpm_x10 >= kph_to_wheel_rpm_x10(LEARN_MIN_SPEED_KPH) &&
		(uint32_t)wheel_rpm_x10 * 10u >= (uint32_t)cadence * LEARN_MIN_GEAR_X10;
}

// Matches one revolution against the stored pattern, a few rotations per
// call. A clear best match sets the offset; a pattern with no distinctive
// shape (all rotations alike) can't be misaligned in a way that matters.
static void align_step() __reentrant
{
	for (uint8_t k = 0; k < ALIGN_ROTATIONS_PER_STEP && align_rot < N; ++k, ++align_rot)
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

		// (a mean error under 3/65536 of a revolution per segment is "alike")
		if (best_err * 2 <= mean_err || mean_err < (uint32_t)N * 48u)
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
	// (working values are static: on the stack they would take over half of it)
	static int32_t trend, recent, mean, re, im;
	static float scale, new_re, new_im, v2w, a, watts_per_wobble, noise, noise_w, k, d_re, d_im, ripple, abs_re, abs_im, power;

	trend = (int32_t)(speed_sum - sum_history[1]);
	recent = (int32_t)(speed_sum - sum_history[0]);
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

	// After a step in crank speed, until it's known to be a gear change: no
	// new estimates (if the legs are freewheeling they'd read as effort), and
	// effort assist keeps the last one for ASSIST_HOLD_MS at most.
	if (lock_state == LOCK_PROBATION)
	{
		if (valid && system_ms() - held_at_ms > ASSIST_HOLD_MS)
		{
			valid = false;
			rider_power_w = 0;
			rider_torque_nm_x10 = 0;
		}
		return;
	}

	if (!have_trend || lock_state == LOCK_FREE || learned_revs < LEARN_REVS || cadence < VALID_MIN_CADENCE_RPM * 10u ||
		wheel_rpm_x10 < min_wheel || wheel_rpm_x10 > max_wheel ||
		g_config.effort_stroke_x100 == 0)
	{
		valid = false;
		rider_power_w = 0;
		rider_torque_nm_x10 = 0;
		return;
	}

	// The trend removal below is exact for a steady change of speed. Keep the
	// last estimate while a sudden change (a gear change) is in the window, or
	// the speed moves more than 10% in half a revolution (a hard launch).
	if (dirty_edges > 0 ||
		(uint32_t)(trend < 0 ? -trend : trend) * 10u > speed_sum ||
		(uint32_t)(recent < 0 ? -recent : recent) * 20u > speed_sum)
	{
		return;
	}

	// 2nd harmonic of segment speed, with the mean taken out exactly
	mean = (int32_t)(speed_sum / N);
	re = re_sum - ((mean * cos_sum) >> 6);
	im = im_sum - ((mean * sin_sum) >> 6);

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
		k = trend * (2.548f / 2048.0f);	// /2 for the mean, /1024 for the table scale
		re -= (int32_t)(k * (sin2_table[q_last] + sin2_table[q0]));
		im += (int32_t)(k * (cos2_table[q_last] + cos2_table[q0]));
	}

	// The wobble as a fraction of the mean speed (amplitude 2|X|/N, the sums
	// being in speed x16 units).
	scale = 2.0f / (16.0f * N * mean);
	new_re = re * scale;
	new_im = im * scale;

	// Timing each edge to 100us puts noise on the wobble: about 4.5e-6 x rpm
	// per window (measured in the host tests). Windows are averaged as vectors (the wobble keeps its angle
	// on the crank, noise doesn't, so it averages away without reading high),
	// over more windows the more the noise is worth in watts: none on a slow
	// climb, a couple of revolutions at 20 km/h. A change well clear of the
	// noise is the rider's, and followed at once.
	v2w = speed2_crank();
	a = g_config.effort_stroke_x100 / 100.0f;
	watts_per_wobble = 2.0f * g_config.effort_mass_kg * v2w / a;
	noise = 4.5e-6f * 0.1f * cadence;
	{
		noise_w = noise * watts_per_wobble;
		k = noise_w * noise_w * (1.0f / (NOISE_TARGET_W * NOISE_TARGET_W));
		d_re = new_re - wobble_re;
		d_im = new_im - wobble_im;
		if (d_re < 0) d_re = -d_re;
		if (d_im < 0) d_im = -d_im;

		if (!valid || k < 1.0f)
		{
			k = 1.0f;
		}
		else if (d_re + d_im > 4.0f * noise)
		{
			k = k > 2.0f ? 2.0f : k;
		}
		else if (k > MAX_AVERAGING)
		{
			k = MAX_AVERAGING;
		}
		wobble_re += (new_re - wobble_re) / k;
		wobble_im += (new_im - wobble_im) / k;
	}

	// |wobble| by alpha-max-plus-beta-min (within ~4%)

	{
		abs_re = wobble_re < 0 ? -wobble_re : wobble_re;
		abs_im = wobble_im < 0 ? -wobble_im : wobble_im;
		ripple = abs_re > abs_im ? abs_re + abs_im * 0.375f : abs_im + abs_re * 0.375f;
	}



	power = ripple * watts_per_wobble;
	if (learned.v2w > 0)
	{
		power += LEARN_ASSUMED_RIDER_W * v2w / learned.v2w;
	}
	if (power > 2000.0f)
	{
		power = 2000.0f;
	}

	rider_power_w = (uint16_t)power;
	rider_torque_nm_x10 = (uint16_t)(rider_power_w * 10.0f / (cadence * (3.14159f / 300.0f)));
	valid = true;
	launch_at_ms = 0;	// the estimate takes over from a launch

	// the gear, for the next launch: wheel turns per crank turn
	if (cadence > 0)
	{
		gear_x100 = (uint16_t)(((uint32_t)wheel_rpm_x10 * 100u) / cadence);
	}
	held_power_w = rider_power_w;
	held_at_ms = system_ms();
}

// The crank's acceleration over the first pulse of a standing start, times
// the bike's mass seen from the crank, m x (wheel radius x gear)^2.
//
// With the sensor's pattern known (learned, and the crank's position kept
// since), the pulse's two halves have known angles a and b and took ta and
// tb: acceleration = 2 (b/tb - a/ta) / (ta + tb), whatever speed it began at.
// Without it, only the whole pulse's 15 degrees is known, and the start is
// taken to be from rest: 2 x 15 degrees / t^2 (which reads high if the bike
// was creeping).
static void launch_measure() __reentrant
{
	static float ta, tb, accel, lever, nm_x10;
	uint16_t size_inch_x10 = EXPAND_U16(g_config.wheel_size_inch_x10_u16h, g_config.wheel_size_inch_x10_u16l);
	// the gear the bike stopped in, or the low gear if it isn't known
	uint16_t gear = gear_x100 > 0 ? gear_x100 : (uint16_t)g_config.gear_ratio_low_x10 * 10u;

	ta = launch_ticks[0] * 1e-4f;
	tb = launch_ticks[1] * 1e-4f;
	if (aligned && learned_revs >= LEARN_REVS)
	{
		// shares are 1/2^20 of a revolution: x 2 pi / 2^20 radians
		accel = 2.0f * 5.992e-6f * (launch_shares[1] / tb - launch_shares[0] / ta) / (ta + tb);
	}
	else
	{
		accel = 2.0f * 0.2618f / ((ta + tb) * (ta + tb));
	}

	lever = size_inch_x10 * 0.00127f * gear * 0.01f;	// metres of road per radian of crank
	nm_x10 = g_config.effort_mass_kg * lever * lever * 10.0f * accel;

	launch_nm_x10 = nm_x10 > LAUNCH_MAX_NM_X10 ? LAUNCH_MAX_NM_X10 : (nm_x10 > 0 ? (uint16_t)nm_x10 : 0);
	launch_at_ms = system_ms();
	if (launch_at_ms == 0)
	{
		launch_at_ms = 1;
	}
}

static bool within(uint16_t a, uint16_t b, uint8_t percent) __reentrant
{
	uint16_t d = a > b ? a - b : b - a;
	return (uint32_t)d * 100u <= (uint32_t)b * percent;
}

static void set_free() __reentrant
{
	lock_state = LOCK_FREE;
	lock_count = 0;
	held_at_ms = 0;		// effort assist mustn't carry on with the last estimate
	reset_window();
}

// With the cranks driving the wheel, the PAS edges per wheel pulse are the
// gear: exactly constant, whatever the speed does, and changing only in a
// step when the rider shifts. Legs turning slower than the chainring show as
// a ratio that drifts, or that doesn't hold after a step.
void effort_add_wheel_pulse(uint16_t e) __reentrant
{
	// too few edges to tell: hardly pedalling (the estimate has no window then)
	if (e < 16u * 8u)
	{
		lock_last_x16 = 0;
		return;
	}

	switch (lock_state)
	{
	case LOCK_DRIVING:
		if (lock_ref_x16 == 0 || within(e, lock_ref_x16, 4))
		{
			// (follows slow creep, such as the tyre's size settling)
			lock_ref_x16 = lock_ref_x16 == 0 ? e : (uint16_t)(lock_ref_x16 + ((int16_t)(e - lock_ref_x16)) / 4);
			lock_count = 0;
		}
		else if (dirty_edges > 0)
		{
			// the crank speed stepped: perhaps a gear change. This pulse spans
			// the step (part old gear, part new), so the next one starts the test
			lock_state = LOCK_PROBATION;
			lock_count = 0;
		}
		else if (++lock_count >= 2)
		{
			// two pulses off the gear without a step (one alone may be a missed magnet)
			set_free();
		}
		break;

	case LOCK_PROBATION:
		// A step down in crank speed at the same road speed is a shift to a
		// taller gear, or the legs starting to spin without driving: the two
		// look the same until the road speed changes. A gear keeps its ratio
		// while the bike speeds up or slows down; freewheeling legs don't.
		if (lock_count == 0)
		{
			lock_start_x16 = e;
			lock_start_wheel = wheel_rpm_x10;
			lock_count = 1;
		}
		else if (!within(e, lock_start_x16, 3))
		{
			// it doesn't hold: no gear
			set_free();
		}
		else if (++lock_count >= 4 &&
			(lock_start_x16 > lock_ref_x16 || !within(wheel_rpm_x10, lock_start_wheel, 3)))
		{
			// More crank turns per wheel turn than before is a lower gear:
			// freewheeling legs turn the cranks slower, never faster, so that
			// one holding is enough. Fewer is a taller gear only if it held
			// while the road speed changed.
			lock_state = LOCK_DRIVING;
			lock_ref_x16 = e;
			lock_count = 0;
		}
		break;

	default:
		// free: driving again once the cranks are back at the gear they left for
		// two pulses, or hold a new ratio for four while the bike's speed moves
		// (which a freewheeling crank wouldn't follow)
		if (lock_last_x16 != 0 && within(e, lock_ref_x16, 3) && within(lock_last_x16, lock_ref_x16, 3))
		{
			lock_state = LOCK_DRIVING;
			lock_count = 0;
		}
		else if (lock_count == 0 || !within(e, lock_start_x16, 2))
		{
			lock_before_x16 = lock_last_x16;
			lock_start_x16 = e;
			lock_start_wheel = wheel_rpm_x10;
			lock_count = 1;
		}
		else if (++lock_count >= 4 &&
			((lock_before_x16 != 0 && !within(lock_start_x16, lock_before_x16, 3) && lock_start_x16 > lock_before_x16) ||
			(lock_count >= 5 && !within(wheel_rpm_x10, lock_start_wheel, 3))))
		{
			// the legs caught up with the chainring: a step up to a ratio
			// that holds; or a ratio that held while the road speed changed
			lock_state = LOCK_DRIVING;
			lock_ref_x16 = e;
			lock_count = 0;
		}
		break;
	}

	lock_last_x16 = e;
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
		launch_edges = 0;
		lose_alignment();
		return false;
	}

	if (backward)
	{
		launch_edges = 0;
		// keep counting so the position stays known, but restart the window
		seg_index = seg_index == 0 ? N - 1 : seg_index - 1;
		reset_window();
		return false;
	}

	seg_index = seg_index == N - 1 ? 0 : seg_index + 1;

	if (ticks > MAX_SEGMENT_TICKS)
	{
		// first edge after the pedals stood still; a launch if the bike stands too
		launch_edges = wheel_rpm_x10 == 0 ? 1 : 0;
		launch_ticks[0] = launch_ticks[1] = 0;
		launch_shares[0] = launch_shares[1] = 0;
		reset_window();
		return false;
	}

	launch_edge_ms = system_ms();
	if (launch_edges > 0)
	{
		uint8_t half = launch_edges > LAUNCH_SEGMENTS / 2 ? 1 : 0;
		launch_ticks[half] += ticks;
		launch_shares[half] += learned.shares[pattern_index(seg_index)];
		if (++launch_edges > LAUNCH_SEGMENTS)
		{
			launch_edges = 0;
			launch_measure();
		}
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
		// 224 x rpm whatever the segment's size: within 16 bits below 290 rpm
		window_add_speed(seg_index, q, (uint16_t)(((uint32_t)learned.shares[q] << 7) / ticks));

		if (dirty_edges > 0)
		{
			dirty_edges--;
		}
		if (window_count >= 8 && speed_stepped(seg_index))
		{
			// until the step and the pulse before it have left the window, and
			// the sum from half a revolution earlier that the trend is taken from
			dirty_edges = (uint8_t)(N + N / 2 + 8);
		}
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
	if (aligned)
	{
		if (commit_pending)
		{
			if (commit_first)
			{
				learned.shares[q] = snapshot[q];
			}
			else
			{
				learned.shares[q] += (int16_t)(snapshot[q] - learned.shares[q]) / 32;
			}
			if (q == N - 1)
			{
				commit_pending = false;
			}
		}
		if (learning_rev)
		{
			// the window is the revolution ending at this segment
			snapshot[q] = share_of(ticks, period_sum);
		}
		if (q == N - 1)
		{
			// steady: the revolution time moved less than 1% during it, and
			// the conditions still hold at its end
			uint32_t moved = period_sum > learning_rev_period ? period_sum - learning_rev_period : learning_rev_period - period_sum;
			bool conditions = learning_conditions();

			if (learning_rev && conditions && moved * 100u < period_sum)
			{
				learn_revolution();
				if (learned_revs == LEARN_REVS)
				{
					// just learned: the window was filled without the pattern
					reset_window();
					return false;
				}
			}
			learning_rev = conditions;
			learning_rev_period = period_sum;
		}
	}
	else if (!aligning)
	{
		// one revolution of shares, by sensor index, to match against the pattern
		snapshot[seg_index] = share_of(ticks, period_sum);
		if (++snapshot_count == N)
		{
			snapshot_count = 0;
			aligning = true;
			align_rot = 0;
			best_err = 0xffffffff;
			err_total = 0;
		}
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

	uint16_t wheel_edges_x16;

	effort_set_wheel_rpm_x10(speed_sensor_get_rpm_x10());
	if (speed_sensor_read_pulse(&wheel_edges_x16))
	{
		effort_add_wheel_pulse(wheel_edges_x16);
	}

	do
	{
		n = pas_read_edges(edges, 8, &overflow);
		if (overflow)
		{
			lose_alignment();
			if (capture_state == EFFORT_CAPTURE_RUNNING)
			{
				capture_state = EFFORT_CAPTURE_OVERFLOW;
			}
		}
		for (uint8_t i = 0; i < n; ++i)
		{
			if (capture_state == EFFORT_CAPTURE_RUNNING)
			{
				capture[capture_count++] = edges[i];
				if (capture_count == EFFORT_CAPTURE_EDGES)
				{
					capture_state = EFFORT_CAPTURE_DONE;
				}
			}
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

bool effort_is_freewheeling() __reentrant
{
	return lock_state == LOCK_FREE;
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

bool effort_get_launch_torque_nm_x10(uint16_t* nm_x10) __reentrant
{
	if (launch_at_ms == 0)
	{
		return false;
	}
	// (no edge for 200 ms: the pedals have all but stopped, under 4 rpm)
	if (system_ms() - launch_at_ms > LAUNCH_HOLD_MS || system_ms() - launch_edge_ms > 200)
	{
		launch_at_ms = 0;
		return false;
	}
	*nm_x10 = launch_nm_x10;
	return true;
}

uint16_t effort_get_rider_torque_nm_x10()
{
	return valid ? rider_torque_nm_x10 : 0;
}

uint8_t effort_get_learned_percent()
{
	return learned_revs >= LEARN_REVS ? 100 : (uint8_t)(learned_revs / (LEARN_REVS / 100));
}

void effort_block_learning()
{
	learning_blocked = true;
}

bool effort_reset_pattern()
{
	// an empty record: no stored pattern reads back at any size
	bool ok = cfgstore_write_effort_pattern(0, 0);
	effort_init();
	return ok;
}

void effort_start_capture()
{
#if HAS_PAS_EDGE_TIMING
	capture_count = 0;
	capture_state = EFFORT_CAPTURE_RUNNING;
#endif
}

uint8_t effort_read_capture(uint16_t first, uint16_t* dst, uint8_t n, uint16_t* count) __reentrant
{
#if HAS_PAS_EDGE_TIMING
	for (uint8_t i = 0; i < n; ++i)
	{
		dst[i] = first + i < capture_count ? capture[first + i] : 0;
	}
	*count = capture_count;
	return capture_state;
#else
	for (uint8_t i = 0; i < n; ++i)
	{
		dst[i] = 0;
	}
	*count = 0;
	return EFFORT_CAPTURE_IDLE;
#endif
}
