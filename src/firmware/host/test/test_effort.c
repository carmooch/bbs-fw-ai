#include "test.h"
#include "fake_hw.h"
#include "cfgstore.h"
#include "effort.h"
#include "sensors.h"

#include <math.h>
#include <stdlib.h>

// Generates PAS edges the way the controller sees them: both channels, four
// edges per pulse at uneven spacing with fixed per-edge errors (like a real
// sensor), each detected on the first 100us tick after it. The crank speed
// wobbles twice per revolution by r = P a / (2 m v^2 w), the inverse of the
// estimator, for a known rider power P.

#define EDGES 96
#define PI 3.14159265358979

typedef struct
{
	double edge_deg[EDGES];	// where each edge sits on the crank
	double phi_deg;			// crank angle
	int next_edge;
	bool throttle;			// app.c reports the throttle held every 5ms
	bool bench;				// the config tool polls status every 5ms
	double t;
	long last_tick;
	uint32_t now_ms;
} crank_t;

static void crank_init(crank_t* c, unsigned seed, double start_deg)
{
	srand(seed);
	for (int k = 0; k < EDGES; ++k)
	{
		// uneven channels: PAS1 high 55% of each pulse, PAS2 lagging by 3.0
		// degrees instead of 3.75, plus 0.3 degree random errors
		static const double in_pulse[4] = { 0.0, 3.0, 8.25, 11.25 };
		double u = (rand() + 1.0) / (RAND_MAX + 2.0), w = (rand() + 1.0) / (RAND_MAX + 2.0);
		double err = 0.3 * sqrt(-2 * log(u)) * cos(2 * PI * w);
		c->edge_deg[k] = (k / 4) * 15.0 + in_pulse[k % 4] + err;
	}
	c->phi_deg = start_deg;
	c->next_edge = 0;
	while (c->next_edge < EDGES && c->edge_deg[c->next_edge] <= start_deg)
	{
		c->next_edge++;
	}
	c->next_edge %= EDGES;
	c->t = 0;
	c->last_tick = -1;
	c->now_ms = 0;
	c->throttle = false;
	c->bench = false;
}

// pin states after edge k: PAS1 rise, PAS2 rise, PAS1 fall, PAS2 fall
static uint16_t state_bits(int k)
{
	static const uint16_t bits[4] =
	{
		PAS_EDGE_PAS1, PAS_EDGE_PAS1 | PAS_EDGE_PAS2, PAS_EDGE_PAS2, 0
	};
	return bits[k % 4];
}

// Pedals for a number of revolutions in a gear, with rider power P (W), the
// cadence going steadily from cadence_from to cadence_to, feeding edges to
// the estimator and calling effort_process() every 5ms. Returns the last
// estimate.
static uint16_t ride_ramp(crank_t* c, double revs, double cadence_from, double cadence_to, double gear, double P, int skip_edge_at)
{
	const double m = 110, a = 0.8, circumference = 28.0 * 0.0254 * PI;

	double start_deg = c->phi_deg;
	double end_deg = c->phi_deg + revs * 360;
	const double dt = 1e-5;
	int edges_fed = 0;
	while (c->phi_deg < end_deg)
	{
		double cadence_rpm = cadence_from + (cadence_to - cadence_from) * (c->phi_deg - start_deg) / (end_deg - start_deg);
		double w0 = cadence_rpm / 60 * 2 * PI;
		double v = cadence_rpm / 60 * gear * circumference;
		double r = P * a / (2 * m * v * v * w0);
		uint16_t wheel_rpm_x10 = (uint16_t)(cadence_rpm * gear * 10);
		g_hw.wheel_rpm_x10 = wheel_rpm_x10;
		effort_set_wheel_rpm_x10(wheel_rpm_x10);

		double phi = fmod(c->phi_deg, 360.0);
		double w = w0 * (1 + r * cos(2 * phi * PI / 180));
		c->phi_deg += w * 180 / PI * dt;
		c->t += dt;

		double target = c->edge_deg[c->next_edge];
		double rel = fmod(c->phi_deg, 360.0);
		bool crossed = c->next_edge == 0 ? (rel < phi) : (rel >= target);
		if (crossed)
		{
			long tick = (long)ceil(c->t / 1e-4);
			if (c->last_tick >= 0 && !(skip_edge_at >= 0 && edges_fed == skip_edge_at))
			{
				long ticks = tick - c->last_tick;
				effort_add_edge((uint16_t)(ticks > PAS_EDGE_TICKS_MASK ? PAS_EDGE_TICKS_MASK : ticks) | state_bits(c->next_edge));
			}
			if (!(skip_edge_at >= 0 && edges_fed == skip_edge_at))
			{
				c->last_tick = tick;
			}
			edges_fed++;
			c->next_edge = (c->next_edge + 1) % EDGES;
		}

		uint32_t ms = (uint32_t)(c->t * 1000);
		if (ms >= c->now_ms + 5)
		{
			c->now_ms = ms;
			g_hw.now_ms = 1000 + ms;
			effort_process();
			effort_set_motor_driven(c->throttle);
			if (c->bench)
			{
				effort_block_learning();
			}
		}
	}

	return effort_get_rider_power_w();
}

static uint16_t ride(crank_t* c, double revs, double cadence_rpm, double gear, double P, int skip_edge_at)
{
	return ride_ramp(c, revs, cadence_rpm, cadence_rpm, gear, P, skip_edge_at);
}

static void boot(void)
{
	fake_hw_reset();
	cfgstore_init();
	effort_init();
}

// learns in high gear at 32 km/h, 80 rpm, 150 W of rider effort
static void learn(crank_t* c)
{
	ride(c, 260, 80, 3.0, 150, -1);
}

static int test_effort_learns_while_cruising(void)
{
	crank_t c;
	boot();
	crank_init(&c, 1, 0);

	ASSERT_EQ(0, effort_get_learned_percent());
	// (the first revolution only fills the window)
	ride(&c, 100, 80, 3.0, 150, -1);
	uint8_t half = effort_get_learned_percent();
	ASSERT_TRUE(half >= 48 && half <= 50);

	// no learning in a low gear or below 20 km/h (whether a revolution
	// counts is decided at the end of the one before, so one more may)
	ride(&c, 100, 60, 1.2, 150, -1);
	ASSERT_TRUE(effort_get_learned_percent() <= half + 1);

	ride(&c, 110, 80, 3.0, 150, -1);
	ASSERT_EQ(100, effort_get_learned_percent());

	return 1;
}

static int test_effort_tracks_rider_power_on_a_climb(void)
{
	crank_t c;
	boot();
	crank_init(&c, 2, 0);
	learn(&c);

	// low gear, 60 rpm (~10 km/h): easy, medium, hard
	uint16_t easy = ride(&c, 10, 60, 1.2, 100, -1);
	uint16_t medium = ride(&c, 10, 60, 1.2, 200, -1);
	uint16_t hard = ride(&c, 10, 60, 1.2, 350, -1);

	printf("  climb: 100 W -> %u, 200 W -> %u, 350 W -> %u\n", easy, medium, hard);
	ASSERT_TRUE(effort_is_valid());
	ASSERT_TRUE(easy >= 80 && easy <= 120);
	ASSERT_TRUE(medium >= 170 && medium <= 230);
	ASSERT_TRUE(hard >= 300 && hard <= 400);

	return 1;
}

static int test_effort_invalid_above_max_speed(void)
{
	crank_t c;
	boot();
	crank_init(&c, 3, 0);
	learn(&c);

	// 32 km/h is above the default 25 km/h effort_max_speed_kph
	ride(&c, 10, 80, 3.0, 200, -1);
	ASSERT_TRUE(!effort_is_valid());
	ASSERT_EQ(0, effort_get_rider_power_w());

	return 1;
}

static int test_effort_realigns_after_power_cycle(void)
{
	crank_t c;
	boot();
	crank_init(&c, 4, 0);
	learn(&c);

	// stop: the learned pattern is saved while standing still
	g_hw.wheel_rpm_x10 = 0;
	effort_process();

	// power cycle: same sensor, crank left somewhere else
	effort_init();
	ASSERT_EQ(100, effort_get_learned_percent());
	crank_init(&c, 4, 137.0);

	uint16_t p = ride(&c, 12, 60, 1.2, 200, -1);
	printf("  after power cycle: 200 W -> %u\n", p);
	ASSERT_TRUE(effort_is_valid());
	ASSERT_TRUE(p >= 170 && p <= 230);

	return 1;
}

static int test_effort_recovers_from_a_missed_edge(void)
{
	crank_t c;
	boot();
	crank_init(&c, 5, 0);
	learn(&c);

	ride(&c, 10, 60, 1.2, 200, -1);
	ASSERT_TRUE(effort_is_valid());

	// a lost edge breaks the expected edge order: the estimate stops until
	// the crank is found again, about a revolution later
	ride(&c, 0.6, 60, 1.2, 200, 10);
	ASSERT_TRUE(!effort_is_valid());

	uint16_t p = ride(&c, 12, 60, 1.2, 200, -1);
	printf("  after a missed edge: 200 W -> %u\n", p);
	ASSERT_TRUE(effort_is_valid());
	ASSERT_TRUE(p >= 170 && p <= 230);

	return 1;
}

static int test_effort_tracks_rider_power_while_speeding_up(void)
{
	crank_t c;
	boot();
	crank_init(&c, 6, 0);
	learn(&c);

	// 50 -> 75 rpm in 10 revolutions (8 -> 12 km/h): without taking the
	// speed trend out, this reads several times too high
	ride(&c, 4, 50, 1.2, 200, -1);
	uint16_t p = ride_ramp(&c, 10, 50, 75, 1.2, 200, -1);
	printf("  speeding up: 200 W -> %u\n", p);
	ASSERT_TRUE(effort_is_valid());
	ASSERT_TRUE(p >= 170 && p <= 230);

	return 1;
}

static int test_effort_holds_through_a_gear_change(void)
{
	crank_t c;
	boot();
	crank_init(&c, 7, 0);
	learn(&c);

	uint16_t before = ride(&c, 10, 60, 1.2, 200, -1);

	// shifting up at the same speed drops the cadence 60 -> 40 rpm in a
	// quarter revolution: no jump in the estimate while that settles
	uint16_t highest = 0;
	for (int i = 0; i < 12; ++i)
	{
		uint16_t p = ride_ramp(&c, 0.25, i == 0 ? 60 : 40, 40, 1.8, 200, -1);
		highest = p > highest ? p : highest;
	}
	printf("  gear change: 200 W -> %u, highest %u during the shift\n", before, highest);
	ASSERT_TRUE(highest <= 300);

	return 1;
}

static int test_effort_pauses_while_the_throttle_drives(void)
{
	crank_t c;
	boot();
	crank_init(&c, 8, 0);

	// no learning while the throttle could be spinning the chainring
	c.throttle = true;
	ride(&c, 100, 80, 3.0, 150, -1);
	ASSERT_EQ(0, effort_get_learned_percent());

	c.throttle = false;
	learn(&c);
	ride(&c, 10, 60, 1.2, 200, -1);
	ASSERT_TRUE(effort_is_valid());

	// no estimate as soon as the throttle is used...
	c.throttle = true;
	ride(&c, 0.05, 60, 1.2, 200, -1);
	ASSERT_TRUE(!effort_is_valid());
	ASSERT_EQ(0, effort_get_rider_power_w());

	// effort assist keeps the last estimate through a short blip (2 s)...
	uint16_t held = 0;
	ride(&c, 2, 60, 1.2, 200, -1);
	ASSERT_TRUE(effort_get_assist_power_w(&held));
	ASSERT_TRUE(held >= 170 && held <= 230);

	// ...but not through a long one (6 s)
	ride(&c, 4, 60, 1.2, 200, -1);
	ASSERT_TRUE(!effort_is_valid());
	ASSERT_TRUE(!effort_get_assist_power_w(&held));

	// ...and back within two revolutions of letting go
	c.throttle = false;
	uint16_t p = ride(&c, 2, 60, 1.2, 200, -1);
	printf("  after the throttle: 200 W -> %u\n", p);
	ASSERT_TRUE(effort_is_valid());
	ASSERT_TRUE(p >= 170 && p <= 230);

	return 1;
}

static int test_effort_no_learning_on_the_bench(void)
{
	crank_t c;
	boot();
	crank_init(&c, 9, 0);

	// the tool's Bench page is polling: nothing learned
	c.bench = true;
	ride(&c, 60, 80, 3.0, 150, -1);
	ASSERT_EQ(0, effort_get_learned_percent());

	// the tool stops polling: still nothing until the controller is
	// switched off, since the bike may still be on the stand
	c.bench = false;
	ride(&c, 60, 80, 3.0, 150, -1);
	ASSERT_EQ(0, effort_get_learned_percent());

	// ... and resetting the pattern doesn't lift it either
	ASSERT_TRUE(effort_reset_pattern());
	ride(&c, 60, 80, 3.0, 150, -1);
	ASSERT_EQ(0, effort_get_learned_percent());

	return 1;
}

static int test_effort_reset_pattern_forgets_it(void)
{
	crank_t c;
	boot();
	crank_init(&c, 10, 0);
	learn(&c);
	g_hw.wheel_rpm_x10 = 0;
	effort_process();	// saved while stopped
	ASSERT_EQ(100, effort_get_learned_percent());

	ASSERT_TRUE(effort_reset_pattern());
	ASSERT_EQ(0, effort_get_learned_percent());

	// and stays forgotten after a power cycle
	effort_init();
	ASSERT_EQ(0, effort_get_learned_percent());

	return 1;
}

static int test_effort_capture_records_raw_edges(void)
{
	boot();
	uint16_t buf[10], count = 0;
	ASSERT_EQ(EFFORT_CAPTURE_IDLE, effort_read_capture(0, buf, 10, &count));

	effort_start_capture();
	for (int i = 0; i < EFFORT_CAPTURE_EDGES + 5; ++i)
	{
		g_hw.pas_edges[g_hw.pas_edge_count++] = (uint16_t)(100 + i) | (i & 1 ? PAS_EDGE_PAS1 : 0);
		if (g_hw.pas_edge_count == 200)
		{
			effort_process();
		}
	}
	effort_process();

	ASSERT_EQ(EFFORT_CAPTURE_DONE, effort_read_capture(570, buf, 10, &count));
	ASSERT_EQ(EFFORT_CAPTURE_EDGES, count);
	ASSERT_EQ(100 + 570, buf[0]);
	ASSERT_EQ((100 + 571) | PAS_EDGE_PAS1, buf[1]);
	ASSERT_EQ(100 + 575, buf[5] & PAS_EDGE_TICKS_MASK);
	ASSERT_EQ(0, buf[6]);	// past the end

	// lost edges spoil a capture
	effort_start_capture();
	g_hw.pas_edges[g_hw.pas_edge_count++] = 123;
	effort_process();
	g_hw.pas_edge_overflow = true;
	effort_process();
	ASSERT_EQ(EFFORT_CAPTURE_OVERFLOW, effort_read_capture(0, buf, 10, &count));
	ASSERT_EQ(1, count);
	ASSERT_EQ(123, buf[0]);

	return 1;
}

static int test_effort_learning_survives_a_glitch(void)
{
	crank_t c;
	boot();
	crank_init(&c, 11, 0);

	// a lost edge early on: too little is learned to find the crank with,
	// so learning starts again rather than stopping for good
	ride(&c, 8, 80, 3.0, 150, -1);
	ride(&c, 1, 80, 3.0, 150, 10);
	ride(&c, 60, 80, 3.0, 150, -1);
	uint8_t early = effort_get_learned_percent();
	ASSERT_TRUE(early >= 25 && early <= 31);

	// a lost edge later: the crank is found again and learning carries on
	ride(&c, 1, 80, 3.0, 150, 10);
	ride(&c, 30, 80, 3.0, 150, -1);
	ASSERT_TRUE(effort_get_learned_percent() >= early + 10);

	ride(&c, 150, 80, 3.0, 150, -1);
	ASSERT_EQ(100, effort_get_learned_percent());
	uint16_t p = ride(&c, 10, 60, 1.2, 200, -1);
	printf("  learned through two glitches: 200 W -> %u\n", p);
	ASSERT_TRUE(p >= 180 && p <= 230);

	return 1;
}

// Different sensors (magnet errors) must read alike. The pattern has to be an
// average of many revolutions for that: one revolution's timing noise in it
// scatters the readings from sensor to sensor.
static int test_effort_reads_alike_on_different_sensors(void)
{
	int lowest = 1000, highest = 0;
	for (unsigned seed = 20; seed < 32; ++seed)
	{
		crank_t c;
		boot();
		crank_init(&c, seed, 0);
		// cadence drifts a little while learning, as it does on the road
		ride_ramp(&c, 260, 77, 83, 3.0, 150, -1);
		// a middle gear at 20 km/h, where the wobble is small and errors in
		// the pattern show most
		int p = ride_ramp(&c, 16, 74, 76, 2.0, 150, -1);
		lowest = p < lowest ? p : lowest;
		highest = p > highest ? p : highest;
	}
	printf("  12 sensors, 150 W at 20 km/h: %d to %d\n", lowest, highest);
	ASSERT_TRUE(lowest >= 108 && highest <= 180);

	return 1;
}

// The estimate looks at half a revolution at a time, so anything wrong with
// one half of the learned pattern shows as a reading that swings once per
// revolution. A gear change straight after cruising must not get learned.
static int test_effort_reads_the_same_all_round_the_crank(void)
{
	crank_t c;
	boot();
	crank_init(&c, 2, 0);
	ride_ramp(&c, 260, 77, 83, 3.0, 150, -1);

	// straight into a low gear at a lower cadence
	ride_ramp(&c, 6, 59, 60, 1.6, 150, -1);
	int lowest = 1000, highest = 0;
	for (int i = 0; i < 16; ++i)
	{
		int p = ride_ramp(&c, 0.125, 60 + i * 0.02, 60.02 + i * 0.02, 1.6, 150, -1);
		lowest = p < lowest ? p : lowest;
		highest = p > highest ? p : highest;
	}
	printf("  150 W over two revolutions, an eighth at a time: %d to %d\n", lowest, highest);
	ASSERT_TRUE(lowest >= 125 && highest <= 185);

	return 1;
}

void test_effort_run(void)
{
	RUN_TEST(test_effort_learns_while_cruising);
	RUN_TEST(test_effort_tracks_rider_power_on_a_climb);
	RUN_TEST(test_effort_invalid_above_max_speed);
	RUN_TEST(test_effort_realigns_after_power_cycle);
	RUN_TEST(test_effort_recovers_from_a_missed_edge);
	RUN_TEST(test_effort_tracks_rider_power_while_speeding_up);
	RUN_TEST(test_effort_holds_through_a_gear_change);
	RUN_TEST(test_effort_pauses_while_the_throttle_drives);
	RUN_TEST(test_effort_no_learning_on_the_bench);
	RUN_TEST(test_effort_reset_pattern_forgets_it);
	RUN_TEST(test_effort_capture_records_raw_edges);
	RUN_TEST(test_effort_learning_survives_a_glitch);
	RUN_TEST(test_effort_reads_alike_on_different_sensors);
	RUN_TEST(test_effort_reads_the_same_all_round_the_crank);
}
