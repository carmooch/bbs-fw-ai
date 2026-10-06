// Host replacement for src/firmware/bbsx/stc15.h.
//
// The real header maps the STC15's special function registers with SDCC
// extensions. Here the registers bbsx/sensors.c touches are plain variables
// (defined in fake_pins.c), so the real sensors.c compiles with gcc and the
// ride simulator can drive its input pins. The pin macros are copied from
// the real header.

#ifndef _STC_15_H_
#define _STC_15_H_

#include <stdint.h>
#include <stdbool.h>

#define IS_BIT_SET(REG, BIT_NUM) ((REG >> BIT_NUM) & 1)
#define SET_BIT(REG, BIT_NUM) (REG |= (1 << BIT_NUM))
#define CLEAR_BIT(REG, BIT_NUM) (REG &= ~(1 << BIT_NUM))

#define EXPAND(x) x

#define SET_PIN_INPUT_(PORT, PIN) CLEAR_BIT(P##PORT##M0, PIN); SET_BIT(P##PORT##M1, PIN)
#define SET_PIN_INPUT(...) EXPAND(SET_PIN_INPUT_(__VA_ARGS__))

#define SET_PIN_QUASI_(PORT, PIN) CLEAR_BIT(P##PORT##M0, PIN); CLEAR_BIT(P##PORT##M1, PIN)
#define SET_PIN_QUASI(...) EXPAND(SET_PIN_QUASI_(__VA_ARGS__))

#define GET_PIN_STATE_(PORT, PIN) P##PORT##_##PIN
#define GET_PIN_STATE(...) EXPAND(GET_PIN_STATE_(__VA_ARGS__))

// timer0 interrupt enable, used by sensors.c for critical sections
extern bool ET0;

// BBSHD input pins (see bbsx/pins.h): PAS1 4,5  PAS2 4,6  speed 2,2  brake 2,4  shift 2,6
extern bool P2_2, P2_4, P2_6, P4_5, P4_6;
extern uint8_t P2M0, P2M1, P4M0, P4M1;

#endif
