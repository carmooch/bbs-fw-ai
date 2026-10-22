// Feasibility model: can the BBSHD controller see rider effort in the crank
// speed wobble within each pedal stroke?
//
// Physics (chain engaged, so crank speed = wheel speed / gear):
//   m_eff dv/dt = T_rider(phi)/(r_w G) + F_motor - F_grade - 0.5 rho CdA v^2 + F_road(t)
//   T_rider(phi) = T_mean (1 + a cos 2phi) + K_leg w^2 sin 2phi   (two legs; leg inertia)
// F_grade is set so the bike is in steady state at the chosen cadence.
//
// Measurement, as bbsx/sensors.c does it: rising edges of PAS1, 24 per crank
// revolution (magnet k at k*15deg + error), detected on the first 100us tick
// after the edge; periods counted in ticks.
//
// Estimator: per revolution, the 2nd harmonic of segment speed
// w_i = (2pi/24) / (p_i * 100us); torque amplitude = A2 * I_eff * 2*w_mean,
// I_eff = m_eff (r_w G)^2, mean torque = amplitude / a_assumed.
//
// usage: ripple_model G cadence_rpm T_mean a a_assumed mag_sigma_deg road_sigma_N
//                     k_leg calibrate seed revs edges tick_us wander
//   calibrate: 0 = assume even magnets, 1 = true magnet positions,
//              2 = positions from learned.txt, 3 = learning mode (prints each
//              segment's share of a revolution, to be averaged into learned.txt)
// prints one line per revolution: true_T est_T ripple_percent

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int N = 24;
static double TICK = 1e-4;
#define NMAX 96

static double randn(void)
{
	double u1 = (rand() + 1.0) / (RAND_MAX + 2.0), u2 = (rand() + 1.0) / (RAND_MAX + 2.0);
	return sqrt(-2 * log(u1)) * cos(2 * M_PI * u2);
}

int main(int argc, char** argv)
{
	if (argc != 15) { fprintf(stderr, "bad args\n"); return 2; }
	N = atoi(argv[12]); TICK = atof(argv[13]) * 1e-6; double wander = atof(argv[14]);
	double G = atof(argv[1]), cad = atof(argv[2]), Tm = atof(argv[3]), a = atof(argv[4]);
	double a_assumed = atof(argv[5]), mag_sigma = atof(argv[6]) * M_PI / 180, road_sigma = atof(argv[7]);
	double k_leg = atof(argv[8]); int calibrate = atoi(argv[9]); srand(atoi(argv[10])); int revs = atoi(argv[11]);

	const double m_eff = 113, rw = 0.356, rho = 1.2, cda = 0.6, dt = 1e-5, road_tau = 1.0 / (2 * M_PI * 10);
	double w0 = cad / 60 * 2 * M_PI, v = w0 * rw * G;
	double F_motor = 60;	// constant motor push (current-controlled), N
	double F_grade = Tm / (rw * G) + F_motor - 0.5 * rho * cda * v * v;

	double edge[NMAX + 1], seg[NMAX];
	for (int k = 0; k < N; ++k) edge[k] = k * 2 * M_PI / N + (k ? mag_sigma * randn() : 0);
	edge[N] = 2 * M_PI;
	for (int k = 0; k < N; ++k) seg[k] = edge[k + 1] - edge[k];

	// calibrate == 2: segment angles learned elsewhere, read from learned.txt
	double learned[NMAX];
	if (calibrate == 2)
	{
		FILE* f = fopen("learned.txt", "r");
		for (int i = 0; i < N; ++i) if (fscanf(f, "%lf", &learned[i]) != 1) return 3;
		fclose(f);
	}
	if (getenv("LEARN")) { srand(atoi(argv[10])); }

	double t = 0, phi = 0, road = 0;
	long last_tick = -1; int k_next = 1; long periods[NMAX]; double Tw = 0; int npend = 0, rev = 0, k0 = 0;
	double I_eff = m_eff * (rw * G) * (rw * G);

	while (rev < revs + 2)
	{
		double w = v / (rw * G);
		// slow random wander of the rider's effort (+/- wander, ~0.5 Hz), like real pedalling
		Tw += -Tw * 3.0 * dt + wander * sqrt(2 * 3.0 * dt) * randn();
		double T = Tm * (1 + Tw) * (1 + a * cos(2 * phi)) + k_leg * w * w * sin(2 * phi);
		road += (-road / road_tau) * dt + road_sigma * sqrt(2 * dt / road_tau) * randn();
		double F = T / (rw * G) + F_motor - F_grade - 0.5 * rho * cda * v * v + road;
		v += F / m_eff * dt;
		double phi_new = phi + v / (rw * G) * dt;
		t += dt;

		// edge crossings within this revolution
		double target = edge[k_next];
		while (phi_new >= target)
		{
			long tick = (long)ceil(t / TICK);
			int k_prev = (k_next + N - 1) % N;
			if (last_tick >= 0)
			{
				periods[k_prev] = tick - last_tick;
				if (++npend == N)
				{
					npend = 0;
					if (rev >= 2 && calibrate == 3)
					{
						// learning mode: print each segment's share of the revolution
						long sum = 0;
						for (int i = 0; i < N; ++i) sum += periods[i];
						for (int i = 0; i < N; ++i) printf("%.6f ", 2 * M_PI * periods[i] / (double)sum);
						printf("\n");
					}
					else if (rev >= 2)
					{
						// segment speeds; remove the mean first, or uneven sample spacing
						// leaks the (large) mean speed into the 2nd harmonic
						double wi[NMAX], ci[NMAX], wsum = 0;
						for (int i = 0; i < N; ++i)
						{
							double ang = calibrate == 1 ? seg[i] : (calibrate == 2 ? learned[i] : 2 * M_PI / N);
							wi[i] = ang / (periods[i] * TICK);
							ci[i] = calibrate == 1 ? (edge[i] + edge[i + 1]) / 2 : (i + 0.5) * 2 * M_PI / N;
							wsum += wi[i];
						}
						double wm = wsum / N, re = 0, im = 0;
						for (int i = 0; i < N; ++i)
						{
							re += (wi[i] - wm) * cos(2 * ci[i]); im -= (wi[i] - wm) * sin(2 * ci[i]);
						}
						double A2 = 2 * sqrt(re * re + im * im) / N;
						double Test = A2 * I_eff * 2 * wm / a_assumed;
						printf("%.2f %.2f %.4f\n", Tm, Test, 100 * A2 / wm);
					}
					rev++;
				}
			}
			last_tick = tick;
			if (k_next == N) { phi_new -= 2 * M_PI; k_next = 1; }
			else k_next++;
			target = edge[k_next];
		}
		phi = phi_new;
	}
	(void)k0;
	return 0;
}
