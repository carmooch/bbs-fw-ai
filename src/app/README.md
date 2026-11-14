# BBS-FW-AI

A tuning workspace for bbs-fw, built beside the config tool (`src/tool`, BBSFWTool), which stays
as it is. It reuses the config tool's protocol and settings code, as bbs-link does.

## What's in this first build

- **Connect** to the controller (USB end of the cable first, then the motor end), or **open a tune**
  or backup (`.xml`) to explore it without the bike. `BBS-FW-AI.exe some-tune.xml` opens one
  directly.
- **Standard and Sport assist maps**: every row of the map in one table, with a chart above that
  shares the table's nine level columns. Pick a row to plot it; drag a point or type in a cell;
  Shift+Up/Down nudges a cell. The current row plots in amps, with your Max Current and the BBSHD's
  33 A limit drawn. Cells that differ from the bike are orange with a dot; the dashed line is what's
  on the bike.
- **Changes** panel listing every difference from the bike.
- **Review & write**: the changes, checks (bike still on fresh live data, Max Current within the
  limit, battery voltages unchanged unless confirmed), a backup saved first, then write and read
  back. Optionally saves the tune with notes.
- **Live strip**: level, cadence, speed, battery current and voltage, temperature, learning
  progress. While connected the app holds the assist level at 0, as the display would.

Backups and saved tunes go to `%LOCALAPPDATA%\BBS-FW-AI\Backups` and `\Tunes`.

## Not yet

Motor & battery, throttle, pedal timing, effort sensing, sensors, display, datalogs and backups
pages; compare view; command palette; changing assist level or type. Use BBSFWTool for those.

## Building

Windows, .NET 8 SDK: `dotnet build src/app/bbs-fw-ai.csproj -c Release`. CI builds it on every
push; it isn't in the release yet.

Tested so far: built and opened with a real backup of the bike's settings, map editing and the
chart checked on screen. Not yet tried connected to the bike.
