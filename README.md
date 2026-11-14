# bbs-fw-ai

**Making a cadence-sensor BBSHD ride like a torque-sensing e-bike, through an AI-assisted rewrite
of its firmware.**

This is a fork of [bbs-fw](https://github.com/danielnilsson9/bbs-fw), the open-source firmware for
Bafang's mid-drive controllers, developed with Claude working alongside the rider: reading the
firmware, writing the changes, testing them in a ride simulator, and tuning the real bike live
from a laptop plugged into it. It's built for one bike: a BBSHD with an SW102T display, a 52 V
battery and 30 A.

## The idea

A BBSHD is a brilliant motor with a blunt way of deciding when to help. Its pedal sensor only says
*the cranks are turning*, so stock assist is a switch: pedal a few pulses and the level's fixed
current arrives, whether you're stamping up a hill or barely turning the cranks, and it hangs on
for a moment after you stop.

A torque-sensing bike feels completely different. Push and it pushes back, ease off and it eases
off, and it feels like your legs got stronger. This project chases that feel without adding a
torque sensor. Instead the firmware reads far more out of the signal it already has: it times every
edge of the pedal sensor, 96 per crank turn, and reads how the rider is pedalling from them.

## What it does on the bike

- **Assist that answers the first push.** Already rolling, assist arrives after about 30° of crank
  instead of 90°. From a standstill, *launch sensing* reads how hard the cranks are being pushed
  over the first pedal pulse and starts in proportion after 15-20° (78° before).
- **Stops when you stop.** The firmware knows when the next pedal pulse is due, so assist cuts
  50-90 ms after the pedals stop instead of coasting on a fixed timeout.
- **More effort, more help.** *Effort sensing* learns the pedal sensor's own pattern, then reads
  rider power from the tiny speed-up and slow-down of the cranks within each pedal stroke.
  *Effort Assist* turns that into motor power: N × what you put in. The rider's watts show live in
  the display's Range field.
- **Power that doesn't fade.** Levels can target watts, so a level feels the same on a full and a
  nearly empty battery, and gear boost adds help in low gears and from a standstill.
- **Smooth everywhere.** Shaped ramps per level, a gentle launch ramp, gear changes that no longer
  read as a surge, and next to no assist when your legs spin without pushing.
- **A battery limit that recovers.** One voltage sag on a climb no longer caps power for the rest
  of the ride.

Every new behaviour has its own setting, so each one can be switched on and felt on its own
([TUNING.md](TUNING.md)).

## How it's built

Every change climbs the same ladder: **simulator → bench → road.**

- **A ride simulator** runs the firmware's real control loop on a laptop against scripted rides
  (pulling away, climbing, gear changes, stopping), with bike physics behind it: mass, gradient,
  rolling resistance and drag. Every behaviour has expected numbers pinned in its scenario, so an
  improvement shows up as a reviewed change to a baseline.
- **A torque-feel benchmark** scores the firmware against an ideal torque-sensor bike that gives
  1.5× the rider's power, out of 65 points. Version 2.2 scored 43.3, and **2.3 scores 57.3.** CI
  fails any change that drops below 50.
- **Live tuning sessions.** With the laptop plugged in where the display goes, Claude drives the
  controller through **bbs-link**: it sets levels, reads live data, changes one setting at a time
  and compares the logs, while the rider pedals and says what they feel. A live dashboard on the
  laptop shows the readings, charts and a running feed of what Claude is doing and why
  ([LIVE.md](LIVE.md)).
- **CI on every push:** the firmware build, unit tests, every ride scenario, the benchmark, and the
  config tool and bbs-link against a virtual controller.

[ROADMAP.md](ROADMAP.md) has the reasoning and the measurements behind each change.

## Where we are

Firmware **2.3.0** is on the bike. It was flashed over upstream 1.4.0 with the config tool's Update
Firmware wizard, and the settings carried over. With the bike on a stand, the post-flash checklist
passed:

- ✅ assist starts and stops within one live sample
- ✅ the brake cuts assist, and assist returns on release
- ✅ the throttle works in the Sport levels
- ✅ the speed sensor reads correctly
- ✅ requested current rises with levels 1 to 5, as configured

The cable's USB adapter locked up twice after hard motor work, so bbs-link now notices a stalled
link, asks for a replug and reconnects at level 0. That fix has been tested in the simulator so far.

## What's next

The features above are proven in the simulator. Next they meet the road:

- **Levels 6 to 9** on the stand, then the first rides.
- **Effort sensing:** the ride test in [TUNING.md](TUNING.md) that settles whether the real pedal
  sensor shows the stroke's wobble as cleanly as the simulator does.
- **Launch sensing**, predictive stop, the rolling start, power levels and gear boost, judged one
  at a time on the bike.
- Further out: a battery model that tells sag from an empty pack, heat-aware power limits, and a
  phone-connected companion for logging every ride.

## Get it

The [**latest release**](../../releases/latest) is rebuilt from `master` after every green CI run.
**BBSFWTool.zip** has the config tool with the matching firmware and flasher bundled. Unzip it,
plug in the programming cable (battery on, not charging), open **Update Firmware** and click
**Start**. It backs up your settings, flashes, checks the result and restores your settings if
needed. [TUNING.md](TUNING.md) walks through it, with a bench checklist for afterwards.

Built and tested for one BBSHD; for any other bike, use
[upstream bbs-fw](https://github.com/danielnilsson9/bbs-fw/releases).

## Safety

bbs-link always starts and stops at level 0, only changes settings with the bike still, backs up
and reads back every change, and never sets Max Current above 30 A. Brakes and the throttle are
wired to the controller, so they work as normal throughout. Don't flash or configure the
controller while the battery is charging.

## Building

[BUILDING.md](BUILDING.md) has everything. In short: the firmware builds with SDCC
(`make all TARGET_CONTROLLER=BBSHD` in `src/firmware`), the tests and simulator with gcc
(`make test && make sim` in `src/firmware/host`), and the config tool and bbs-link with .NET.

## Credits and licence

Built on [bbs-fw](https://github.com/danielnilsson9/bbs-fw) by Daniel Nilsson, who wrote the
firmware and config tool this all stands on. If it's useful to you, consider a
[donation](https://www.paypal.com/donate/?business=LVAYFCMQYN8F4&no_recurring=0&item_name=BBSHD-FW&currency_code=USD)
to upstream. Supported controllers are upstream's BBSHD V1.4 (STC15W4K56S4) and V1.5
(IAP15W4K61S4).

Licensed under the [GNU GPL v3.0](LICENSE), like upstream. Flashing custom firmware voids your
warranty, and you ride it at your own risk.
