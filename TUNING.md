# Tuning bbs-fw-ai 2.0

Everything new in 2.0 has its own setting, so it can be switched on and judged one change at a
time. If several change at once and the bike feels wrong, there's no telling which one did it.

## Before you flash

1. From the latest green CI run on `master`, download **bbs-fw-BBSHD** (the firmware `.hex`) and
   **BBSFWTool** (the config tool, which needs the .NET 6 Desktop Runtime, the same as the upstream tool).
2. Flash the hex the usual way. Your existing settings carry over: the firmware converts the old
   config on first boot.
3. Use the 2.0 config tool from now on. The 1.5 tool rejects the new config version, so it can't
   read or write 2.0 settings.

Going back to upstream firmware later resets the settings to defaults, because upstream doesn't
understand the 2.0 config. Write your settings down first.

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

## Undoing a change

Set the setting back to its "off" value (0, or unticked) and write the config again. Every
setting in the table is independent.
