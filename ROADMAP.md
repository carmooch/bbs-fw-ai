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

**0. Foundations** (no behaviour change)
- [x] `.gitattributes` plus a CI check that rejects line-ending-only changes, so diffs against upstream stay readable
- [x] CI: build the BBSHD hex and run the host tests
- [x] Host test harness with a fake hardware layer that can link `app.c`
- [x] Ride simulator: scenario scripts in, CSV and plot of target current out

**1. Reliability.** Each fix is proven by a failing test first.
- [ ] `system_delay_ms` uses `!=`, so it can spin for about 49 days if the target tick is skipped
- [ ] Low-voltage-cutoff filter only ratchets down, so one bad sag keeps power reduced until reboot
- [ ] `util.h` macros (`MIN`/`MAX`/`ABS`) are unparenthesised
- [ ] Any defects the simulator turns up

**2. Measure the bike.** We need real data before tuning feel.
- [ ] Ride recorder: a small device spliced into the display cable that logs cadence, speed and current to a file (builds on `src/logger`)
- [ ] Confirm the BBSHD PAS signal on a scope or logic analyser: pulse count, duty cycle, and whether PAS1/PAS2 form a usable quadrature pair
- [ ] Feed recorded rides back into the simulator as scenarios

**3. Responsiveness**
- [ ] Higher PAS resolution (more edges per revolution, if phase 2 confirms it's possible)
- [ ] Faster start when already rolling; predictive stop detection
- [ ] Shaped ramp-up and launch

**4. Proportional assist** (the headline)
- [ ] Cadence-proportional power per level: start level, W/rpm scale, cap
- [ ] WPF tool fields plus a curve preview

**5. Experiments**
- [ ] Effort inference from cadence ripple or speed/acceleration, only if phase 2 data supports it

## Out of scope

- BBS02 and TSDZ2. Their code stays untouched so the diff against upstream stays clean, but they aren't built or tested.
- The web config tool, profile sharing, config migration from upstream, and anything else aimed at other riders.
- Anything the motor MCU controls: field weakening, phase advance, FOC, or more than 33A.

## Archive

The first attempt at this fork (June 2026) is preserved on the `archive/fork-v1` branch.
