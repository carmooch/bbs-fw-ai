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

	// no learning in a low gear or below 20 km/h
	ride(&c, 100, 60, 1.2, 150, -1);
	ASSERT_EQ(half, effort_get_learned_percent());

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
}
