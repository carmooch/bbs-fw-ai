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

Each of these has an `expect` line pinned to the numbers, so every improvement shows up as a
deliberate, reviewed change to a baseline.

### What 2.0 changes (simulator, not yet ridden)

- **Stop:** assist is cut 50-90 ms after the pedals stop, instead of 160-195 ms.
- **Rolling start:** at 20 km/h assist starts after ~30° of crank (75 ms), instead of 90° (215 ms).
  From a standstill it still waits 90°, on purpose.
- **Effort:** with a power-based level on a 4% hill, the motor gives ~200 W when the rider puts in
  100 W and ~260 W at 250 W; the fixed-current level gives 175 W at both. This is
  *cadence*-proportional: the extra help comes from pedalling faster in the same gear, so a rider
  who shifts up to hold cadence gets about the same assist. That's the ceiling without a torque
  sensor; gear boost adds a second signal for climbs and launches.
- **End of battery:** one sag on a climb no longer limits power for the rest of the ride.

See [TUNING.md](TUNING.md) for switching the new settings on one at a time.

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

**1. Free wins: config changes to try before any code** (all in the WPF tool)
- [ ] `pas_keep_current_percent` = 100. This switches off the taper that cuts assist as cadence
  rises. In the simulator, hard pedalling goes from 12% to 14%, matching the easy spin.
- [ ] Check the battery settings match the 52V (14s) pack: max battery voltage 58.8V, low cutoff
  42V. Upstream's default max is 54.6V, which is for a 48V (13s) pack, and it skews both the
  battery % and the low-voltage limit.
- [ ] Optionally raise max current from 30A to 33A (the hardware limit): about 10% more peak
  power, at the cost of more heat and more battery sag.

**2. End-of-battery power.** The bike slows near the end of the battery. Two causes:
- At a fixed current, power falls with voltage: about 22% less from 58.8V to 46V. This is
  physics, and watt-based levels (phase 5) compensate for it up to the current limit.
- The low-voltage limit makes it worse. It acts on the *lowest* voltage seen, which only moves
  down, so one sag on a climb keeps power reduced until the controller restarts. Baseline:
  `lvc_sag.sim`. On a 52V pack resting at 48V, a 3 s sag to 44V drops level 9 from 26.7A to
  21.3A for the rest of the ride.

Fixes:
- [x] Low-voltage limit that recovers when the battery does: slowly (0.25 V/s), and only while
  the battery is supplying current, so it neither hunts nor lets a pull-away surge after a stop.
  Proven in `lvc_sag.sim`, `lvc_near_empty.sim` and `lvc_restart.sim`. Still needs a bench and
  ride check.
- [ ] Sag compensation: estimate the battery's internal resistance from voltage/current pairs and
  judge the battery on its resting voltage, not the sag. It still protects a genuinely empty
  pack; the BMS remains the last line of defence.
- [ ] Battery % from a lithium-ion discharge curve instead of a straight line, with the same sag
  compensation, so the display is right while riding. Today it shows 33% at 48V resting, where
  a 14s pack is nearer 10-20%, and it only updates after 2 s with no load.

**3. Responsiveness.** Uses pulse timing the firmware already has; no new measurements needed.
- [x] Predictive stop detection: pedalling counts as stopped once the next pulse is twice as late
  as the last, instead of after a fixed 200 ms. A brief slowdown that resumes inside the stop
  delay picks up at the previous current instead of ramping from zero.
- [x] Faster start when already rolling (above 5 km/h): 2 pulses instead of 6
- [x] Shaped ramp-up: current ramp rate per level *(old #3)* and a launch ramp below 10 km/h *(old #4)*
- Not done: PAS start and stop delay per assist level *(old #2, #40)*. Predictive stop and the
  rolling start cover most of what they were for, and they'd need 40 bytes when the config
  message has 29 left.

**4. Bench measurements.** Bike on the stand, config cable connected, results through the event log.
- [ ] PAS signal shape: a diagnostic build that sends PAS edge timing to the WPF tool's event log
  while the cranks are turned by hand. Pulse count, duty cycle, and whether PAS1/PAS2 form a usable
  quadrature pair. This decides whether higher PAS resolution is possible.
- [ ] Motor response: commanded current against the actual current the motor controller reports
  (`motor_get_battery_current_x10`). The motor's own control chip has its own current loop, so if
  it adds a lot of lag, that limits what phase 3 can achieve.
- [ ] Battery: voltage against current under load, to measure the pack's internal resistance
  for phase 2 and the simulator's battery model
- [ ] Higher PAS resolution (more edges per revolution), if the PAS measurement allows it

**5. Proportional assist** (the headline)
- [x] Simulator physics: rider power, bike mass, grade, drag, and motor power feeding back into
  speed and cadence (checked against hand calculations)
- [x] Assist levels in watts: the "Power" PAS variant targets battery power, using the measured
  voltage, so a level gives the same power on a full and a nearly empty battery (up to its current cap)
- [x] Cadence-proportional power per level: start W + W/rpm x cadence, capped by the level's current
- [x] Gear-aware boost: extra assist in low gears from the wheel/crank ratio, full boost from a
  standstill
- [x] WPF tool fields for all of the above, plus a power-vs-cadence preview per level

**6. Motor speed locked to cadence**
- [x] The motor's speed cap (`motor_set_target_speed`) follows the rider's cadence plus a margin,
  so the motor can't run ahead of the pedals. **Off by default and untested on hardware:** how the
  motor's control chip treats a moving speed cap is still unknown (phase 4). Try it on the stand first.

**7. Performance and range**

Peak power is capped by hardware (33A, 63V), so the gains here come from not wasting power and
heat that's already available.
- [ ] Simulator battery and heat models: a discharge curve plus internal resistance, and a simple
  motor heating model, calibrated against bench and ride data
- [ ] Heat budget instead of a cliff. Today power starts cutting at 80 °C and is down to 20% at
  85 °C, so a long climb gets full power until it suddenly doesn't. Ease off earlier and more
  gently, predicting winding heat from current, because the sensor on the housing lags the
  windings. Raising the 85 °C limit itself stays off the table until warnings and logging exist.
- [ ] Lugging protection: limit current at very low cadence, where the motor turns the most power
  into heat. This helps both sustained climbing and range.
- [ ] Trip and lifetime Wh counting *(old #10)*, and Wh per km
- [ ] Range estimate on the SW102T: Wh remaining divided by recent Wh/km. The Range field shows
  temperature today, so choose one or alternate between them.
- [ ] Get-home reserve: below a set battery level, limit power so the last few km are guaranteed
  *(old #55, #56)*
- [ ] Range target mode: "I need 40 km", and assist scales itself to the energy budget, using
  distance from the speed sensor and Wh from current × voltage. Easiest to drive from the
  companion device.

**8. Companion device**
- [ ] An ESP32 board spliced into the display cable that passes display traffic through and adds
  its own, like `src/logger` does. From a phone: change settings mid-ride, flip between two
  tunings for back-to-back comparison, and log every ride.
- [ ] Recorded rides replayed in the simulator as scenarios, and used to check the physics model
- [ ] External assist hints: a new protocol command, with a safety timeout, that lets the companion
  scale assist. Unlocks heart-rate-zone assist from a Bluetooth chest strap, gradient-aware assist
  from an accelerometer, and location-based speed zones.

**9. Experiments**
- [ ] Effort inference from cadence ripple within each pedal stroke, or from speed and
  acceleration, only if recorded ride data shows a usable signal

**Small items, whenever they fit**
- [ ] Throttle upper deadband: full power before full twist *(old #5)*
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
- Hardware changes (battery, chainring size, cooling mods). They can matter more than firmware for
  performance and range, but they're outside this repository.

## Archive

The first attempt at this fork (June 2026) is preserved on the `archive/fork-v1` branch.
