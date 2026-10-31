# Tuning bbs-fw-ai 2.2

Everything new since upstream has its own setting, so it can be switched on and judged one change at a
time. If several change at once and the bike feels wrong, there's no telling which one did it.

## Flashing: the Update Firmware tab

From the latest green CI run on `master`, download **BBSFWTool** and unzip it. It needs the .NET 6
Desktop Runtime, the same as the upstream tool. It contains the matching firmware and a flasher
(stcgal), so nothing else is needed: no STC-ISP, no separate firmware download.

1. Plug the programming cable into the controller and the laptop. Battery on, not charging.
2. Open **Update Firmware** (the first tab), pick the cable's port and click **Start**.
3. Do what the page says. It asks you to switch the controller **off** and **on** three times:
   - the controller's chip only accepts new firmware in the moment after it's switched on;
   - use the battery's switch, or unplug and replug the cable if yours powers the controller.
   The highlighted box tells you when.

What it does, in order:

- **Backs up your settings** to a file (shown at the end) before anything else.
- **Checks the chip.** It continues only for the BBSHD chips bbs-fw supports (STC15W4K56S4,
  IAP15W4K61S4), and stops for anything else.
- **Flashes** with the same settings as the upstream wiki (STC15, 20 MHz). The log shows the exact
  stcgal command.
- **Checks** the new version answers and your settings came through. If the flash erased them,
  it offers to restore the backup.

A failed or interrupted flash is safe to retry: the chip's loader can't be overwritten. If
Windows flags `stcgal.exe` (an unsigned, packaged Python program), allow it, or flash manually.

**Manual flashing**, if you prefer: the hex is `flash/bbs-fw-BBSHD.hex` in the download, flashed
as in the [upstream wiki](https://github.com/danielnilsson9/bbs-fw/wiki/Flash-Firmware-(BBS02-&-BBSHD))
with STC-ISP and 20 MHz.

Older config tools (upstream 1.5, or 2.0/2.1) can't read or write 2.2 settings: use this one.
Going back to upstream firmware later resets the settings to defaults, because upstream doesn't
understand this fork's config. Keep the backup the wizard makes.

## Bench testing: the Bench tab

With the cable in place of the display, the tool acts as the display. Use it with the bike on a
stand and the **rear wheel off the ground**: the motor can spin the wheel at full speed.

1. **Connection** tab: connect. Then **Bench** tab: **Start**.
2. **Display:** assist level buttons, lights, and what the SW102T would show (speed, battery,
   current, the Range field). **Live data:** what it can't show (cadence, motor current asked
   for, motor speed cap, brake, throttle, temperature, learning progress).
3. **Record to CSV** while you try things. A recording is what to send back if something looks
   wrong.

A short checklist after flashing:

| Check | How | Expect |
|---|---|---|
| Settings came through | Bench: speed sensor, battery % | Speed while the wheel turns, sensible battery % |
| Assist starts and stops | Level 1, turn the cranks by hand, stop | Motor current within half a turn; back to zero as soon as you stop |
| Brake cut-off | Pedal at level 1, pull a brake lever | Current to zero at once, Brake = True |
| Throttle | Level 1, twist gently | Throttle % follows; motor responds |
| Levels | Step 0 → 9 | Motor current asked for rises with the level |

**Pedal sensor check** (for effort sensing): **Check pedal sensor**, then turn the cranks forwards
steadily by hand for about 10 seconds (assist goes to 0). It records 6 turns and reports:

- whether the two sensor channels give their edges in the right order;
- how much each edge wanders from turn to turn, compared with what the controller's timing alone
  causes. "Clean" means effort sensing is worth the ride test below; "Noisy" means it probably won't
  work on this sensor.

**Save capture...** keeps the raw edges, for me to look at.

While the Bench tab runs, effort sensing doesn't learn, and it stays that way until the controller
is switched off. On a stand, the cranks wobble far more than on the road, and learning there would
spoil the pattern. If learning ran on the stand anyway (Range counted up), **Reset learned pattern**
starts it again.

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

0. **Bench first** (optional, recommended): the pedal sensor check on the Bench tab says whether
   the sensor is steady enough before you ride.
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
