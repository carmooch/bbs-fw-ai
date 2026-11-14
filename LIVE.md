# Live sessions: Claude on the laptop, plugged into the bike

Instead of clicking through the config tool, a Claude session on the laptop drives the controller
directly: it reads live data, sets the assist level, changes settings and runs checks, while you
pedal, brake and tell it what you feel. It does this with **bbs-link**, a small program that holds
the serial link and takes commands.

## What you need

- The laptop (Windows 10 or 11), with the **Claude desktop app** signed in. Nothing else to
  install: the session downloads bbs-link itself.
- The **programming cable** used for flashing, plugged in where the display normally goes. If
  flashing has worked with it, it works here.
- The bike running this firmware version (flash it first if not, see [TUNING.md](TUNING.md)):
  bbs-link refuses to change settings on a controller whose settings format doesn't match.

## Starting a session

1. Make an empty folder on the laptop, e.g. `C:\bbs`, and start a Claude Code session in it from
   the Claude desktop app.
2. Plug the cable into the laptop and the bike, battery on.
3. Tell it: *"Read CLAUDE.md in this repository and connect to my bike:"* followed by this
   repository's address (the GitHub page you're reading this on).

It downloads `bbs-link.exe` and its rules from the repository's **latest release** (built by CI
from `master` after every green push, no login needed), checks them against their checksums,
finds the cable's port and connects. If no port shows up, the cable's USB-serial driver is
missing (CH340, FTDI or PL2303; Windows Update usually has it), and it will say so.

The same release has **BBSFWTool.zip** (config tool, firmware and flasher) and the firmware hex,
so flashing doesn't need the Actions page either.

**Or from a session you already have on the web or your phone:** in the Claude desktop app on the
laptop, turn on Remote Control (Settings → Claude Code → Remote Control) and share the
`C:\bbs` folder. The session can then ask to use that folder, and you approve it on the
laptop. This isn't available to every account yet; the first way always works.

## What it can and can't do

- **It's the display.** While the cable is in, the display isn't, so assist levels come from the
  session (`bbs-link level N`). It starts at level 0 and goes back to 0 when it stops. Brake
  cut-off and the throttle work as usual: they're wired to the controller, not the display.
- **Settings only change with the bike still**: no pedalling, wheel stopped, throttle closed.
  Every change is backed up first and read back after; `config undo` puts the previous settings
  back. Max Current can't go over 30 A, and battery voltages only change on purpose.
- **You see it live** at `http://localhost:8710` in a browser on the laptop, while the link runs:
  big readings (level, cadence, speed, motor current, battery, your effort), rolling charts of the
  last 30 s to 3 min with brake pulls and level changes marked, and a feed of what the session is
  doing and why. It only shows things; it can't change anything, and only the laptop can open it.
- **Everything is logged** to `live\` (live data about 4 times a second, events, settings
  backups). It stays on the laptop; nothing from it goes into the repository.
- **Effort sensing doesn't learn while the link reads live data**, until the controller is switched
  off, as with the Bench tab. Learn the pedal sensor pattern on a ride without the laptop (TUNING.md,
  effort sensing step 2) before testing effort assist live.
- **On a stand, rear wheel off the ground**, for anything above level 0 that you aren't riding:
  the motor can spin the wheel at full speed.
- **Riding with the laptop** (in a backpack, short test loops) works, but the level only changes
  when the session changes it. Agree the level before you set off, and use the brake to cut the
  motor.
- **If the USB cable's adapter stalls** (seen after the motor had worked hard: live data stops
  while the link still looks connected), the link notices within about 10 s, says it lost the
  controller and asks you to **unplug and replug the USB end of the cable**. Until then the
  controller keeps the last level (the brakes still cut the motor). Once replugged, the link
  reconnects by itself at level 0.

## Commands (for reference)

`bbs-link help` lists them all. The ones a session uses most:

```
bbs-link serve --port COM5        hold the link (runs until `bbs-link stop`)
bbs-link status                   connected? the latest live data
bbs-link watch 10                 10 s of live data as a table, with a summary
bbs-link level 2                  assist level
bbs-link mark "push hard now"     a note in the live log
bbs-link config get LaunchSensing
bbs-link config set "StandardAssistLevels[3].MaxCurrentPercent=40" --dry-run
bbs-link config undo
bbs-link capture                  pedal sensor steadiness check (turn the cranks ~6 turns)
bbs-link stop                     level 0, port closed
```

On Linux or macOS, build it from the repository with
`dotnet run --project src/link -c Release -- <command>` (needs the .NET 8 SDK).
`src/link/check.sh` tests it against a virtual controller (see [BUILDING.md](BUILDING.md)).
