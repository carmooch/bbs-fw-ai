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

## Host builds: unit tests and ride simulator

`src/firmware/host` builds the firmware's logic with `gcc` so it runs on a normal computer. The
firmware sources are compiled unmodified; only the hardware underneath is replaced.

```
cd src/firmware/host
make test     # unit tests
make sim      # run every ride scenario, writing out/<name>.csv
make plots    # plot each out/<name>.csv to out/<name>.svg (needs python3, no packages)
```

On Windows, `make sim` and `make plots` should work under MSYS2 or WSL (untested so far). The
unit tests need WSL because they fork a process per test. CI runs all of this on every push
anyway, and the plots are downloadable from each run.

### Layout

- `fake/fake_hw.c` stands in for the controller hardware (clock, ADC, motor link, lights,
  EEPROM). Inputs are set on `g_hw`, and what the firmware commanded is read back from it.
- `fake/fake_sensors.c` returns sensor values from `g_hw` directly. Only the unit tests use it.
- `fake/bbsx/stc15.h` and `fake/fake_pins.c` replace the chip's registers with plain variables.
  That lets the simulator run the **real** `bbsx/sensors.c` and drive its PAS, speed and brake pins.
- `test/` holds the unit tests. Each test runs in its own forked process, because the firmware
  keeps state in statics that nothing resets.
- `sim/` holds the ride simulator, its scenarios and the plotting script.

### Ride simulator

`ride_sim <scenario.sim> <out.csv>` runs the real `app.c` and `bbsx/sensors.c` against a
scripted ride, with the controller's timing:

- the sensor interrupt every 100 µs
- the millisecond clock
- `main.c`'s processing every 5 ms

It prints how assist responded each time the rider started and stopped pedalling: time and crank
angle until assist arrived, time to 90% of peak, and time until assist stopped. It also checks
the scenario's `expect` lines. The exit code is nonzero if any expectation fails.

A scenario is a plain text file; `sim/scenario.h` documents the full syntax. For example:

```
title Start pedalling from standstill, ride, stop pedalling
config pas_start_delay_pulses 3      # override any config field before boot
at 1000 cadence 70 over 800          # ramp cadence from 0 to 70 rpm over 800 ms
at 6000 cadence 0
expect 3000 current == 13            # target current, % of max
end 8000
```

`plot.py` overlays several CSVs, which is how a change gets compared against a baseline:

```
python3 sim/plot.py compare.svg before.csv after.csv --from 5800 --to 6400
```

### Simulator limits

The bike model is deliberately simple. Keep these in mind when reading results:

- The rider's cadence is an **input**. Motor power doesn't feed back into cadence or speed, so
  the simulator shows what the firmware *asks for*, not how the bike would accelerate.
- PAS is modelled as a clean quadrature pair with 50% duty. The real BBSHD signal hasn't been
  measured yet (roadmap phase 2); `bike pas_duty <fraction>` changes the duty.
- While pedalling, the wheel turns at cadence × `gear`. Otherwise it coasts down at
  `bike coast_decel` m/s².
- Temperature reads 0 °C, so thermal limiting never engages. The motor controller's own
  behaviour, including its current loop and speed limit, isn't modelled at all.

## Line endings

Upstream files are a mix of CRLF and LF. Keep each file's existing line endings:
`scripts/check-eol.sh` fails on any line that differs from upstream only by its line ending.

## CI

`.github/workflows/ci.yml` runs on every push and pull request:

- Builds the BBSHD hex. It's uploaded as an artifact, with memory usage in the run summary.
- Runs the unit tests and every ride scenario. Scenario results go in the run summary, and the
  CSVs and plots are uploaded as the `ride-sim` artifact.
- Runs the line-ending check.
