# Building

## Firmware

Requires [SDCC](https://sdcc.sourceforge.net/) and GNU Make.

On Debian/Ubuntu:

```
sudo apt-get install sdcc
```

Build one of the three supported targets from `src/firmware`:

```
cd src/firmware
make all TARGET_CONTROLLER=BBSHD   # or BBS02, or TSDZ2
```

This produces `bbs-fw.hex` in `src/firmware`, ready to flash. Use `make clean` to remove build
artifacts before switching targets.

## Host unit tests

The hardware-independent logic (throttle response, battery SOC estimation, the shared math
macros in `util.h`, ...) has a separate test suite that builds and runs natively with `gcc`,
no SDCC or target hardware required:

```
cd src/firmware/test
make run
```

This compiles the relevant firmware source files together with mocked-out hardware functions
(see `test/mocks/mock_hw.c`) and a small custom test runner, then executes the resulting binary.
A nonzero exit code means at least one assertion failed.

When adding a new pure-logic module, add its `.c` file to `test/Makefile`'s `SRCS` list and
provide mocks in `test/mocks/` for anything it calls that touches real hardware (ADC, UART,
EEPROM, the motor MCU link, etc).

## CI

`.github/workflows/build.yml` runs on every push and pull request:

- Builds the firmware for BBSHD, BBS02, and TSDZ2 and uploads each `.hex` as a build artifact.
- Builds and runs the host unit test suite.
