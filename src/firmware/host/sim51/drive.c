// Runs effort.c on the generated ride and prints a line per checkpoint. The
// same file is built for the PC (gcc) and for the 8051 (SDCC, run in the
// ucsim simulator): the two outputs must agree, which checks the compiler's
// 16-bit ints and software floating point do what the PC tests assume. On the
// 8051 it also times the calls, in machine cycles.

#include "effort.h"
#include "cfgstore.h"
#include "sensors.h"
#include "system.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __SDCC
#include <8051.h>
#define CODE __code
#else
#include <stdio.h>
#define CODE
#endif

typedef struct { uint8_t n; uint8_t first; uint16_t wheel_rpm_x10; uint16_t wheel_pulse_x16; uint8_t action; } chunk_t;

static const uint8_t CODE edges[] = {
#include "stream_edges.inc"
};
static const chunk_t CODE chunks[] = {
#include "stream_chunks.inc"
};

// --- what effort.c needs from the rest of the firmware ---

config_t g_config;
static uint32_t now_ticks;		// 100us
static uint16_t wheel_rpm;
static uint8_t stored[200];
static uint8_t stored_size;

uint32_t system_ms() { return now_ticks / 10; }
uint16_t speed_sensor_get_rpm_x10() { return wheel_rpm; }
bool pas_is_pedaling_forwards() { return wheel_rpm != 0; }
uint8_t pas_read_edges(uint16_t* dst, uint8_t max, bool* overflow) { (void)dst; (void)max; *overflow = false; return 0; }
// wheel pulses are fed directly below (effort_add_wheel_pulse)
bool speed_sensor_read_pulse(uint16_t* pas_edges_x16) __reentrant { (void)pas_edges_x16; return false; }

bool cfgstore_read_effort_pattern(uint8_t* dst, uint8_t size)
{
	if (size != stored_size) return false;
	memcpy(dst, stored, size);
	return true;
}

bool cfgstore_write_effort_pattern(uint8_t* src, uint8_t size)
{
	memcpy(stored, src, size);
	stored_size = size;
	return true;
}

// --- output and timing ---

#ifdef __SDCC
static volatile uint16_t timer_overflows;
void timer0_isr(void) __interrupt(1) { timer_overflows++; }

static uint32_t cycles(void)
{
	uint16_t o;
	uint8_t h, l;
	do { o = timer_overflows; h = TH0; l = TL0; } while (o != timer_overflows || h != TH0);
	return ((uint32_t)o << 16) | ((uint16_t)h << 8) | l;
}

static void out(char c) { SBUF = c; while (!TI); TI = 0; }
#else
static uint32_t cycles(void) { return 0; }
static void out(char c) { putchar(c); }
#endif

static void print_u32(uint32_t v)
{
	char buf[11];
	uint8_t i = 0;
	do { buf[i++] = (char)('0' + v % 10); v /= 10; } while (v);
	while (i) out(buf[--i]);
	out(' ');
}

static void print_str(const char* s) { while (*s) out(*s++); }

#ifdef __SDCC
// the simulator stops at a breakpoint here
void sim_end(void) { }
#endif

void main(void)
{
	uint16_t e = 0, c;
	uint32_t max_edge = 0, max_process = 0, start, d;

#ifdef __SDCC
	TMOD = 0x21; TH1 = 0xfd; SCON = 0x50; TR1 = 1;	// uart for the output
	TR0 = 1; ET0 = 1; EA = 1;						// timer 0 counts machine cycles
#endif

	memset(&g_config, 0, sizeof(g_config));
	g_config.wheel_size_inch_x10_u16l = (uint8_t)280;
	g_config.wheel_size_inch_x10_u16h = 280 >> 8;
	g_config.effort_mass_kg = 110;
	g_config.effort_stroke_x100 = 80;
	g_config.effort_max_speed_kph = 25;

	effort_init();

	for (c = 0; c < sizeof(chunks) / sizeof(chunks[0]); ++c)
	{
		uint8_t k = chunks[c].first, i;

		wheel_rpm = chunks[c].wheel_rpm_x10;
		for (i = 0; i < chunks[c].n; ++i)
		{
			// pin states after edge k: PAS1 rise, PAS2 rise, PAS1 fall, PAS2 fall
			static const uint16_t CODE bits[4] = { PAS_EDGE_PAS1, PAS_EDGE_PAS1 | PAS_EDGE_PAS2, PAS_EDGE_PAS2, 0 };
			uint16_t ticks = edges[e++];

			if (ticks == 0)
			{
				// escape: a long segment, two bytes
				ticks = (uint16_t)edges[e] << 8 | edges[e + 1];
				e += 2;
			}
			now_ticks += ticks;
			if (ticks > PAS_EDGE_TICKS_MASK)
			{
				ticks = PAS_EDGE_TICKS_MASK;
			}
			effort_set_wheel_rpm_x10(wheel_rpm);

			start = cycles();
			effort_add_edge(ticks | bits[k & 3]);
			d = cycles() - start;
			if (d > max_edge) max_edge = d;

			start = cycles();
			effort_process();
			d = cycles() - start;
			if (d > max_process) max_process = d;

			k = k == 95 ? 0 : k + 1;
		}

		if (chunks[c].wheel_pulse_x16 != 0)
		{
			start = cycles();
			effort_add_wheel_pulse(chunks[c].wheel_pulse_x16);
			d = cycles() - start;
			if (d > max_process) max_process = d;
		}

		if (chunks[c].action == 1)
		{
			uint16_t launch_nm_x10 = 0;
			bool launching = effort_get_launch_torque_nm_x10(&launch_nm_x10);

			print_u32(effort_get_learned_percent());
			print_u32(effort_is_valid());
			print_u32(effort_get_rider_power_w());
			print_u32(effort_get_rider_torque_nm_x10());
			print_u32(effort_is_freewheeling());
			print_u32(launching);
			print_u32(launch_nm_x10);
			out('\n');
		}
		else if (chunks[c].action == 3)
		{
			// stop: bike and pedals standing
			wheel_rpm = 0;
			effort_set_wheel_rpm_x10(0);
			now_ticks += 10000;
			effort_process();
		}
		else if (chunks[c].action == 2)
		{
			// stop (the pattern is saved while standing still), power off and on
			wheel_rpm = 0;
			effort_process();
			now_ticks += 600000;
			effort_init();
		}
	}

#ifdef __SDCC
	print_str("cycles: edge ");
	print_u32(max_edge);
	print_str("process ");
	print_u32(max_process);
	out('\n');
#endif
	print_str("end\n");
#ifdef __SDCC
	sim_end();
	for (;;) ;
#endif
}
