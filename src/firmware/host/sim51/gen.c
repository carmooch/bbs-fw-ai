// Generates the ride for the 8051 cross-check (see run.sh): PAS edges and
// wheel sensor pulses, made the same way as test/test_effort.c makes them.
//
// stream_edges.inc: ticks between edges, one byte each; 0 means the next two
// bytes are the ticks (high, low), for segments over 255 ticks.
// stream_chunks.inc: { edges, crank edge index of the first one, wheel rpm
// x10, wheel pulse (PAS edges x16 since the last one, 0 = none), action }.
// A chunk ends at each wheel pulse, which is fed after the chunk's edges.
// action: 0 none, 1 print a checkpoint, 2 stop + power cycle, 3 stop (the
// pedals stand, the bike too).

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define EDGES 96
#define PI 3.14159265358979
#define M_BIKE 110.0
#define STROKE 0.8
#define WHEEL_M (28.0 * 0.0254 * PI)
#define WHEEL_R (28.0 * 0.0254 / 2)

static double edge_deg[EDGES];
static double phi_deg, t, wheel_revs;
static int next_edge;
static long last_tick;
static int edges_since_pulse;
static int speed_unknown;	// the speed sensor reads 0 until the wheel has turned
static double last_edge_t, last_segment_t;

static FILE* fe;
static FILE* fc;
static int chunk_n, chunk_first, total_edges, total_chunks, bytes;
static unsigned chunk_wheel;

static void crank_init(unsigned seed, double start_deg)
{
	srand(seed);
	for (int k = 0; k < EDGES; ++k)
	{
		// uneven channels and magnets, as in test_effort.c
		static const double in_pulse[4] = { 0.0, 3.0, 8.25, 11.25 };
		double u = (rand() + 1.0) / (RAND_MAX + 2.0), w = (rand() + 1.0) / (RAND_MAX + 2.0);
		double err = 0.3 * sqrt(-2 * log(u)) * cos(2 * PI * w);
		edge_deg[k] = (k / 4) * 15.0 + in_pulse[k % 4] + err;
	}
	phi_deg = start_deg;
	next_edge = 0;
	while (next_edge < EDGES && edge_deg[next_edge] <= start_deg)
	{
		next_edge++;
	}
	next_edge %= EDGES;
	last_tick = -1;
}

static void end_chunk(unsigned pulse, int action)
{
	if (chunk_n > 0 || pulse || action)
	{
		fprintf(fc, "\t{ %d, %d, %u, %u, %d },\n", chunk_n, chunk_first, chunk_wheel, pulse, action);
		total_chunks++;
	}
	chunk_n = 0;
}

static void emit(long ticks, int k, unsigned wheel)
{
	if (chunk_n > 0 && (wheel != chunk_wheel || chunk_n == 24))
	{
		end_chunk(0, 0);
	}
	if (chunk_n == 0)
	{
		chunk_first = k;
		chunk_wheel = wheel;
	}
	if (ticks > 65535)
	{
		ticks = 65535;
	}
	if (ticks > 255 || ticks == 0)
	{
		fprintf(fe, "0,%ld,%ld,", ticks >> 8, ticks & 255);
		bytes += 3;
	}
	else
	{
		fprintf(fe, "%ld,", ticks);
		bytes++;
	}
	if (++total_edges % 24 == 0)
	{
		fprintf(fe, "\n");
	}
	chunk_n++;
}

// One time step: the crank turns by w (rad/s), the wheel by wheel_rps.
static void step(double w, double wheel_rps, double dt)
{
	double phi = fmod(phi_deg, 360.0);
	phi_deg += w * 180 / PI * dt;
	t += dt;
	unsigned wheel_x10 = speed_unknown ? 0 : (unsigned)(wheel_rps * 60 * 10);

	double rel = fmod(phi_deg, 360.0);
	if (w > 0 && (next_edge == 0 ? (rel < phi) : (rel >= edge_deg[next_edge])))
	{
		long tick = (long)ceil(t / 1e-4);
		if (last_tick >= 0)
		{
			emit(tick - last_tick, next_edge, wheel_x10);
		}
		last_tick = tick;
		last_segment_t = t - last_edge_t;
		last_edge_t = t;
		edges_since_pulse++;
		next_edge = (next_edge + 1) % EDGES;
	}

	// one wheel magnet: a pulse each turn, latched as the sensor ISR does it
	// (whole edges, plus how far into the next segment, by the last one's time)
	double before = wheel_revs;
	wheel_revs += wheel_rps * dt;
	if (floor(wheel_revs) != floor(before))
	{
		double frac = last_segment_t > 0 ? (t - last_edge_t) / last_segment_t : 0;
		unsigned part = frac >= 1 ? 15 : (unsigned)(frac * 16);
		end_chunk(edges_since_pulse * 16 + part, 0);
		edges_since_pulse = 0;
	}
}

// The rider pedals with power P (W) at a cadence going from cad_from to
// cad_to, the cranks driving the wheel in a gear.
static void ride(double revs, double cad_from, double cad_to, double gear, double P, int action)
{
	const double dt = 1e-5;
	double start_deg = phi_deg, end_deg = phi_deg + revs * 360;

	while (phi_deg < end_deg)
	{
		double cadence = cad_from + (cad_to - cad_from) * (phi_deg - start_deg) / (end_deg - start_deg);
		double w0 = cadence / 60 * 2 * PI;
		double v = cadence / 60 * gear * WHEEL_M;
		double r = P * STROKE / (2 * M_BIKE * v * v * w0);
		double w = w0 * (1 + r * cos(2 * fmod(phi_deg, 360.0) * PI / 180));
		step(w, cadence / 60 * gear, dt);
	}
	end_chunk(0, action);
}

// Ghost pedalling: the bike rolls on (slowing a little) while the legs turn
// the cranks slower than the chainring, so they freewheel, with the legs'
// own unevenness on them.
static void ghost(double seconds, double spin_rpm, double wheel_rps_from, double wheel_rps_to, int action)
{
	const double dt = 1e-5;
	double t0 = t;
	while (t < t0 + seconds)
	{
		double f = (t - t0) / seconds;
		double w = spin_rpm / 60 * 2 * PI * (1 + 0.03 * cos(2 * fmod(phi_deg, 360.0) * PI / 180));
		step(w, wheel_rps_from + (wheel_rps_to - wheel_rps_from) * f, dt);
	}
	end_chunk(0, action);
}

// A standing start: crank torque T (Nm) on the bike's mass in a gear, no
// motor yet, for a number of revolutions.
static void launch(double revs, double T, double gear, int action)
{
	const double dt = 1e-5;
	double lever = WHEEL_R * gear;				// metres of road per radian of crank
	double accel = T / (M_BIKE * lever * lever);	// rad/s^2 at the crank
	double w = 0, end_deg = phi_deg + revs * 360;

	// the pedals have stood: the first edge comes after a long gap
	last_tick = (long)ceil(t / 1e-4) - 30000;
	speed_unknown = 1;
	while (phi_deg < end_deg)
	{
		w += accel * dt;
		step(w, w / (2 * PI) * gear, dt);
	}
	speed_unknown = 0;
	end_chunk(0, action);
}

static void stand(double seconds, int action)
{
	t += seconds;
	end_chunk(0, action);
}

int main(void)
{
	fe = fopen("stream_edges.inc", "w");
	fc = fopen("stream_chunks.inc", "w");

	crank_init(2, 0);
	ride(205, 80, 80, 3.0, 150, 1);		// learn while cruising
	ride(8, 60, 60, 1.2, 100, 1);		// climb: easy, medium, hard
	ride(8, 60, 60, 1.2, 200, 1);
	ride(8, 60, 60, 1.2, 350, 1);
	ride(8, 50, 75, 1.2, 200, 1);		// speeding up
	ride(0.25, 60, 45, 1.6, 200, 0);	// shift up at the same speed
	ride(6, 45, 45, 1.6, 200, 1);
	ghost(3, 30, 45.0 / 60 * 1.6, 40.0 / 60 * 1.6, 1);	// ghost pedalling
	ride(6, 42, 42, 1.6, 200, 1);		// driving again
	stand(1, 3);						// stop
	launch(0.2, 15, 1.6, 1);			// gentle push-off (in the gear it stopped in)
	stand(1, 3);
	launch(0.2, 45, 1.6, 1);			// hard push-off
	ride(10, 60, 60, 1.2, 200, 2);		// stop, save, power cycle
	crank_init(2, 137.0);				// same sensor, crank left elsewhere
	ride(10, 60, 60, 1.2, 200, 1);

	fclose(fe);
	fclose(fc);
	fprintf(stderr, "%d edges (%d bytes) in %d chunks\n", total_edges, bytes, total_chunks);
	return 0;
}
