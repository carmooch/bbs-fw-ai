# Roadmap

A personal fork of [danielnilsson9/bbs-fw](https://github.com/danielnilsson9/bbs-fw) for one bike:
a **BBSHD** with an **SW102T** display, configured from a Windows laptop with the existing WPF tool.

## Goal

The bike rides like what it is: a cadence-sensor ebike. Assist is on/off. Pedal a few pulses and the
level's fixed current arrives, however hard or fast you pedal. Stop pedalling and it fades out
after a delay. The goal is to get it as close to a torque-sensor feel as a BBSHD can manage.

A BBSHD has no torque sensor, so this is an approximation. These are the things that make a
torque-sensor bike feel natural, and what this hardware can do about each:

| Quality | What we can do on a BBSHD |
|---|---|
| Assist starts the instant you push | Detect pedalling sooner: finer PAS resolution, and start on the first edge when the bike is already rolling |
| Assist stops the instant you stop | Detect "stopped pedalling" from the expected next-pulse time, not a fixed timeout |
| More effort gives more help | Make power a function of cadence (Grin Cycle Analyst-style W/rpm), with a shaped curve per level |
| Smooth, never jerky | Shaped ramps instead of fixed 1%-per-step linear ones; gentle, quick launch from standstill |
| Low effort gives little help | Low start level and a curve that stays low at easy cadence, so soft-pedalling doesn't surge |

### Where the bike starts (measured in the simulator)

With the default config at level 3, the baseline scenarios in `src/firmware/host/sim/scenarios` show:

- **Late start.** Assist waits for 6 PAS pulses, which is 90° of crank, whether the bike is
  parked or already rolling at 20 km/h. Pulling away gently, that's about 0.6 s.
- **Late stop.** Assist stays at full for about 170 ms after the pedals stop, until a 200 ms
  "no pulse" timeout runs out. At 70 rpm a pulse is due every 36 ms, so the firmware could know
  much sooner.
- **Effort is ignored, or worse.** At the same speed, spinning hard at 90 rpm gets 12% current;
  an easy 50 rpm gets 14%. The "keep current" setting tapers assist down as cadence rises.

Each of these has an `expect` line pinned to today's numbers, so every improvement shows up as a
deliberate, reviewed change to a baseline.

There's also an experimental idea: inferring effort from the cadence ripple within each pedal
stroke, or from speed and acceleration. It may or may not work, so it only gets built after
real ride data shows a usable signal.

## How changes get tested

Every change goes up this ladder. No rung gets skipped.

1. **Host tests.** Unit tests of the pure logic, built with `gcc` on a normal computer.
2. **Ride simulator.** The real `app.c` control loop on the host, fed scripted pedalling, speed and
   throttle, producing a motor-current trace over time. Ride-feel changes are compared as traces
   before they are flashed.
3. **Bench.** Bike on a stand, rear wheel off the ground, with a short written checklist per build.
4. **Ride.** Start at low assist levels and low current.

Each change lands on a branch through a pull request, with CI green before merging. A feature is
marked done only once it has been checked against the code and at least bench-tested.

## Phases

Items marked *(old #n)* come from the first fork's feature catalogue on `archive/fork-v1`.

**0. Foundations** (no behaviour change)
- [x] `.gitattributes` plus a CI check that rejects line-ending-only changes, so diffs against upstream stay readable
- [x] CI: build the BBSHD hex and run the host tests
- [x] Host test harness with a fake hardware layer that can link `app.c`
- [x] Ride simulator: scenario scripts in, CSV and plot of target current out

**1. Free win: try it before writing code**
- [ ] Ride with `pas_keep_current_percent` = 100 (set in the WPF tool). This switches off the taper
  that cuts assist as cadence rises. In the simulator, hard pedalling goes from 12% to 14%,
  matching the easy spin, instead of getting less help.

**2. Responsiveness.** Uses pulse timing the firmware already has; no new measurements needed.
- [ ] Predictive stop detection: treat pedalling as stopped once the next pulse is clearly
  overdue at the current cadence, instead of after a fixed 200 ms
- [ ] Faster start when already rolling, instead of always waiting 90° of crank
- [ ] PAS start and stop delay per assist level *(old #2, #40)*
- [ ] Shaped ramp-up: current ramp rate per level *(old #3)* and a launch boost from standstill *(old #4)*

**3. Bench measurements.** Bike on the stand, config cable connected, results through the event log.
- [ ] PAS signal shape: a diagnostic build that sends PAS edge timing to the WPF tool's event log
  while the cranks are turned by hand. Pulse count, duty cycle, and whether PAS1/PAS2 form a usable
  quadrature pair. This decides whether higher PAS resolution is possible.
- [ ] Motor response: commanded current against the actual current the motor controller reports
  (`motor_get_battery_current_x10`). The motor's own control chip has its own current loop, so if
  it adds a lot of lag, that limits what phase 2 can achieve.
- [ ] Higher PAS resolution (more edges per revolution), if the PAS measurement allows it

**4. Proportional assist** (the headline)
- [ ] Simulator physics: rider, bike mass, slope, drag, and motor power feeding back into speed and
  cadence. Proportional assist changes how the rider pedals, so cadence can't stay a fixed input.
- [ ] Assist levels in watts instead of current percent, using measured battery voltage, so a level
  feels the same on a full and a nearly empty battery
- [ ] Cadence-proportional power per level: start level, W/rpm scale, cap
- [ ] Gear-aware assist: the speed/cadence ratio says which gear you're in, and low gear at low
  speed (a climb or a hard start) is a usable proxy for effort. Gear is already a simulator input.
- [ ] WPF tool fields for the above, plus a curve preview

**5. Motor speed locked to cadence**
- [ ] The firmware already gives the motor a maximum speed (`motor_set_target_speed`), but only as a
  fixed percentage per level. Make it follow the rider's cadence plus a small margin, so the motor
  can't run ahead of the pedals ("ghost pedalling"). This needs the phase 3 bench results first,
  to see how the motor's control chip treats a moving speed cap.

**6. Companion device**
- [ ] An ESP32 board spliced into the display cable that passes display traffic through and adds
  its own, like `src/logger` does. From a phone: change settings mid-ride, flip between two
  tunings for back-to-back comparison, and log every ride.
- [ ] Recorded rides replayed in the simulator as scenarios, and used to check the physics model
- [ ] External assist hints: a new protocol command, with a safety timeout, that lets the companion
  scale assist. Unlocks heart-rate-zone assist from a Bluetooth chest strap, gradient-aware assist
  from an accelerometer, and location-based speed zones.

**7. Experiments**
- [ ] Effort inference from cadence ripple within each pedal stroke, or from speed and
  acceleration, only if recorded ride data shows a usable signal

**Small items, whenever they fit**
- [ ] Throttle upper deadband: full power before full twist *(old #5)*
- [ ] Trip and lifetime Wh counting *(old #10)*; pairs with watt-based levels
- [ ] The low-voltage-cutoff filter only ratchets down, so a hard sag keeps power reduced until
  reboot. This only matters within about 15% of empty, and may be deliberate, so check it before
  changing it.
- [ ] Tidy-up: the `util.h` macros (`MIN`/`MAX`/`ABS`) are unparenthesised. No live call site is
  affected; the only one that would break is disabled debug code in `throttle.c`.

**Checked and dropped**
- `system_delay_ms` "49-day hang": it can't realistically happen. `system_ms()` reads the clock
  atomically, the clock steps exactly 1 ms at a time, and the loop polls it continuously. It's
  also only used during start-up.

**Already there:** the SW102T's Range field shows controller/motor temperature. That's
upstream's default on a BBSHD (`DISPLAY_RANGE_FIELD_DATA` in `fwconfig.h`), so it's in these
builds too, just not switchable from the config tool.

## Out of scope

- BBS02 and TSDZ2. Their code stays untouched so the diff against upstream stays clean, but they aren't built or tested.
- The web config tool, profile sharing, config migration from upstream, and anything else aimed at other riders.
- Anything the motor MCU controls: field weakening, phase advance, FOC, or more than 33A.

## Archive

The first attempt at this fork (June 2026) is preserved on the `archive/fork-v1` branch.
