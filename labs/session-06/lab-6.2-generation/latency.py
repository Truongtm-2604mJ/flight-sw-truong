#!/usr/bin/env python3
"""MM-150 system measurement: ground mode command latency.

Reads a ground station log written by the Session 5 ground scripts (host
simulation or SIS) and, for every TC(8,1) mode command, measures in
onboard time from the command's completion report TM(1,7) to the first
following mode transition report caused by GROUND.

    python3 latency.py logs/host-sim-modes.log --label host-sim
    python3 latency.py logs/sis-modes.log --label sis

Writes logs/latency-<label>.md and appends one result line in the
requirement test format (TC id, requirement, PASS/FAIL) to
logs/latency-<label>.txt, which the traceability matrix reads.
"""
import argparse
import os
import re
import sys

LIMIT_S = 3.0      # MM-150
PERIOD_S = 2.0     # supervisor period, the design constraint behind MM-150

TC_RE = re.compile(r"TC -> ([0-9A-F]{8})\S*\s+TC\(8,1\) ground: (\w+)")
DONE_RE = re.compile(r"t=\s*([\d.]+)s TM\(1,7\)\s+completion OK for request ([0-9A-F]{8})")
MODE_RE = re.compile(r"t=\s*([\d.]+)s TM\(5,\d\)\s+MODE (\w+) -> (\w+), cause GROUND")
BOOT_RE = re.compile(r"TM\(5,1\)\s+BOOT #")


def measure(path):
    rows = []
    pending = {}          # request id -> target name, waiting for TM(1,7)
    waiting = []          # [target, t_done] waiting for the mode report
    for line in open(path, errors="replace"):
        if BOOT_RE.search(line):
            # A new run of the spacecraft: onboard time restarts.
            for target, t_done in waiting:
                rows.append((target, t_done, None))
            waiting = []
            pending = {}
            continue
        m = TC_RE.search(line)
        if m:
            pending[m.group(1)] = m.group(2)
            continue
        m = DONE_RE.search(line)
        if m and m.group(2) in pending:
            waiting.append([pending.pop(m.group(2)), float(m.group(1))])
            continue
        m = MODE_RE.search(line)
        if m and waiting:
            t_mode, to = float(m.group(1)), m.group(3)
            for i, (target, t_done) in enumerate(waiting):
                if target == to:
                    rows.append((target, t_done, t_mode - t_done))
                    del waiting[i]
                    break
    for target, t_done in waiting:
        rows.append((target, t_done, None))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--label", required=True)
    a = ap.parse_args()
    rows = measure(a.log)
    out = os.path.join(os.path.dirname(os.path.abspath(a.log)), f"latency-{a.label}")
    lat = [r[2] for r in rows if r[2] is not None]
    missing = [r for r in rows if r[2] is None]
    ok = bool(lat) and max(lat) <= LIMIT_S
    with open(out + ".md", "w") as f:
        f.write(f"# MM-150: ground mode command latency ({a.label})\n\n")
        f.write(f"Source log: `{os.path.basename(a.log)}`. Measured in onboard time, from the "
                "TM(1,7) completion report of each TC(8,1) to the mode transition report "
                "with cause GROUND.\n\n")
        f.write("| # | Commanded mode | TM(1,7) at (s) | Latency (s) |\n|---|---|---:|---:|\n")
        for i, (target, t_done, d) in enumerate(rows, 1):
            f.write(f"| {i} | {target} | {t_done:.3f} | "
                    f"{'%.3f' % d if d is not None else 'no report in log'} |\n")
        if lat:
            f.write(f"\nCommands measured: {len(lat)}. Maximum {max(lat):.3f} s, "
                    f"minimum {min(lat):.3f} s. Requirement: at most {LIMIT_S:.1f} s "
                    f"(supervisor period {PERIOD_S:.1f} s plus margin).\n")
        if missing:
            f.write(f"\n{len(missing)} command(s) without a mode report before the log ended "
                    "(run stopped first); not counted.\n")
        f.write(f"\n**Result: {'PASS' if ok else 'FAIL'}**\n")
    with open(out + ".txt", "w") as f:
        f.write(f"{'TC-LAT-SYS-' + a.label:<18} {'MM-150':<15} {'PASS' if ok else 'FAIL'}\n")
    print(open(out + ".md").read())
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
