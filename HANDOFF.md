# Handoff: finishing the torque-feel work (branch `torque-feel-2`)

This branch is work in progress on top of `master` (2.2.0). **It does not build for the controller
yet.** `master` is untouched, green, and is what should be flashed until this branch is finished.
Delete this file in the commit that finishes the work.

## Goal

Make the BBSHD ride as close to a torque-sensor bike as the cadence sensor allows, measured by
the new benchmark, without regressing safety. The owner flashes from the CI download, so
`master` must only ever receive a green, complete state.

## Rules (the owner's, not negotiable)

- **Never put session links, account names, emails or local paths in commits, files or PRs.** The
  repo is public. Commit trailer: only `Co-Authored-By: Claude <noreply@anthropic.com>`.
  Before every push, read the diff and the commit messages for anything personal.
- Don't merge to `master` until CI is green on the branch **and** the owner says so.
- Line endings: files mix CRLF and LF. Edit byte-exact (keep each line's ending); run
  `scripts/check-eol.sh` before committing.
- SDCC/8051: 16-bit `int`; no 16/32-bit multiply or divide in the ISR; the directly addressable
  RAM is full (see "The build failure" below).

## How to check things

```bash
cd src/firmware/host
make test                      # unit tests (37 pass on this branch)
make sim                       # 18 ride scenarios, all pass
python3 sim/feel.py            # torque-feel benchmark (make ride_sim first), ~20 s
sim51/run.sh                   # effort.c on a simulated 8051 must match the PC build (needs sdcc, sdcc-ucsim)
cd .. && make clean && make all TARGET_CONTROLLER=BBSHD     # controller build; read bbs-fw.mem
../../scripts/check-eol.sh
src/tool-check/check.sh        # config tool vs firmware (needs dotnet 8, python3); from the repo root
```

Break each new test on purpose once (mutation check) to confirm it catches the fault.

## Done on this branch

1. **Benchmark** `host/sim/feel.py`: launches, effort steps, steady climbs, gear change, ghost
   pedalling, against an ideal torque-sensor bike (motor = 1.5 x rider). Timing tests are averaged
   over 6 runs because single runs are sensitive to where in the stroke a change lands. Simulator
   additions for it: `spin` input (ghost pedalling: cranks freewheel with +/-3% wobble, a guess),
   motor push limited by torque at low speed (5.3 Nm/A, an estimate).
2. **Gear-change step detector** (`speed_stepped`, `dirty_edges` in `effort.c`): holds the
   estimate while a step in crank speed is in the window plus half a revolution.
3. **Steady-only learning**: each revolution's shares go into `snapshot[]`; committed to the
   pattern only if the revolution time moved < 1% and the conditions still hold. Fixes a bug from
   the 2.2 review, where the revolution after a downshift was learned and bent the pattern.
4. **Launch sensing** (config v8 field `launch_sensing`, off by default): from a standstill, crank
   acceleration over the first pulse (15 degrees) gives the rider's torque; assist starts after
   ~17 degrees instead of ~78, scaled to the push, capped by the level. `effort_get_launch_torque_nm_x10`,
   `launch_torque_to_level_current` in `app.c`.
5. **Ghost-pedalling detector**: PAS edges per wheel pulse (latched in the sensor ISR, so no lag)
   are the gear while the cranks drive. Drift without a step, or a step that doesn't settle =
   freewheeling: no estimate, no effort assist. `effort_add_wheel_pulse`, `effort_is_freewheeling`,
   `speed_sensor_read_pulse`.
6. Change detection in the wobble averaging loosened from 6 to 4 x noise (ease-off 1.3 s -> 0.7 s,
   no ripple cost); acceleration hold loosened from 5% to 10% per half revolution.

Tried and rejected, don't repeat: a **half-revolution estimation window** (unstable: it can't tell
the rider's wobble from once-per-revolution variation, which the motor's own response creates;
limit cycle at 1/rev) and **estimates every eighth of a revolution** (slower, the per-update change
drops under the detection threshold).

## Scores (measured part, out of 65; simulator only)

| | original 2.2 | this branch |
|---|---|---|
| Launch onset (crank degrees) | 78 | 17 |
| Follows effort < 25 km/h (mean error) | 5.8% | 6.2% |
| Push harder, time to 90% | 2.2 s | 1.1 s |
| Ease off, time to 90% | 0.8 s | 0.7 s |
| Gear change spike/dip | 1.7x | 1.2x |
| Ghost pedalling assist (fraction of driving) | 3.2x | 0.04x |
| **Total** | **43.3** | **57.9** |

The other 35 points (performance, safety, verification) aren't simulated; real-bike validation
(6 points) is 0 until the owner rides it. An honest overall figure today is about 80/100, and 90
is not reachable without a ride test. Don't tune the rubric to get there.

## To do, in order

### 1. The build failure (blocking)

`make all TARGET_CONTROLLER=BBSHD` fails: `Could not get 5 consecutive bytes in internal RAM for
area OSEG`. Cause: one more **bit** variable than `master`. `effort.rel` BSEG went from 1 to 2:
SDCC gave `effort_is_freewheeling()` a bit spill slot (`_effort_is_freewheeling_sloc0_1_0`).
Fix: make it return without a boolean temporary, e.g. keep a `static uint8_t` and
`return lock_state == LOCK_FREE ? 1 : 0;` or mark it `__reentrant` (and in `effort.h`). Then
confirm against `master` that no module's `A DSEG`, `A OSEG` or `A BSEG` size grew:

```bash
for f in *.rel bbsx/*.rel; do grep -H 'A DSEG\|A OSEG\|A BSEG' $f | grep -v 'size 0 '; done
```

`master` values: app DSEG 0x38, battery 4, throttle 4, motor 4, sensors DSEG 8 / OSEG 3, uart 0xA;
BSEG app 3, cfgstore 6, effort 1, throttle 1, eeprom 1, sensors 4. Any new non-reentrant function
doing 16/32-bit or float work needs `__reentrant` (temporaries then go on the stack). After the
build passes: stack must still show 128 bytes available, and check frame sizes of the new
reentrant functions in `effort.asm` (look for `add a,#0x..` after `mov a,sp`); `estimate()` is
0x24. The worst call chain plus an interrupt must stay well under 128.

Uncommitted-then-committed in this handoff commit: the `__reentrant` markers for the launch and
lock functions and `(uint8_t)(N + N / 2 + 8)` for `dirty_edges` (SDCC warned about the constant).

### 2. 8051 cross-check for the new code

Extend `host/sim51/gen.c` / `drive.c` so the generated ride also covers: a gear change (step
detector + hold), a standing start (launch torque; print `effort_get_launch_torque_nm_x10`), and
wheel pulses (`effort_add_wheel_pulse`) including a ghost-pedalling stretch. `drive.c` needs a
stub for `speed_sensor_read_pulse`. Flash budget of the harness is tight (edges are 1 byte each);
shorten the learning ride if needed. `run.sh` must report agreement and no call over ~15 ms.

### 3. Tests still missing

- `test_cfgstore.c`: v7 -> v8 migration (settings survive, `launch_sensing` defaults to 0), and
  update `store_old_config` users.
- `test_effort.c`: launch torque from a standing start (gentle vs hard push, with and without a
  learned pattern, rolling start reads sensibly, cancels 200 ms after the pedals stop).
- `test_app.c`: launch current is capped by the level; freewheeling gives zero assist on an
  effort level and doesn't affect a level without effort assist.
- A sim scenario with `expect` lines for ghost pedalling and for launch sensing (the benchmark
  has no pass/fail).
- Mutation-check each.

### 4. Config tool v8

`src/tool/Model/Configuration.cs`: `CurrentVersion = 8`, `ByteSizeV8 = 246`, `ParseFromBufferV8`
(v7 prefix + 1 byte), write, `CopyFrom`, `GetByteSize`; `BbsfwConnection.cs` case 8;
`ConfigurationViewModel` property; a checkbox "Launch Sensing" in the Effort Sensing section of
`View/SystemView.xaml` with a tooltip. `src/tool-check/config_dump.c` + `Program.cs` for v8.
The firmware's `extcom.c` buffer is already 255 (config max 250 bytes). Run `check.sh`.

### 5. Benchmark in CI, docs, version

- CI host job: run `python3 sim/feel.py` and put its table in the step summary (no pass/fail
  threshold beyond "runs"; or fail under a floor of 50).
- `TUNING.md`: launch sensing (what it does, try on a stand first, off by default) and ghost
  pedalling (what the rider will notice: no assist while spinning slower than the bike; a second or
  two without assist after shifting while coasting). `BUILDING.md`: `feel.py`, `spin`,
  `pas_edge_jitter_deg`, the motor torque limit. `ROADMAP.md`: what 2.3 adds.
- Version 2.3.0 in `version.h` and `bbs-fw-tool.csproj`.

### 6. Known weaknesses to state plainly to the owner (don't paper over)

- Everything is simulator-only. The ghost-pedalling model (3% leg wobble) and the motor constant
  (5.3 Nm/A) are guesses.
- Ghost detection needs the wheel speed sensor; at walking pace one wheel turn takes over a
  second, so detection is slow there.
- After ghost pedalling **and** a gear change, assist stays off until the new gear is proven (a few
  seconds of pedalling while the speed changes).
- The estimate reads 20-40% low above 25 km/h (the rider's effort while the pattern was learned is
  assumed to be 100 W), so effort assist still hands back to the watt-based level there.
- Response to effort changes is about one crank revolution. That is the floor for a stable
  estimate; faster needs a different signal (bike acceleration), which was not built.

## When finished

All of: unit tests, `make sim`, `feel.py` >= 57 measured, `sim51/run.sh`, controller build with
unchanged direct RAM and 128 bytes of stack, `check-eol.sh`, `tool-check/check.sh`, CI green on
the branch. Then report to the owner and wait for "merge".
