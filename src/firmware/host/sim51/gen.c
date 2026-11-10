// Generates the ride for the 8051 cross-check (see run.sh): PAS
// edges of a ride, made the same way as test/test_effort.c makes them, as
// one byte of ticks per edge plus a table of chunks.
//
// chunk: edges, crank edge index of the first one, wheel rpm x10, action
// after the chunk (0 none, 1 print a checkpoint, 2 stop + power cycle).

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define EDGES 96
#define PI 3.14159265358979

static double edge_deg[EDGES];
static double phi_deg, t;
static int next_edge;
static long last_tick;

static FILE* fe;
static FILE* fc;
static int chunk_n, chunk_first, total_edges, total_chunks;
static unsigned chunk_wheel;

static void crank_init(unsigned seed, double start_deg)
{
	srand(seed);
	for (int k = 0; k < EDGES; ++k)
	{
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
	t = 0;
	last_tick = -1;
}

static void end_chunk(int action)
{
	if (chunk_n > 0 || action)
	{
		fprintf(fc, "\t{ %d, %d, %u, %d },\n", chunk_n, chunk_first, chunk_wheel, action);
		total_chunks++;
	}
	chunk_n = 0;
}

static void emit(long ticks, int k, unsigned wheel)
{
	if (ticks > 255)
	{
		fprintf(stderr, "ticks %ld don't fit a byte\n", ticks);
		exit(1);
	}
	if (chunk_n > 0 && (wheel != chunk_wheel || chunk_n == 24))
	{
		end_chunk(0);
	}
	if (chunk_n == 0)
	{
		chunk_first = k;
		chunk_wheel = wheel;
	}
	fprintf(fe, "%ld,%s", ticks, ++total_edges % 24 == 0 ? "\n" : "");
	chunk_n++;
}

static void ride(double revs, double cad_from, double cad_to, double gear, double P, int action)
{
	const double m = 110, a = 0.8, circumference = 28.0 * 0.0254 * PI, dt = 1e-5;
	double start_deg = phi_deg, end_deg = phi_deg + revs * 360;

	while (phi_deg < end_deg)
	{
		double cadence = cad_from + (cad_to - cad_from) * (phi_deg - start_deg) / (end_deg - start_deg);
		double w0 = cadence / 60 * 2 * PI;
		double v = cadence / 60 * gear * circumference;
		double r = P * a / (2 * m * v * v * w0);
		double phi = fmod(phi_deg, 360.0);
		double w = w0 * (1 + r * cos(2 * phi * PI / 180));
		phi_deg += w * 180 / PI * dt;
		t += dt;

		double rel = fmod(phi_deg, 360.0);
		if (next_edge == 0 ? (rel < phi) : (rel >= edge_deg[next_edge]))
		{
			long tick = (long)ceil(t / 1e-4);
			if (last_tick >= 0)
			{
				emit(tick - last_tick, next_edge, (unsigned)(cadence * gear * 10));
			}
			last_tick = tick;
			next_edge = (next_edge + 1) % EDGES;
		}
	}
	end_chunk(action);
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
	end_chunk(2);						// stop, save, power cycle
	crank_init(2, 137.0);				// same sensor, crank left elsewhere
	ride(10, 60, 60, 1.2, 200, 1);

	fclose(fe);
	fclose(fc);
	fprintf(stderr, "%d edges in %d chunks\n", total_edges, total_chunks);
	return 0;
}
