#ifndef _FAKE_UART_H_
#define _FAKE_UART_H_

// Opens a pseudo-terminal for uart_*(); returns the path a client opens
// (e.g. /dev/pts/3), or NULL on failure.
const char* fake_uart_open_pty(void);

#endif
