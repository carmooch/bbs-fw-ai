# Building

This fork only builds and tests the BBSHD target.

## Firmware

Needs [SDCC](https://sdcc.sourceforge.net/) and GNU Make. On Debian or Ubuntu:

```
sudo apt-get install sdcc
cd src/firmware
make all TARGET_CONTROLLER=BBSHD
```

This writes `bbs-fw.hex` to `src/firmware`. `make clean` removes the build output.

## Host tests

The logic modules (`app.c`, `throttle.c`, `battery.c`, `cfgstore.c`, `eventlog.c`) are built
unmodified with `gcc` against a fake hardware layer, so they run on a normal computer:

```
cd src/firmware/test
make run
```

- `fake/fake_hw.c` implements every hardware function those modules call. Tests set its inputs
  (`g_hw.pas_cadence_rpm_x10`, `g_hw.brake`, `g_hw.now_ms`, ...) and read what the firmware
  commanded (`g_hw.motor_target_current`, ...).
- Each test runs in its own forked process, because the firmware keeps state in statics that
  nothing resets.
- `test_app.c` drives `app_process()` at the same 5ms tick as `main.c`.

## Line endings

Upstream files are a mix of CRLF and LF. Keep each file's existing line endings:
`scripts/check-eol.sh` fails on any line that differs from upstream only by its line ending.

## CI

`.github/workflows/ci.yml` builds the BBSHD hex (uploaded as an artifact, with memory usage in the
run summary), runs the host tests, and runs the line-ending check on every push and pull request.
