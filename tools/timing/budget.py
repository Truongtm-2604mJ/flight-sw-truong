"""LOTUS-1 timing budget of the integrated application (evidence/timing/).

Inputs, best available first:
  --sec-timing  output of build/rtems/sec-timing.exe on SIS (cost per operation)
  --rm          output of build/rtems/lotus1-timing.exe on SIS (rate monotonic
                stats of the integrated flight build, 60 s)
Without them it uses estimate.txt (instruction counts calibrated on the
Session 4 SIS run) and the Session 4 SIS task measurements.

Writes budget.md (or --out). With --report, also replaces the block between
the SIS-TIMING markers in that file with the measured results and their
comparison with the estimate.
"""
import argparse
import math
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))

# Session 4 SIS timing run (tmtc-design.md S5): C max (ms), T (ms), priority
S4 = {"ACS_CTRL": (18.957, 100, 10), "TM_HK": (47.764, 500, 20),
      "PL_LOG": (95.644, 1000, 30)}
# Every periodic task of the integrated application (src/app/mission.h):
# rate monotonic period owner name -> (task, T ms, RTEMS priority)
PERIODIC = {"SUP1": ("SUPERVISOR", 2000, 5), "ACS1": ("ACS_CTRL", 100, 10),
            "HK11": ("TM_HK", 500, 20), "LOG1": ("PL_LOG", 1000, 30),
            "SCHD": ("SCHED", 1000, 45)}
# Tasks with no Session 4 measurement: conservative design allowances (C ms,
# T ms, priority), replaced by the SIS measurement when --rm is given.
#   SUPERVISOR: liveness of 3 tasks, inhibit expiry, mode_sm step, thermal
#               step, one NV write (CRC over 64 octets), watchdog kick.
#   SCHED:      scan of 16 schedule slots, one release = one authenticated
#               path skipped (trusted), plus the command execution.
ALLOWANCE = {"SUPERVISOR": (5.0, 2000, 5), "SCHED": (4.0, 1000, 45)}
OPS = ("plain", "accept", "reject", "scrub", "load")
MEANINGFUL = 0.25        # a difference above 25 % is called out in the text

# Uplink assumptions for the TC_RX demand (aperiodic, priority 40).
LINK = {"lab console, 115200 baud, hex lines": 115200 / 10 / (3 + 2 * 120 + 1),
        "S-band TC uplink, 4 kbit/s, 120 octet TCs": 4000 / 8 / 128}


def parse_ops(path, est):
    ops = {}
    with open(path, errors="replace") as f:
        for line in f:
            if est:
                m = re.match(r"(\w+)\s+\d+ instr/op\s+~\s+([\d.]+) us", line)
                if m:
                    v = float(m.group(2)) / 1000
                    ops[m.group(1)] = (v, v)
            else:
                m = re.match(r"\s*(\w+)\s+mean\s+([\d.]+) us\s+max\s+([\d.]+) us", line)
                if m:
                    ops[m.group(1)] = (float(m.group(2)) / 1000, float(m.group(3)) / 1000)
    return ops


def seconds(tok):
    """'0.018957', '0:00.018957' or '00:00:00.018957' -> seconds."""
    parts = tok.split(":")
    v = 0.0
    for p in parts:
        v = v * 60 + float(p)
    return v


def parse_rm(path):
    """rtems_rate_monotonic_report_statistics() lines:
    ID OWNER COUNT MISSED cpu_min/max/avg wall_min/max/avg"""
    tasks = {}
    num = r"[\d:.]+"
    rx = re.compile(rf"0x[0-9a-fA-F]+\s+(\S+)\s+(\d+)\s+(\d+)\s+({num})/({num})/({num})"
                    rf"(?:\s+({num})/({num})/({num}))?")
    with open(path, errors="replace") as f:
        for line in f:
            m = rx.search(line)
            if m and m.group(1) in PERIODIC:
                name = PERIODIC[m.group(1)][0]
                tasks[name] = dict(count=int(m.group(2)), missed=int(m.group(3)),
                                   cpu_max=seconds(m.group(5)) * 1000,
                                   cpu_avg=seconds(m.group(6)) * 1000,
                                   wall_max=seconds(m.group(8)) * 1000 if m.group(8) else None)
    return tasks


def rta(tasks, blocking):
    """Response time analysis, fixed priority, with blocking B_i."""
    out = {}
    order = sorted(tasks, key=lambda n: tasks[n][2])
    for i, name in enumerate(order):
        c, t, _ = tasks[name]
        b = blocking.get(name, 0.0)
        r = c + b
        for _ in range(200):
            new = c + b + sum(math.ceil(r / tasks[h][1]) * tasks[h][0] for h in order[:i])
            if abs(new - r) < 1e-9:
                break
            r = new
        out[name] = r
    return out


def diff(measured, estimated):
    if estimated <= 0:
        return "n/a"
    d = (measured - estimated) / estimated
    return f"{d * 100:+.0f} %"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sec-timing")
    ap.add_argument("--rm")
    ap.add_argument("--out", default=os.path.join(HERE, "budget.md"))
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--report", help="file with <!-- SIS-TIMING:BEGIN/END --> markers")
    a = ap.parse_args()

    est = parse_ops(os.path.join(HERE, "estimate.txt"), True)
    sis = parse_ops(a.sec_timing, False) if a.sec_timing else {}
    if a.sec_timing and not all(k in sis for k in OPS):
        print(f"WARNING: {a.sec_timing} lacks some operations "
              f"({[k for k in OPS if k not in sis]}): using the estimate for them")
    ops = {k: sis.get(k, est[k]) for k in OPS}
    measured_ops = bool(sis)
    rm = parse_rm(a.rm) if a.rm else {}
    if a.rm and not rm:
        print(f"WARNING: no rate monotonic statistics found in {a.rm}: using Session 4 values")

    mac = ops["accept"][1] - ops["plain"][1]
    hk_extra = ops["scrub"][1] + 0.2           # scrub every 2nd cycle + SID 3

    # Task set: measured C when available, else Session 4 + the estimated additions.
    tasks, est_tasks = {}, {}
    for n, (c, t, p) in S4.items():
        est_tasks[n] = (c + (est["scrub"][1] + 0.2 if n == "TM_HK" else 0.0), t, p)
    for n, (c, t, p) in ALLOWANCE.items():
        est_tasks[n] = (c + (ops["plain"][1] if n == "SCHED" else 0.0), t, p)
    for name, t, p in PERIODIC.values():
        if name in rm:
            tasks[name] = (rm[name]["cpu_max"], t, p)
        elif name in est_tasks:
            tasks[name] = est_tasks[name]
    b = ops["accept"][1] + ops["load"][1]       # one TC(210,1) under the tc lock
    blocking = {n: b for n in tasks if tasks[n][2] >= 20}
    r = rta(tasks, blocking)
    u = sum(c / t for c, t, _ in tasks.values())
    u_est = sum(c / t for c, t, _ in est_tasks.values())
    n = len(tasks)
    bound = n * (2 ** (1 / n) - 1)
    missed = sum(v["missed"] for v in rm.values())
    closes = all(r[k] <= tasks[k][1] for k in tasks) and missed == 0

    src = ("SIS (`" + os.path.basename(a.sec_timing) + "`)") if measured_ops else \
        "ESTIMATE (instruction counts calibrated on Session 4 SIS)"
    L = [f"# LOTUS-1 timing budget (integrated application)\n",
         f"Operation costs: {src}. Task set: " +
         ("SIS rate monotonic statistics of the integrated flight build, 60 s (`" +
          os.path.basename(a.rm) + "`)." if rm else
          "Session 4 SIS C max plus the Session 8 additions to TM_HK (estimate); "
          "SUPERVISOR and SCHED: design allowances (" +
          ", ".join(f"{k} {v[0]:.1f} ms" for k, v in ALLOWANCE.items()) + ")."), ""]
    L.append("## Security operations\n")
    L.append("| Operation | estimate (ms) | " + ("SIS mean (ms) | SIS max (ms) | max vs estimate |"
             if measured_ops else "") + "\n|---|---:|" + ("---:|---:|---:|" if measured_ops else ""))
    for k in OPS:
        row = f"| {k} | {est[k][1]:.3f} |"
        if measured_ops and k in sis:
            row += f" {sis[k][0]:.3f} | {sis[k][1]:.3f} | {diff(sis[k][1], est[k][1])} |"
        L.append(row)
    L.append(f"\nMAC cost per command (accept minus plain): {mac:.3f} ms"
             + (f" (estimate {est['accept'][1] - est['plain'][1]:.3f} ms)." if measured_ops else "."))

    L.append("\n## Task set\n")
    L.append("| Task | P | T (ms) | C (ms) | U | B (ms) | R (ms) | R <= T |" +
             (" missed | estimate C (ms) | C vs estimate |" if rm else "") +
             "\n|---|---:|---:|---:|---:|---:|---:|---|" + ("---:|---:|---:|" if rm else ""))
    for k in sorted(tasks, key=lambda x: tasks[x][2]):
        c, t, p = tasks[k]
        row = (f"| {k} | {p} | {t} | {c:.3f} | {c / t:.4f} | {blocking.get(k, 0.0):.3f} | "
               f"{r[k]:.1f} | {'yes' if r[k] <= t else 'NO'} |")
        if rm:
            e = est_tasks.get(k)
            row += (f" {rm[k]['missed'] if k in rm else '-'} | "
                    f"{e[0]:.3f} | {diff(c, e[0])} |" if e else
                    f" {rm[k]['missed'] if k in rm else '-'} | - | - |")
        L.append(row)
    L.append(f"\nPeriodic utilisation U = {u:.4f} (Session 4, three tasks: 0.3807; "
             f"estimate for this task set: {u_est:.4f}). Liu and Layland bound for n = {n}: "
             f"{bound:.4f}. Missed periods: {missed if rm else 'not measured'}.\n")
    L.append("TC_RX (priority 40, below the periodic tasks except SCHED) under a flood of "
             "invalid TCs, each costing one rejection:\n")
    L.append("| Uplink | TC/s max | TC_RX load | U with TC_RX |\n|---|---:|---:|---:|")
    for name, rate in LINK.items():
        ut = rate * ops["reject"][1] / 1000
        L.append(f"| {name} | {rate:.1f} | {ut:.4f} | {u + ut:.4f} |")
    L.append(f"\nVerdict: the task set {'CLOSES' if closes else 'DOES NOT CLOSE'}"
             f" (every R <= T{', no missed period' if rm else ''}).")
    text = "\n".join(L) + "\n"
    with open(a.out, "w") as f:
        f.write(text)
    if not a.quiet:
        print(text)

    if a.report:
        notes = []
        if measured_ops:
            for k in OPS:
                if k in sis:
                    d = (sis[k][1] - est[k][1]) / est[k][1]
                    if abs(d) > MEANINGFUL:
                        notes.append(f"`{k}` measured {sis[k][1]:.3f} ms against an estimate of "
                                     f"{est[k][1]:.3f} ms ({d * 100:+.0f} %)")
        for k, v in rm.items():
            e = est_tasks.get(k)
            if e and abs(v["cpu_max"] - e[0]) / e[0] > 0.05:
                notes.append(f"{k} C max {v['cpu_max']:.3f} ms against {e[0]:.3f} ms expected "
                             f"({diff(v['cpu_max'], e[0])})")
        block = [
            "**Measured on SIS** (`docs/timing/budget-sis.md`, generated from "
            "`sis-sec-timing.log` and `sis-rm.log` by `budget.py`).\n",
            "| Quantity | Estimate | SIS | Difference |", "|---|---:|---:|---:|"]
        for k in OPS:
            if k in sis:
                block.append(f"| {k}, max (ms) | {est[k][1]:.3f} | {sis[k][1]:.3f} | "
                             f"{diff(sis[k][1], est[k][1])} |")
        if measured_ops:
            block.append(f"| MAC per TC (ms) | {est['accept'][1] - est['plain'][1]:.3f} | "
                         f"{mac:.3f} | {diff(mac, est['accept'][1] - est['plain'][1])} |")
        for k in ("ACS_CTRL", "TM_HK", "PL_LOG"):
            if k in rm:
                block.append(f"| {k} C max (ms) | {est_tasks[k][0]:.3f} | {rm[k]['cpu_max']:.3f} | "
                             f"{diff(rm[k]['cpu_max'], est_tasks[k][0])} |")
        if rm:
            u3 = sum(rm[k]["cpu_max"] / S4[k][1] for k in S4 if k in rm)
            block.append(f"| U, three Session 3 tasks | {u_est:.4f} | {u3:.4f} | "
                         f"{diff(u3, u_est)} |")
            block.append(f"| U, all {n} periodic tasks | - | {u:.4f} | - |")
            block.append(f"| Missed periods in 60 s | 0 | {missed} | - |")
        block.append("")
        if notes:
            block.append("Differences above 25 % for operations, or 5 % for task C: " +
                         "; ".join(notes) + ".")
        else:
            block.append("No operation differs from its estimate by more than 25 %, and no "
                         "task C by more than 5 %.")
        block.append(f"\nWith the measured values the task set {'still closes' if closes else 'DOES NOT CLOSE'}: "
                     f"U = {u:.4f} for {n} periodic tasks (bound {bound:.4f}), largest R/T = "
                     f"{max(r[k] / tasks[k][1] for k in tasks):.3f}"
                     + (f", {missed} missed periods." if rm else "."))
        rp = open(a.report).read()
        begin, end = "<!-- SIS-TIMING:BEGIN -->", "<!-- SIS-TIMING:END -->"
        i, j = rp.index(begin) + len(begin), rp.index(end)
        rp = rp[:i] + "\n" + "\n".join(block) + "\n" + rp[j:]
        with open(a.report, "w") as f:
            f.write(rp)
        print(f"updated {a.report}")


if __name__ == "__main__":
    main()
