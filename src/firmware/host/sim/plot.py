#!/usr/bin/env python3
"""Plot ride simulator output as an SVG. Standard library only.

    python3 plot.py out.svg run.csv [other.csv ...] [--from MS] [--to MS]

Three stacked panels share the time axis: rider cadence (solid) against what
the firmware measured (dashed), wheel speed, and the motor current the
firmware asked for. With several CSVs, each run gets its own colour, so a
change can be compared against a baseline.
"""

import argparse
import csv
import html
import math
import sys

COLOURS = ["#1f6feb", "#d1242f", "#1a7f37", "#8250df", "#bf8700"]

WIDTH = 960
PANEL_H = 170
GAP = 40
LEFT = 64
RIGHT = 16
TOP = 56
BOTTOM = 40


def load(path):
    title = path
    rows = []
    with open(path, newline="") as f:
        lines = f.readlines()
    if lines and lines[0].startswith("#"):
        title = lines[0][1:].strip()
        lines = lines[1:]
    for row in csv.DictReader(lines):
        rows.append({k: float(v) for k, v in row.items()})
    return title, rows


def nice_max(v):
    """Round an axis maximum up to 1, 2 or 5 x 10^n."""
    if v <= 0:
        return 1.0
    base = 1.0
    while base * 10 <= v:
        base *= 10
    while base > v:
        base /= 10
    for m in (1, 2, 5, 10):
        if base * m >= v:
            return base * m
    return base * 10


def axis(vmax, n):
    """A round tick step for about n intervals, and the axis maximum it gives."""
    step = nice_max(vmax / n)
    top = step
    while top < vmax - 1e-9:
        top += step
    return step, top


def ticks(step, top):
    return [step * i for i in range(int(round(top / step)) + 1)]


def fmt(v):
    return f"{v:g}" if abs(v) < 1000 else f"{v:.0f}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("svg")
    ap.add_argument("csv", nargs="+")
    ap.add_argument("--from", dest="t_from", type=float, default=None, help="start time, ms")
    ap.add_argument("--to", dest="t_to", type=float, default=None, help="end time, ms")
    args = ap.parse_args()

    runs = []
    for path in args.csv:
        title, rows = load(path)
        rows = [r for r in rows
                if (args.t_from is None or r["t_ms"] >= args.t_from)
                and (args.t_to is None or r["t_ms"] <= args.t_to)]
        if not rows:
            sys.exit(f"{path}: no samples in the selected time range")
        runs.append((path, title, rows))

    t0 = min(r[2][0]["t_ms"] for r in runs)
    t1 = max(r[2][-1]["t_ms"] for r in runs)
    if t1 <= t0:
        t1 = t0 + 1

    # (label, [(column, dash)], axis starts at zero)
    panels = [
        ("Cadence (rpm): rider solid, firmware dashed",
         [("cadence_rpm", ""), ("fw_cadence_rpm", "5 4")], True),
        ("Wheel speed (km/h): actual solid, firmware dashed",
         [("speed_kph", ""), ("fw_speed_kph", "5 4")], True),
        ("Motor current requested (A)",
         [("current_a", "")], True),
        ("Power (W): motor solid, rider dashed",
         [("motor_w", ""), ("rider_w", "5 4")], True),
        ("Battery voltage (V)",
         [("voltage_v", "")], False),
    ]

    # skip panels whose columns an older CSV doesn't have
    panels = [pn for pn in panels if all(key in rows[0] for _, _, rows in runs for key, _ in pn[1])]

    height = TOP + len(panels) * PANEL_H + (len(panels) - 1) * GAP + BOTTOM
    plot_w = WIDTH - LEFT - RIGHT

    def x_of(t):
        return LEFT + (t - t0) / (t1 - t0) * plot_w

    out = []
    out.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{height}" '
               f'viewBox="0 0 {WIDTH} {height}" font-family="system-ui, sans-serif" font-size="12">')
    out.append(f'<rect width="{WIDTH}" height="{height}" fill="#ffffff"/>')

    # legend: one entry per run
    lx = LEFT
    for i, (path, title, _) in enumerate(runs):
        colour = COLOURS[i % len(COLOURS)]
        out.append(f'<rect x="{lx}" y="14" width="14" height="4" fill="{colour}"/>')
        out.append(f'<text x="{lx + 20}" y="20" fill="#1f2328">{html.escape(title)}</text>')
        lx += 28 + 7 * len(title)
        if lx > WIDTH - 200:
            lx = LEFT

    for p, (label, series, from_zero) in enumerate(panels):
        top = TOP + p * (PANEL_H + GAP)
        bottom = top + PANEL_H

        values = [r[key] for _, _, rows in runs for r in rows for key, _ in series]
        if from_zero:
            vmin = 0.0
            step, vmax = axis(max(values), 4)
        else:
            # zoom to the data (at least 2 units tall), on round tick values
            lo, hi = min(values), max(values)
            if hi - lo < 2:
                mid = (lo + hi) / 2
                lo, hi = mid - 1, mid + 1
            step, _ = axis(hi - lo, 4)
            vmin = step * math.floor(lo / step)
            vmax = vmin + step
            while vmax < hi - 1e-9:
                vmax += step

        def y_of(v, top=top, vmin=vmin, vmax=vmax):
            return top + PANEL_H - ((v - vmin) / (vmax - vmin)) * PANEL_H

        out.append(f'<text x="{LEFT}" y="{top - 8}" fill="#1f2328" font-weight="600">{html.escape(label)}</text>')

        for v in ticks(step, vmax - vmin):
            v += vmin
            y = y_of(v)
            out.append(f'<line x1="{LEFT}" x2="{WIDTH - RIGHT}" y1="{y:.1f}" y2="{y:.1f}" stroke="#d0d7de" stroke-width="1"/>')
            out.append(f'<text x="{LEFT - 6}" y="{y + 4:.1f}" fill="#59636e" text-anchor="end">{fmt(v)}</text>')

        for i, (_, _, rows) in enumerate(runs):
            colour = COLOURS[i % len(COLOURS)]
            for key, dash in series:
                pts = " ".join(f"{x_of(r['t_ms']):.1f},{y_of(r[key]):.1f}" for r in rows)
                dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
                out.append(f'<polyline points="{pts}" fill="none" stroke="{colour}" '
                           f'stroke-width="1.6"{dash_attr} stroke-linejoin="round"/>')

            # brake intervals as a band along the bottom of the current panel
            if series[0][0] == "current_a":
                start = None
                for r in rows + [{"t_ms": rows[-1]["t_ms"], "brake": 0}]:
                    if r["brake"] and start is None:
                        start = r["t_ms"]
                    elif not r["brake"] and start is not None:
                        out.append(f'<rect x="{x_of(start):.1f}" y="{bottom - 6 - 6 * i}" '
                                   f'width="{max(1.0, x_of(r["t_ms"]) - x_of(start)):.1f}" height="4" '
                                   f'fill="{colour}" opacity="0.5"><title>brake</title></rect>')
                        start = None

        out.append(f'<line x1="{LEFT}" x2="{WIDTH - RIGHT}" y1="{bottom}" y2="{bottom}" stroke="#59636e"/>')

    # time axis under the last panel
    axis_y = TOP + len(panels) * PANEL_H + (len(panels) - 1) * GAP
    t_step, t_top = axis(t1 - t0, 8)
    for t in ticks(t_step, t_top):
        if t0 + t > t1:
            break
        x = x_of(t0 + t)
        out.append(f'<line x1="{x:.1f}" x2="{x:.1f}" y1="{axis_y}" y2="{axis_y + 4}" stroke="#59636e"/>')
        out.append(f'<text x="{x:.1f}" y="{axis_y + 18}" fill="#59636e" text-anchor="middle">{fmt(t0 + t)}</text>')
    out.append(f'<text x="{WIDTH - RIGHT}" y="{axis_y + 34}" fill="#59636e" text-anchor="end">time (ms)</text>')

    out.append("</svg>")

    with open(args.svg, "w") as f:
        f.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
