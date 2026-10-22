# Tuning bbs-fw-ai 2.1

Everything new since upstream has its own setting, so it can be switched on and judged one change at a
time. If several change at once and the bike feels wrong, there's no telling which one did it.

## Before you flash

1. From the latest green CI run on `master`, download **bbs-fw-BBSHD** (the firmware `.hex`) and
   **BBSFWTool** (the config tool, which needs the .NET 6 Desktop Runtime, the same as the upstream tool).
2. Flash the hex the usual way, with the same STC flashing tool and cable as before (see the
   [upstream wiki](https://github.com/danielnilsson9/bbs-fw/wiki/Flash-Firmware-(BBS02-&-BBSHD))).
   The flashing tool hasn't changed; only the config tool is new. Your existing settings carry
   over: the firmware converts the old config on first boot.
3. Use the 2.1 config tool from now on. Older tools (upstream 1.5, or 2.0) reject the new config
   version, so they can't read or write 2.1 settings.

Going back to upstream firmware later resets the settings to defaults, because upstream doesn't
understand this fork's config. Write your settings down first.

## Already on after flashing

- **Predictive stop** (System → Torque Feel). Assist stops 50-90 ms after your feet do, instead
  of about 0.2 s.
- **Rolling start delay = 1** (System → Torque Feel). Above 5 km/h assist comes in after about 30°
  of crank instead of 90°. From a standstill it still waits for the normal start delay.
- **Low-voltage recovery.** Near the end of the battery, power comes back after a climb instead
  of staying limited until you switch off.

## Switch on in this order

Bench first (bike on a stand, rear wheel off the ground), then a short ride at a low level.

| # | Setting | Where | Start with | What you should feel |
|---|---|---|---|---|
| 1 | Battery voltages | System | Max **58.8 V**, low cutoff **42 V** | Correct battery % and end-of-battery behaviour for a 52 V pack |
| 2 | Keep Current (%) | System → Pedal Assist | **100** | Spinning faster no longer gets *less* assist |
| 3 | Launch Ramp (A/s) | System → Torque Feel | **25-30** | Quicker, still smooth pull-away below 10 km/h |
| 4 | Power PAS on one level | Assist Levels → a level → Variant **Power** | Level 3: Max Current **40 %**, Power Start **100 W**, **2 W/rpm** | Pedal faster, get more help. Use the preview to shape it |
| 5 | Gear Boost | System → Torque Feel | **30 %**; low ratio about your lowest gear + 0.2, high ratio about mid-cassette | More help in low gears on climbs and from a standstill |
| 6 | Per-level Current Ramp | Assist Levels → a level | e.g. **5 A/s** on high levels | Gentler surge when you step up to high levels |
| 7 | Cadence Lock Margin | System → Torque Feel | **20 rpm**, stand test first | The motor can't spin the chainring ahead of your feet |

Notes:

- **Power levels:** in Power mode a level's Max Current (%) becomes a *cap*. Upstream's default
  caps are small (14% at level 3), so raise the cap or the curve will be flat. The preview shows
  battery watts against cadence, with the cap.
- **Gear ratios:** a gear's ratio is chainring teeth ÷ cog teeth. A 42T chainring on a 42T cog is
  1.0; on an 11T cog it's 3.8.
- **Cadence lock is experimental.** Nothing yet shows how the motor's own control chip handles a
  moving speed cap. On the stand, pedal gently with it on. If the motor surges, stutters or feels
  like it's fighting you, set it back to 0.

## Effort sensing (2.1): does it work on this bike?

2.1 tries to tell how hard you're pushing with only the cadence sensor. Each leg pushes hardest
halfway down, so the bike and the cranks speed up and slow down very slightly twice per
revolution. The firmware times every edge of the pedal sensor (96 per revolution) and works
your power out from that wobble. It works in the simulator. Nobody knows yet whether it works on
a real BBSHD: that's what this experiment is for. Nothing changes how the bike rides until you
set Effort Assist on a level.

1. **Show it.** System → Effort Sensing → Range Field Shows: **Your Power**. Set Bike + Rider
   Mass to your real total. Write the config.
2. **Let it learn the sensor.** The Range field counts 0 → 100. It only counts while you ride
   above 20 km/h in a tall gear at 60 rpm or more, so a few minutes of steady fast riding on the
   flat or a gentle descent. Easy pedalling is best. It's saved the next time you stop, and kept
   from then on, so this happens once.
3. **The test.** Below 25 km/h, in a low or middle gear, the Range field now shows watts. Ride
   steadily, then push harder without changing gear or cadence much, then ease off. **Does the
   number follow you?** That's the whole question. The miles/km setting doesn't matter: the
   firmware compensates.
   - It follows, roughly right → continue to 4.
   - It follows but reads high or low across the board → adjust Pedal Stroke (%): reads high,
     raise it; reads low, lower it.
   - It jumps around without following you → the effort can't be seen on this bike (the
     sensor's edges aren't steady enough, or the motor smooths the wobble). Set the Range field
     back and tell me what you saw; there are bench measurements to try next.
4. **Effort assist on one level.** Assist Levels → level 3 → Effort Assist **1.5** (motor power =
   1.5× yours), Max Current **40 %** as the cap. The level falls back to its normal behaviour
   until there's an estimate (the first one or two crank turns, above Effort Max Speed, or
   before learning has finished). Try it on a hill: push harder, get more help, regardless of cadence.

Things to know:

- It reads low by a fixed amount at higher speeds (about 20-30 W at 15-20 km/h) if you pushed
  hard while it learned. Learning while pedalling lightly avoids that.
- Gear changes and hard launches hold the last reading for about half a revolution.
- Above Effort Max Speed (default 25 km/h) there's no reading: the wobble is too small to
  measure there.
- **Throttle (and cruise):** while the throttle is open, the motor can spin the chainring faster
  than your feet, so the cranks tell nothing about effort. The reading shows 0 and learning
  pauses until you let go and pedal on for a turn or so. Effort assist keeps your last estimate
  for up to 5 s, so a throttle blip doesn't drop it back to the level's fixed current; the
  throttle still wins whenever it asks for more. Don't use the throttle while the pattern is
  learning (step 2): it just won't count.

## Undoing a change

Set the setting back to its "off" value (0, or unticked) and write the config again. Every
setting in the table is independent.
