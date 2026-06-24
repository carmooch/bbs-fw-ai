# bbs-fw Enhancement Plan

A working document of proposed enhancements to Daniel Nilsson's open-source firmware for the Bafang BBSHD/BBS02 motor controller.

The goal is not to replace the existing firmware. It is to ship features that make the BBSHD feel more like a modern torque-sensing ebike, expose useful configuration that's currently hidden or hard-coded, and improve the day-to-day experience of installing, tuning, and diagnosing the system.

---

## Context

The BBSHD is a 1000W mid-drive motor sold by Bafang since around 2014. It's popular because it's cheap, powerful, and serviceable, but its stock firmware is conservative and its stock display protocol is limited. Daniel Nilsson's bbs-fw replaces the stock firmware on the controller's main MCU (an 8051-derived STC15W4K56S4 or IAP variant) while leaving the second motor-control MCU untouched. That second MCU runs the closed-source BLDC commutation loop and accepts only a fixed set of UART opcodes — which sets the hard ceiling on what any firmware on the main MCU can change.

bbs-fw is small (around 5000 lines of C across ~30 files), has run since 2022, and is maintained largely by one person. It has a slow but steady release cadence, an active forum thread on Endless Sphere, and a small community of users who share tuned profiles via screenshots and forum posts.

## What's possible and what isn't

The motor MCU is the ceiling on real performance gains. These features cannot be added in any firmware that runs on the main MCU:

| Capability | Why not |
|---|---|
| Field weakening | Motor MCU runs the commutation loop; no opcode exists to request weakening current |
| Phase advance tuning | Same — handled internally by motor MCU |
| Phase current ≠ battery current | Motor MCU exposes only a single combined current limit |
| Sinusoidal commutation / FOC | Motor MCU uses trapezoidal commutation; not switchable |
| Hall sensor auto-tune | No motor MCU opcode for it |
| Regen braking | Mid-drive freewheel makes this useless even if hardware allowed it |
| Above 33A peak | Shunt resistor and ADC scaling — hardware modification only |

These limits matter because the project's pitch can't be "more power." bbs-fw already runs the BBSHD at higher voltage and current than Bafang intended (63V max vs 52V stock, 33A vs 30A stock). The realistic pitch is "the same hardware, made to feel like a more expensive ebike."

What's actually available to firmware on the main MCU:

- Anything that shapes when, how, and how much power gets requested from the motor MCU
- Anything related to display protocol, configuration, persistent storage, diagnostics
- Anything that uses available sensor inputs (PAS, throttle, speed, brake, temperature, battery voltage and current)
- Anything that improves the install, tune, and diagnose loop for users

## Codebase observations

A quick review surfaced a number of issues worth addressing before adding features. None are urgent, but several would cause confusion later if left unfixed.

### Defects worth fixing

- **`system_delay_ms` can hang for ~49 days.** Both BBSx and TSDZ2 implementations use `while (system_ms() != end)`. If an interrupt latency causes the load to skip the exact target tick, the loop spins until rollover. Use `< end` or signed-diff comparison.
- **Busy-wait in `motor_init` without watchdog yield.** `while (system_ms() < 100);` works because the current WDT prescaler is generous, but a future change to the watchdog window would trip it.
- ~~**Duplicate `#define OPCODE_UNKNOWN5`** in `bbsx/motor.c`~~ — fixed. Confirmed harmless (both definitions were `0x6C`), just a redundant define.
- ~~**Unparenthesised macros in `util.h`.**~~ — fixed. `MAX`, `MIN`, `ABS` now have outer parens so they're safe as sub-expressions. They still evaluate their arguments more than once (documented in a comment), so still don't pass an expression with side effects.
- **8-bit checksum on the wire protocol.** Single-bit error detection is only ~50%; CRC-8 is the same code size and dramatically more robust. The display-side protocol is constrained by compatibility, but the config-tool channel is the project's own.
- ~~**`process_write_config` length-check bypass.**~~ — fixed. The checksum now validates exactly the bytes the host declared (`length`), not `sizeof(config_t)`. The memcpy into `g_config` was already correctly gated on `length == sizeof(config_t)`, so this was a checksum-validation bug (computed over stale buffer contents on a version mismatch) rather than a memory-safety one — `msg_len` is bounded by `BUFFER_SIZE` before this code runs, so it can't read out of bounds.
- **`convert_wheel_speed_kph_to_rpm` uses float math on an 8051.** Pulls in the entire soft-float library for one cold-path conversion. Easily replaced with fixed-point.
- **`flt_min_bat_volt_x100` only ratchets down.** Once the low-voltage filter drops on a sag, it can only rise on a new reading above the current filter — which the guard explicitly blocks. After one bad sag the LVC ramp engages permanently until reboot. Needs a slow rise term.
- **Speed limit ramp range is fixed at 3 km/h.** Hard-coded in `SPEED_LIMIT_RAMP_DOWN_INTERVAL_KPH`. Riders running offroad with no real cap want this much smaller.

### Code-quality items

- ~~Misspellings throughout (`yeild`, `proccess`, `disenage`, `releasig`, `EEPROM_ERROR_LENGHT`).~~ — fixed, including the equivalent typo in the config tool's error dialogs ("timeout occured").
- ~~`compute_checksum` is duplicated across `extcom.c`, `motor.c`~~ — fixed, now a single shared implementation in `util.h`.
- Hand-split `_u16l`/`_u16h` byte pairs throughout the config struct, then reassembled with `EXPAND_U16` macros at every read site. Could be a single serialisation helper.
- ~~`app_set_lights` has an 11-case if-ladder for assist levels 0-9.~~ — fixed, collapsed to `ASSIST_MODE_SELECT_PAS0_LIGHT + assist_level`.
- `process_read_status` is a `:TODO:` stub that silently returns 0 — a config tool requesting status will hang.
- ~~`DISPLAY_RANGE_FIELD_DATA` system in `fwconfig.h` defines three options but the handler isn't visible in `extcom.c`.~~ — turned out to be a false positive. The handler exists in `extcom.c` (`process_bafang_display_read_range`); it was missed originally because it's a compile-time `#if`, not a runtime branch.

### Infrastructure gaps

- ~~No CI.~~ — done. GitHub Actions builds BBSHD, BBS02, and TSDZ2 on every push.
- ~~No unit tests.~~ — started. Host-side tests cover the pure-logic layer (`throttle.c`, `battery.c`, `cfgstore.c`, `util.h`) with mocked hardware inputs; `app.c` and `extcom.c` aren't covered yet — `extcom.c` in particular would need a much larger mocking surface (uart, sensors, motor, app) for the payoff so far.
- No human-readable changelog tied to config version bumps. Users updating across versions have no idea what changed.
- ~~EEPROM config has no magic number~~ — fixed. A magic byte is now checked before version/length/checksum, so a fresh/erased chip or foreign data is rejected outright. One-time layout change made now, before any release ships, so there's no migration concern.

## The feature catalog

Numbered for stable reference. Effort and risk are subjective rough scales. "Needs config tool?" indicates whether the feature requires changes to the config tool to be usable by riders (not just the firmware).

### Ride feel

| # | Feature | Effort | Risk | Tool? |
|---|---|---|---|---|
| 1a | RPM-proportional PAS (Grin-style cadence scaling) | Low | Low | Yes |
| 1b | Virtual torque sensor (load-aware PAS, cadence + motor current) | High | High | Yes |
| 2 | Per-level PAS startup delay | Low | Low | Yes |
| 3 | Per-level current ramp rate | Low | Low | Yes |
| 4 | Launch boost (faster ramp first 1–2s from standstill) | Low | Low | Yes |
| 5 | Throttle upper deadband | Trivial | Low | Yes |
| 6 | Per-level throttle bang-bang flag | Low | Medium | Yes |
| 7 | Softer motor disengage when PAS stops | Low | Low | Optional |
| 40 | Per-level PAS stop delay | Low | Low | Yes |
| 41 | Per-level `pas_keep_current_percent` | Low | Low | Yes |
| 42 | Nonlinear current ramp curve | Medium | Low | Yes (LUT) |
| 43 | Brake-pre-release current memory | Low | Medium | Yes |
| 45 | Predictive PAS power | Medium | High | Yes |

### Display, feedback, diagnostics

| # | Feature | Effort | Risk | Tool? |
|---|---|---|---|---|
| 8 | Battery percent in display field | Low | Low | Yes |
| 9 | Configurable display field 2 (volt/percent/temp/Wh) | Low | Low | Yes |
| 10 | Trip Wh + lifetime Wh integration | Medium | Low | Yes |
| 11 | Audible thermal warning beeps | Medium | Low | No |
| 12 | Audible LVC warning beep | Low | Low | No |
| 13 | Audible watchdog-reset boot beep | Trivial | Low | No |
| 14 | Status LED blink codes | Medium | Low | No |
| 34 | ~~Display temperature in Calories or Range field~~ — done | Trivial | Low | Yes |
| 47 | Status code on display when motor stops unexpectedly | Low | Low | No |
| 50 | Diagnostic stream mode (cycle telemetry through display field) | Low | Low | Yes |
| 57 | Ride statistics in EEPROM (top speed, max temp, total Wh) | Medium | Low | Yes |
| 58 | "Black box" 60-second telemetry on motor error | Medium | Low | Yes |

### Configuration tool

| # | Feature | Effort | Risk | Tool? |
|---|---|---|---|---|
| 15 | Live telemetry tab | High | Low | — |
| 16 | Config diff view before write | Medium | Low | — |
| 17 | Named profile slots + import/export | Medium | Low | — |
| 18 | Shareable profile file format | Low | Low | — |
| 19 | Visual assist curve preview | Medium | Low | — |
| 20 | Throttle response curve editor | Medium | Low | Yes (LUT) |
| 21 | Wheel size preset dropdown | Trivial | Low | — |
| 22 | Throttle calibration wizard | Medium | Low | — |
| 23 | Speed sensor magnet test wizard | Low | Low | — |
| 24 | Voltage calibration wizard polish | Low | Low | — |
| 25 | Integrated firmware updater | High | Medium | — |
| 32 | Street-legal speed profile preset (EU 25 / US 32 / S-pedelec 45) | Low | Low | — |
| 46 | First-ride diagnostic wizard | Medium | Low | Yes |
| 65 | Per-version changelog shown after flash | Trivial | Low | — |
| 66 | Natural-language profile generator (offline AI helper) | Medium | Low | — |

### Safety and convenience

| # | Feature | Effort | Risk | Tool? |
|---|---|---|---|---|
| 26 | Auto-off timer after inactivity | Low | Low | Yes |
| 27 | Per-level walk-mode max speed | Low | Low | Yes |
| 28 | Configurable brake cutoff response | Low | **High — safety** | Yes |
| 29 | Hill-hold (TSDZ2 only) | High | High | Yes |
| 30 | Brake light flash pattern | Low | Low | Yes |
| 31 | Per-mode max speed (Default vs Sport) | Low | Low | Yes |
| 35 | Per-mode global throttle disable | Low | Medium | Yes |
| 48 | Self-diagnosing thermal sensor (sanity check) | Low | Low | No |
| 49 | "Limp home" mode on critical errors | Low | Medium | Yes |
| 53 | Anti-rollback assist | Medium | Medium | Yes |
| 54 | Power-on safety check | Low | Low | No |
| 55 | Auto-reduce power below 20% battery | Low | Low | Yes |
| 56 | "Get home" battery reserve | Low | Low | Yes |
| 63 | Tamper-evident config | Medium | Low | — |
| 64 | Speed limit lock (password-protected) | Medium | Low | Yes |

### Performance (within hardware limits)

| # | Feature | Effort | Risk | Tool? |
|---|---|---|---|---|
| 36 | Configurable thermal limit (raise from 85°C) | Low | **High — hardware** | Yes |
| 37 | ~~Configurable `MAX_CADENCE_RPM_X10`~~ — done | Trivial | Low | Yes |
| 38 | Double PAS resolution (sample both edges) | Low | Low | No |
| 39 | Configurable speed limit ramp interval | Low | Low | Yes |
| 44 | Cumulative energy delivery warning | Medium | Low | Yes |

### Ecosystem

| # | Feature | Effort | Risk | Tool? |
|---|---|---|---|---|
| 60 | Wireless config via ESP32 dongle | Medium | Low | — |
| 61 | Eggrider integration / protocol docs | Low | Low | — |
| 62 | Mobile app companion | High | Low | — |

### Code hygiene

| # | Feature | Effort | Risk | Tool? |
|---|---|---|---|---|
| 33 | ~~Clean up `app_set_lights` PAS0–PAS9 ladder~~ | Trivial | Low | No |

## Strategic groupings

A few features that only make sense delivered together.

**Virtual torque sensor bundle: 1a + 19 + 17 + 18.** RPM-proportional PAS is the algorithm. The visual curve preview makes it tunable. Profile slots and the shareable file format let riders trade tuned curves. Together this is the headline differentiator for a community fork — nobody else in the bbs-fw ecosystem has it.

**Power management bundle: 10 + 55 + 56 + 44.** Trip and lifetime Wh tracking, automatic power reduction below 20% battery, a configurable battery reserve, and cumulative energy warnings together make range planning much more reliable on long rides.

**Higher-performance bundle: 36 + 11 + 44 + 48 + 58.** A raised thermal limit is genuinely dangerous if shipped alone. Bundled with audible warnings, energy delivery tracking, thermal sensor sanity checking, and black-box logging on failure, it becomes responsibly usable.

**Onboarding bundle: 21 + 22 + 23 + 46 + 47.** Wheel size presets, calibration wizards, the diagnostic wizard and on-display status codes together transform install from "read the wiki carefully" to "follow the prompts."

**Legal-mode bundle: 31 + 35 + 32.** Per-mode max speed, per-mode throttle disable, and a street-legal preset together let a single bike be properly compliant in Default mode and unrestricted in Sport mode — addressing the specific use case of riders who want both on the same hardware.

## What can't be done

Listed explicitly to save future debate:

- **Field weakening, phase advance, FOC, hall auto-tune, separate phase/battery current limits.** All locked behind the motor MCU.
- **Regen braking on the BBSHD.** Mid-drive freewheel means the motor isn't spinning when the bike is — nothing to regen from.
- **Above 33A peak current.** Shunt resistor and ADC calibration limit, not firmware.
- **Custom labels on the SW102 display.** Display firmware owns its own labels. The only paths are (a) accept a wrong label on a repurposed field, (b) flash custom SW102 firmware, or (c) switch to a display with the right label built in.
- **A "torque sensor" that actually measures torque.** Cadence-derivative inference is the closest a software-only solution can get on a BBSHD. It is a meaningful improvement over fixed-power PAS, but it is not a Bosch.

## Recommended roadmap

A possible order. Cheaper, safer, higher-impact features earlier; ambitious or risky features later, once the infrastructure has proven itself.

**Phase 0 — Infrastructure (no user-visible features).** ~~Fork the repo. Set up GitHub Actions building BBSHD, BBS02 and TSDZ2 hex files on every push. Get the build clean and the release pipeline boring. Document the build process well enough that someone else could reproduce it. Set up a host-side test target for the pure-logic functions.~~ Done. Decide on naming and licence stance — done (stays GPLv3, the fork is named bbs-fw-ai).

**Phase 0.5 — Foundation cleanup (added, not in the original plan).** Before building features on top of a codebase, fix the mechanical, zero-hardware-risk defects and code-quality items above so feature work doesn't inherit them. Done in two passes: mechanical fixes with no hardware exposure first (macros, duplicated code, misspellings), then the wire-protocol/EEPROM robustness fixes since they're about to be touched repeatedly by config-version bumps anyway. The remaining defects (timing/motor-control code, the LVC ratchet bug) genuinely need a real controller on a bench, so they're deliberately deferred to be fixed alongside the feature work that already requires hardware testing, rather than in isolation.

**Phase 1 — Smallest viable improvements.** ~~#34 (temperature in repurposed display field) and #37 (configurable max cadence).~~ Done — `CONFIG_VERSION` bumped to 6, both fields are now config-tool-settable instead of compile-time `#define`s, with per-target defaults preserved. One config field each, one display change each. Proves the release pipeline works end-to-end and gives early users a reason to try the fork. Still untested on real hardware — see the warning at the top of the README.

**Phase 1.5 — Web config tool foundation (added, not in the original plan).** The existing config tool (`src/tool`) is a Windows-only WPF/.NET Framework desktop app. The connection to the controller is just a plain serial port at 1200 baud (`System.IO.Ports.SerialPort` in `BbsfwConnection.cs`) over a USB-serial adapter — exactly what the browser's Web Serial API is for. Moving to a browser-based tool now, before Phase 2 sinks more work into WPF-specific UI, avoids redoing that work later. Scope: a Web Serial connection plus the byte protocol layer (read/write config, checksums, opcodes), and just the System view fields that exist today, not a big-bang rewrite of every assist-level editor, wizard, and the event log up front — those get built directly in the web tool as later phases need them. The web tool only needs to understand this fork's current config format (`CONFIG_VERSION` 6+), not the legacy V1-V5 layouts upstream bbs-fw releases used — a rider on a stock install would use the original WPF tool first. The WPF tool stays in the repo as a fallback, frozen rather than actively developed, until the web tool reaches parity and it can be retired. Web Serial is Chromium-only (Chrome/Edge/Opera/Brave) — no Firefox or Safari — which is a real access tradeoff against dropping the Windows-only install barrier.

**Phase 2 — Ride feel quick wins.** #3 (per-level ramp rate), #5 (throttle upper deadband), #21 (wheel size dropdown in the tool). Each is small. Each is visible. Each requires the config tool to be updated, so this phase forces that work too.

**Phase 3 — Profile sharing.** #17 (named profile slots), #18 (shareable file format), #16 (config diff view), #19 (visual assist curve preview). Together this turns the config tool from a settings editor into something that supports a community. Without this, every feature added later is harder for users to share and discuss.

**Phase 4 — The headline feature.** #1a (RPM-proportional PAS). Reference implementation from the Cycle Analyst V3 manual (Strt Level + Scale Fctr in W/RPM). Per-level config fields. Combined with #19 from the previous phase, riders can see the curve they're tuning. This is the feature that justifies the fork existing.

**Phase 5 — Configuration depth.** #31 (per-mode max speed), #35 (per-mode throttle disable), #32 (street-legal preset), #40 (per-level PAS stop delay), #41 (per-level keep current). The legal-mode bundle plus the per-level granularity that turns the firmware into a real tuning surface.

**Phase 6 — Diagnostics and safety.** #46 (first-ride wizard), #47 (status code on display), #57 (ride stats in EEPROM), #58 (black box on error), #48 (thermal sensor sanity check), #54 (power-on safety check). The unglamorous features that prevent support questions and build trust.

**Phase 7 — Performance.** #36 (raised thermal limit), only after #11 (audible warnings), #44 (energy tracking) and #48 (sensor sanity) are in place. This is the most genuinely dangerous feature on the list and shipping it without the surrounding bundle is irresponsible. Pair with prominent warnings in the config tool.

**Phase 8 — The ambitious one.** #1b (virtual torque sensor with motor current). This is the genuinely novel feature — nothing in the existing ebike custom firmware ecosystem combines cadence-derivative inference with motor current sensing the way bbs-fw could. Months of work, will need beta testers, will need patience. Should not be attempted until everything above it is shipped and stable.

**Open-ended.** The config tool features (#15, #25, #66), the ecosystem features (#60, #61, #62), and the niche/situational items (#29 hill-hold, #63 tamper-evident config, #64 speed lock) are best treated as opportunistic — done if someone has the energy and a specific need, deferred otherwise.

## Prior art

A few references worth knowing about.

**Cycle Analyst V3 (Grin Technologies).** The reference implementation for RPM-proportional PAS. Their manual documents the formula `Output Watts = Strt Level + (Cadence above threshold) × Scale Fctr` with example values of 600W baseline and 5 W/RPM scale. Working range from forum posts: 2–10 W/RPM. This is feature #1a's blueprint.

**TSDZ2 OSF (Casainho et al.).** Open-source firmware for the Tongsheng TSDZ2 mid-drive, which has a real torque sensor. The torque-handling logic, smoothing filters, and ramp curves are directly transferable to a cadence-derivative input on the BBSHD. Worth studying before writing #1b.

**Endless Sphere "User profiles for BBS-FW" thread.** A decade of Kepler-style and similar PAS profile sharing for BBS-FW specifically. The Endless Sphere community has been doing static profile tuning to approximate torque-sensor feel for years; the existence of those profiles is informative about what curves work in the real world.

**Endless Sphere "BBS-FW: Open Source Firmware" thread.** The home of the project. Reading the last few pages gives the current pulse of user requests and pain points.

**ASI BAC controllers / Phaserunner.** The commercial answer to "I want a BBSHD that feels like a Bosch" — a £400+ replacement controller. Useful as a reference point for what features matter most when you're willing to pay for them.

**The 2025 ES thread on torque-sensor settings for the CAN-bus Bafang firmware.** A different open-source ebike firmware effort targeting the newer Bafang motors. Worth tracking because the questions being asked there (what should be user-settable, what good tuning looks like) overlap directly with the questions a feature-rich bbs-fw fork will face.

## Notes on the development approach

Open-source ebike firmware appears to be uniformly hand-written, with no visible adoption of AI-assisted development in the public record. The codebases are small enough that AI assistance is genuinely useful, the maintainers are typically solo and time-constrained, and the bottleneck on shipping features is human-hours rather than code quality. This is an opportunity, but it comes with the obligation to be honest about how the code was produced when relevant, and to take ownership of testing — AI-drafted firmware running on hardware is the developer's responsibility, not the tool's.

The realistic constraint on any fork is not the code, it's the testing. Every feature on this list will need bench testing on a controller, then bike testing under varied conditions, before it's safe to release. This is true regardless of how the code is written. The single biggest mistake to avoid is shipping faster than testing can keep up.

## Final principle

The pitch for a bbs-fw fork is not "more power." It is: **the same hardware, made to feel like a more expensive ebike, with better tooling around install and tuning.** Every feature on this list either contributes to that pitch or supports the infrastructure that makes the pitch credible. Features that don't fit either category — however interesting in isolation — are probably not worth building.
