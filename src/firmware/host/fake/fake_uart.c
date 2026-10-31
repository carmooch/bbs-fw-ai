// UART for host builds. By default output goes nowhere and nothing comes in
// (unit tests). With fake_uart_open_pty() it's a Linux pseudo-terminal, so
// the ride simulator can act as a controller on the end of a "serial port"
// for the config tool's code (see --serial in sim.c).

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600
#include "fake_uart.h"
#include "uart.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static int master_fd = -1;
static int slave_fd = -1;
static uint8_t rx[256];
static int rx_count;

const char* fake_uart_open_pty(void)
{
	master_fd = posix_openpt(O_RDWR | O_NOCTTY);
	if (master_fd < 0 || grantpt(master_fd) != 0 || unlockpt(master_fd) != 0)
	{
		return NULL;
	}
	fcntl(master_fd, F_SETFL, O_NONBLOCK);

	// Raw from the start: the default line discipline would echo the
	// controller's own replies back to it before a client opens the port.
	// Keeping a slave descriptor open keeps these settings in place.
	const char* name = ptsname(master_fd);
	slave_fd = open(name, O_RDWR | O_NOCTTY);
	struct termios t;
	tcgetattr(slave_fd, &t);
	cfmakeraw(&t);
	tcsetattr(slave_fd, TCSANOW, &t);

	return name;
}

void uart_open(uint32_t baudrate) { (void)baudrate; }
void uart_close() {}

static void fill(void)
{
	if (master_fd >= 0 && rx_count < (int)sizeof(rx))
	{
		ssize_t n = read(master_fd, rx + rx_count, sizeof(rx) - rx_count);
		if (n > 0)
		{
			rx_count += (int)n;
		}
	}
}

uint8_t uart_available()
{
	fill();
	return (uint8_t)(rx_count > 255 ? 255 : rx_count);
}

uint8_t uart_read()
{
	fill();
	if (rx_count == 0)
	{
		return 0;
	}
	uint8_t b = rx[0];
	for (int i = 1; i < rx_count; ++i)
	{
		rx[i - 1] = rx[i];
	}
	rx_count--;
	return b;
}

void uart_write(uint8_t byte)
{
	if (master_fd >= 0)
	{
		while (write(master_fd, &byte, 1) != 1)
		{
			usleep(100);
		}
	}
}

void uart_flush() {}
