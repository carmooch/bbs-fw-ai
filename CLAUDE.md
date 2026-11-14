# Notes for Claude sessions

bbs-fw-ai is a personal fork of bbs-fw (firmware for Bafang BBSHD motor controllers) for one bike:
BBSHD, SW102T display, 52 V battery, 30 A. The aim is a torque-sensor-like ride from the cadence
sensor. [ROADMAP.md](ROADMAP.md) has the state of things, [BUILDING.md](BUILDING.md) the builds and
checks, [TUNING.md](TUNING.md) and [LIVE.md](LIVE.md) the owner's side.

## Rules from the owner (always)

- **Privacy.** The repository is public. Never put session links, names, email addresses, account
  details, local paths or ride logs in commits, files, pull requests or anything published. The
  commit trailer is only `Co-Authored-By: Claude <noreply@anthropic.com>` (with the model's name),
  never a session link. Before every push, read the whole diff and the commit messages for anything
  personal.
- **master is what gets flashed.** Work on a branch; merge only when CI is green and the owner says
  "merge".
- **Line endings:** files mix CRLF and LF. Keep each line's ending; `scripts/check-eol.sh` checks.
- Say plainly what is simulator-only and what has been tested on the bike.

## Live sessions with the bike (bbs-link)

When the laptop is plugged into the bike, `bbs-link` talks to the controller. On the laptop it's
`.\bbs-link.exe` beside this file (from the latest release, or the `link` folder of
BBSFWTool.zip); in the repository,
`dotnet run --project src/link -c Release -- <command>`. `bbs-link help` lists the commands.

**On a fresh laptop** (asked to connect to the bike, no bbs-link here yet): download
`bbs-link.exe`, `CLAUDE.md` and `SHA256SUMS` from this repository's latest release
(`https://github.com/<owner>/<repo>/releases/latest/download/<file>`, the owner and repository
from the address you were given) into the working folder, check them with
`sha256sum -c --ignore-missing SHA256SUMS`, read the downloaded `CLAUDE.md` (it matches the
`bbs-link.exe` beside it), then start as below. Never run a bbs-link whose checksum doesn't match.

**Starting:** `bbs-link ports`, then run `bbs-link serve --port COMn` as a background process (it
holds the port until `bbs-link stop`). `bbs-link status` shows when it's connected. It sets level 0
on connecting. If it waits for the controller, ask the owner to switch the battery on, or to unplug
and replug the cable.

**Working:** `status` for a glance, `watch N` for N seconds of data (rows are averages; every
sample is in the CSV log), `mark "..."` to note in the log what the owner is about to do,
`events` for the controller's event log.

**Rules for the session** (bbs-link enforces the ones marked *, the rest are yours):

- Before any level above 0, ask whether the bike is on a stand with the rear wheel off the
  ground, or whether the owner is riding, and keep to what they say.
- One change at a time: say what you're changing and why, `--dry-run` first, then set it, then
  test it with the owner and compare against the log. Undo (`config undo`) anything that doesn't
  help.
- Settings change only with the bike still*; every change is backed up and read back*; Max Current
  stays at or under 30 A*; battery voltages only change with `--battery`*, and only when the owner
  asks. Never raise current, speed limits or launch settings while the owner is riding; stop first.
- Ask before `reset-effort` (it throws away the learned pattern, which needs a fast ride to
  relearn) and before `calibrate-voltage` (use a voltage the owner has just measured).
- Effort sensing doesn't learn while bbs-link reads live data (until the controller is switched
  off). Don't expect the Range field / rider watts to learn during a live session.
- Finish with `bbs-link stop` (level 0, port closed), and tell the owner the level is 0.
- The `live\` folder (logs, backups) is the owner's ride data: read it, summarise it, never commit
  or upload it.

**What the numbers mean:** `motor %` is the current the firmware asks for (percent of Max Current);
`A` and `W` are battery current and power; `rider W` is the effort estimate (0 until valid);
`learned %` is the pedal sensor pattern's learning progress. Status codes other than normal are
errors (low voltage, hall sensor, overheating...) and worth reporting straight away.
