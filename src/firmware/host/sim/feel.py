#!/usr/bin/env python3
"""Torque-feel benchmark: rides the firmware through launches, effort changes
and steady climbs in the ride simulator and compares what the motor does with
an ideal torque-sensor bike (motor power = ASSIST x rider power, at once).

    python3 sim/feel.py [--json out.json]     (from src/firmware/host, after `make ride_sim`)

Every ride starts by learning the pedal sensor's pattern while cruising, then
stops and runs its test. The level under test is the recommended effort
setup: effort assist 1.5x with the watt-based level as its fallback.

The numbers are simulator numbers: the bike model is simple (see BUILDING.md)
and the real motor controller isn't modelled. They rank firmware versions
against each other; they don't promise how the bike rides.
"""

import argparse
import csv
import json
import os
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.join(HERE, "..", "ride_sim")
OUT = os.path.join(HERE, "..", "out", "feel")

ASSIST = 1.5
T0 = 200000  # ms: the test starts here, after learning and a stop

PRELUDE = """
bike pedal_stroke 0.8
bike pas_edge_error_deg 0.3
config pas_keep_current_percent 100
config level 3 flags 129
config level 3 target_current_percent 60
config level 3 effort_x10 15
config launch_sensing 1
config launch_ramp_amps_s 30
{extra}
at 0 level 0
at 0 speed 32
at 0 gear 3.0
at 0 grade -2
at 0 rider_w 150
at 190000 rider_w 0
at 190000 brake 1
at 199000 brake 0
"""


def run(name, body, end_ms, extra=""):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".sim")
    out = os.path.join(OUT, name + ".csv")
    with open(path, "w") as f:
        f.write("title feel: %s\n" % name + PRELUDE.format(extra=extra) + body + "\nend %d\n" % end_ms)
    subprocess.run([SIM, path, out], check=True, stdout=subprocess.DEVNULL)
    with open(out) as f:
        lines = f.readlines()[1:]
    rows = [{k: float(v) for k, v in r.items()} for r in csv.DictReader(lines)]
    for r in rows:
        # what the firmware controls, and what "motor W per rider W" is set in
        r["battery_w"] = r["current_a"] * r["voltage_v"]
    return rows


def window(rows, a, b):
    return [r for r in rows if a <= r["t_ms"] < b]


def mean(rows, key):
    return statistics.fmean(r[key] for r in rows) if rows else 0.0


def launch(force_w, extra=""):
    rows = run("launch_%d" % force_w, """
at %d grade 0
at %d gear 1.2
at %d level 3
at %d rider_w %d
""" % (T0, T0, T0, T0, force_w), T0 + 4000, extra)
    start = next(r for r in rows if r["t_ms"] >= T0)
    onset = next((r for r in rows if r["t_ms"] >= T0 and r["current_pct"] > 0), None)
    res = {"onset_ms": None, "onset_deg": None}
    if onset:
        res["onset_ms"] = onset["t_ms"] - T0
        res["onset_deg"] = onset["crank_deg"] - start["crank_deg"]
    # Assist over the first 1.5 s of pushing, as motor current: from a
    # standstill the motor's push is its torque, which goes with current, not
    # with the battery power (that is next to nothing until the bike moves).
    res["assist_a"] = mean(window(rows, T0, T0 + 1500), "current_a")
    res["peak_a"] = max(r["current_a"] for r in window(rows, T0, T0 + 1500))
    # what an ideal torque-sensor bike would add: ASSIST x the rider's crank
    # torque (the simulator's push of force_w newtons at the wheel in gear
    # 1.2), at the motor's 5.3 Nm per amp
    res["ideal_a"] = ASSIST * force_w * (28 * 0.0254 / 2) * 1.2 / 5.3
    return res


def steps_once(shift, extra=""):
    seg = 25000 + shift
    rows = run("steps_%d" % shift, """
at %d grade 7
at %d gear 1.6
at %d level 3
at %d rider_w 150
at %d rider_w 280
at %d rider_w 100
at %d rider_w 0
""" % (T0, T0, T0, T0, T0 + seg, T0 + 2 * seg, T0 + 3 * seg), T0 + 3 * seg + 3000, extra)

    # time until the motor has made 90% of the change an ideal torque-sensor
    # bike would make at once (ASSIST x the rider's old and new power)
    def settle(t_step, t_end, rider_from, rider_to):
        before = ASSIST * rider_from
        after = ASSIST * rider_to
        target = before + 0.9 * (after - before)
        for r in window(rows, t_step, t_end):
            if (after > before and r["battery_w"] >= target) or (after < before and r["battery_w"] <= target):
                return r["t_ms"] - t_step, before, after
        return None, before, after

    up_ms, up_from, up_to = settle(T0 + seg, T0 + 2 * seg, 150, 280)
    down_ms, down_from, down_to = settle(T0 + 2 * seg, T0 + 3 * seg, 280, 100)
    stop = next((r["t_ms"] - (T0 + 3 * seg) for r in rows if r["t_ms"] >= T0 + 3 * seg and r["current_pct"] == 0), None)
    return {"up_ms": up_ms, "down_ms": down_ms, "stop_ms": stop}


# Where in the pedal stroke a change lands matters, so each timing test is
# run at several moments and averaged (a response that never comes counts as
# 3 s).
SHIFTS = [0, 170, 340, 510, 680, 850]


def steps(extra=""):
    runs = [steps_once(shift, extra) for shift in SHIFTS]
    return {k: statistics.fmean(3000 if r[k] is None else r[k] for r in runs) for k in runs[0]}


# (grade %, gear, rider W): steady states an effort bike should follow
# (the first one has to be a hill the rider can start on from a standstill)
LOW = [(7, 1.6, 150), (10, 1.0, 100), (12, 1.2, 250), (8, 1.9, 250), (4, 2.2, 120), (5, 2.5, 200)]
HIGH = [(1.5, 3.0, 150), (2, 3.2, 220), (1, 3.4, 180)]


def steady(name, states, extra=""):
    seg = 30000
    body = "at %d level 3\n" % T0
    for i, (grade, gear, rider) in enumerate(states):
        t = T0 + i * seg
        body += "at %d grade %g\nat %d gear %g\nat %d rider_w %d\n" % (t, grade, t, gear, t, rider)
    rows = run(name, body, T0 + len(states) * seg, extra)
    res = []
    for i, (grade, gear, rider) in enumerate(states):
        w = window(rows, T0 + i * seg + 18000, T0 + (i + 1) * seg)
        motor = mean(w, "battery_w")
        amps = [r["current_a"] for r in w]
        res.append({
            "grade": grade, "gear": gear, "rider_w": rider,
            "speed_kph": mean(w, "speed_kph"), "cadence_rpm": mean(w, "cadence_rpm"),
            "battery_w": motor, "ideal_w": ASSIST * rider,
            "error_pct": 100 * abs(motor - ASSIST * rider) / (ASSIST * rider),
            "ripple_pct": 100 * statistics.pstdev(amps) / statistics.fmean(amps) if statistics.fmean(amps) > 0 else 0,
        })
    return res


def gear_change(extra=""):
    runs = [gear_change_once(shift, extra) for shift in SHIFTS]
    return {"spike": statistics.fmean(r for r in runs), "worst": max(runs)}


def gear_change_once(shift, extra=""):
    rows = run("gear_change_%d" % shift, """
at %d grade 3
at %d gear 1.8
at %d level 3
at %d rider_w 160
at %d gear 2.2
""" % (T0, T0, T0, T0, T0 + 25000 + shift), T0 + 40000, extra)
    t = T0 + 25000 + shift
    before = mean(window(rows, t - 8000, t), "battery_w")
    peak = max(r["battery_w"] for r in window(rows, t, t + 4000))
    low = min(r["battery_w"] for r in window(rows, t, t + 4000))
    # how far the assist jumps or sags around the shift, as a factor
    return max(peak / before, before / max(low, 1)) if before > 0 else 9


def ghost(extra=""):
    # spinning the pedals without pushing, the motor (throttle-free) should
    # give next to nothing; needs the simulator's `spin` input
    rows = run("ghost", """
at %d grade 4
at %d gear 1.8
at %d level 3
at %d rider_w 160
at %d rider_w 0
at %d spin 55
""" % (T0, T0, T0, T0, T0 + 20000, T0 + 20000), T0 + 40000, extra)
    driving = mean(window(rows, T0 + 12000, T0 + 20000), "battery_w")
    ghosting = mean(window(rows, T0 + 28000, T0 + 40000), "battery_w")
    return {"driving_w": driving, "ghost_w": ghosting}


def lin(value, best, worst, points):
    """points at best or better, 0 at worst or worse, linear between"""
    if value is None:
        return 0.0
    frac = (worst - value) / (worst - best)
    return points * max(0.0, min(1.0, frac))


def measure(extra="", with_ghost=True):
    m = {
        "launch_gentle": launch(120, extra),
        "launch_hard": launch(400, extra),
        "steps": steps(extra),
        "low": steady("steady_low", LOW, extra),
        "high": steady("steady_high", HIGH, extra),
        "gear_change": gear_change(extra),
    }
    if with_ghost:
        try:
            m["ghost"] = ghost(extra)
        except subprocess.CalledProcessError:
            m["ghost"] = None  # this simulator build has no `spin` input
    return m


def score(m):
    s = {}
    lg, lh = m["launch_gentle"], m["launch_hard"]

    # A. launch (15): how soon assist comes, and whether a stomp gets more than a gentle push
    onset = max(x for x in (lg["onset_deg"], lh["onset_deg"]) if x is not None) if lg["onset_deg"] is not None and lh["onset_deg"] is not None else None
    s["A1 launch onset (crank deg)"] = (lin(onset, 20, 90, 7), 7, onset)
    ratio = lh["assist_a"] / lg["assist_a"] if lg["assist_a"] > 0 else 0
    s["A2 stomp gets more (hard/gentle assist)"] = (lin(ratio, 2.5, 1.0, 5), 5, ratio)
    over = lg["peak_a"] / lg["ideal_a"]
    s["A3 gentle launch isn't a surge (peak / ideal)"] = (lin(over, 2.0, 5.0, 3), 3, over)

    # B. effort tracking (20)
    low_err = statistics.fmean(x["error_pct"] for x in m["low"])
    high_err = statistics.fmean(x["error_pct"] for x in m["high"])
    s["B1 follows effort below 25 km/h (mean error %)"] = (lin(low_err, 10, 60, 13), 13, low_err)
    s["B2 follows effort at 27-35 km/h (mean error %)"] = (lin(high_err, 15, 80, 7), 7, high_err)

    # C. response (15)
    st = m["steps"]
    s["C1 push harder: time to 90% (ms)"] = (lin(st["up_ms"], 400, 2000, 7), 7, st["up_ms"])
    s["C2 ease off: time to 90% (ms)"] = (lin(st["down_ms"], 400, 2000, 5), 5, st["down_ms"])
    s["C3 stop pedalling: motor off (ms)"] = (lin(st["stop_ms"], 100, 300, 3), 3, st["stop_ms"])

    # D. smoothness (10)
    ripple = statistics.fmean(x["ripple_pct"] for x in m["low"])
    s["D1 steady assist ripple (%)"] = (lin(ripple, 5, 25, 6), 6, ripple)
    spike = m["gear_change"]["spike"]
    s["D2 gear change spike or dip (x)"] = (lin(spike, 1.3, 2.5, 4), 4, spike)

    # E. ghost pedalling (5)
    g = m.get("ghost")
    if g:
        frac = g["ghost_w"] / g["driving_w"] if g["driving_w"] > 0 else 1
        s["E1 ghost pedalling assist (fraction of driving)"] = (lin(frac, 0.25, 1.0, 5), 5, frac)
    else:
        s["E1 ghost pedalling assist (not measurable yet)"] = (0.0, 5, None)
    return s


def report(s):
    total = sum(v[0] for v in s.values())
    out_of = sum(v[1] for v in s.values())
    for name, (got, of, value) in s.items():
        shown = "-" if value is None else ("%.0f" % value if abs(value) >= 10 else "%.2f" % value)
        print("  %-52s %8s   %4.1f / %d" % (name, shown, got, of))
    print("  %-52s %8s   %4.1f / %d" % ("measured total", "", total, out_of))
    return total, out_of


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json")
    ap.add_argument("--floor", type=float, help="fail (exit 1) if the measured total is under this")
    args = ap.parse_args()

    m = measure()
    s = score(m)
    total, _ = report(s)
    print()
    for x in m["low"] + m["high"]:
        print("  %4.1f%% grade, gear %.1f, %3.0f W rider: %4.1f km/h %3.0f rpm -> motor %3.0f W (ideal %3.0f, %2.0f%% off, ripple %2.0f%%)" % (
            x["grade"], x["gear"], x["rider_w"], x["speed_kph"], x["cadence_rpm"], x["battery_w"], x["ideal_w"], x["error_pct"], x["ripple_pct"]))
    if args.json:
        with open(args.json, "w") as f:
            json.dump({"metrics": m, "score": {k: v[:2] for k, v in s.items()}}, f, indent=1)
    if args.floor is not None and total < args.floor:
        print("\nmeasured total %.1f is under the floor of %.1f" % (total, args.floor))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
