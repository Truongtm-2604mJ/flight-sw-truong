"""Session 8 timing budget with authentication (security-report.md S4).

Inputs, best available first:
  --sec-timing  output of app/sec-timing.exe on SIS (cost per operation)
  --rm          output of app/lab8-timing.exe on SIS (rate monotonic stats
                of the full Session 8 flight build, 60 s)
Without them it uses estimate.txt (instruction counts calibrated on the
Session 4 SIS run) and the Session 4 SIS task measurements.

Writes budget.md (or --out) and prints it.
"""
import argparse
import math
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))

# Session 4 SIS timing run (tmtc-design.md S5): C max (ms), T (ms), priority
S4 = {"ACS_CTRL": (18.957, 100, 10), "TM_HK": (47.764, 500, 20),
      "PL_LOG": (95.644, 1000, 30)}
RM_NAMES = {"ACS1": "ACS_CTRL", "HK11": "TM_HK", "LOG1": "PL_LOG"}

# Uplink assumptions for the TC_RX demand (aperiodic, priority 40).
LINK = {"lab console, 115200 baud, hex lines": 115200 / 10 / (3 + 2 * 120 + 1),
        "S-band TC uplink, 4 kbit/s, 120 octet TCs": 4000 / 8 / 128}


def parse_ops(path, est):
    ops = {}
    with open(path) as f:
        for line in f:
            if est:
                m = re.match(r"(\w+)\s+\d+ instr/op\s+~\s+([\d.]+) us", line)
                if m:
                    ops[m.group(1)] = (float(m.group(2)) / 1000, float(m.group(2)) / 1000)
            else:
                m = re.match(r"\s+(\w+)\s+mean\s+([\d.]+) us\s+max\s+([\d.]+) us", line)
                if m:
                    ops[m.group(1)] = (float(m.group(2)) / 1000, float(m.group(3)) / 1000)
    return ops


def parse_rm(path):
    tasks = {}
    rx = re.compile(r"0x[0-9a-fA-F]+\s+(\S+)\s+(\d+)\s+(\d+)\s+([\d.]+)/([\d.]+)/([\d.]+)")
    with open(path) as f:
        for line in f:
            m = rx.search(line)
            if m and m.group(1) in RM_NAMES:
                tasks[RM_NAMES[m.group(1)]] = (float(m.group(5)) * 1000, int(m.group(3)))
    return tasks


def rta(tasks, blocking):
    """Response time analysis, fixed priority, with blocking B_i."""
    out = {}
    order = sorted(tasks, key=lambda n: tasks[n][2])
    for i, name in enumerate(order):
        c, t, _ = tasks[name]
        b = blocking.get(name, 0.0)
        r = c + b
        for _ in range(100):
            new = c + b + sum(math.ceil(r / tasks[h][1]) * tasks[h][0] for h in order[:i])
            if abs(new - r) < 1e-9:
                break
            r = new
        out[name] = r
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sec-timing")
    ap.add_argument("--rm")
    ap.add_argument("--out", default=os.path.join(HERE, "budget.md"))
    a = ap.parse_args()

    if a.sec_timing:
        ops, src_ops = parse_ops(a.sec_timing, False), f"SIS, `{os.path.basename(a.sec_timing)}`"
    else:
        ops, src_ops = parse_ops(os.path.join(HERE, "estimate.txt"), True), \
            "ESTIMATE (instruction counts, calibrated on Session 4 SIS)"
    lines = [f"# Timing budget with authentication\n", f"Operation costs: {src_ops}.\n"]
    lines.append("| Operation | mean (ms) | max (ms) |\n|---|---:|---:|")
    for k in ("plain", "accept", "reject", "scrub", "load"):
        if k in ops:
            lines.append(f"| {k} | {ops[k][0]:.3f} | {ops[k][1]:.3f} |")
    mac = ops["accept"][1] - ops["plain"][1]

    # TM_HK: scrub every 2nd 500 ms cycle, plus SID 3 and events (~ a TM build).
    hk_extra = ops["scrub"][1] + 0.2
    tasks = {}
    measured = parse_rm(a.rm) if a.rm else {}
    for n, (c, t, p) in S4.items():
        if n in measured:
            tasks[n] = (measured[n][0], t, p)
        else:
            tasks[n] = (c + (hk_extra if n == "TM_HK" else 0.0), t, p)
    src_tasks = (f"SIS rate monotonic statistics of the Session 8 flight build "
                 f"(`{os.path.basename(a.rm)}`)" if measured else
                 "Session 4 SIS C max, plus the Session 8 additions to TM_HK")
    # Longest tc lock section by a task below TM_HK: one TC(210,1),
    # verify + unwrap (+ an NV write, a memcpy on this target).
    b = ops["accept"][1] + ops["load"][1]
    blocking = {"TM_HK": b, "PL_LOG": b}
    r = rta(tasks, blocking)
    u = sum(c / t for c, t, _ in tasks.values())
    lines.append(f"\nTask set: {src_tasks}.\n")
    lines.append("| Task | P | T (ms) | C (ms) | U | B (ms) | R (ms) | R <= T |"
                 "\n|---|---:|---:|---:|---:|---:|---:|---|")
    for n in sorted(tasks, key=lambda k: tasks[k][2]):
        c, t, p = tasks[n]
        lines.append(f"| {n} | {p} | {t} | {c:.3f} | {c / t:.4f} | "
                     f"{blocking.get(n, 0.0):.3f} | {r[n]:.1f} | "
                     f"{'yes' if r[n] <= t else 'NO'} |")
    n = len(tasks)
    lines.append(f"\nPeriodic utilisation U = {u:.4f} (Session 4: 0.3807); "
                 f"Liu and Layland bound for n = {n}: {n * (2 ** (1 / n) - 1):.4f}.\n")
    lines.append("TC_RX (priority 40, below the three periodic tasks) under a flood of "
                 "invalid TCs, each costing one rejection:\n")
    lines.append("| Uplink | TC/s max | TC_RX load | U with TC_RX | bound n = 4 |"
                 "\n|---|---:|---:|---:|---:|")
    for name, rate in LINK.items():
        ut = rate * ops["reject"][1] / 1000
        lines.append(f"| {name} | {rate:.1f} | {ut:.4f} | {u + ut:.4f} | "
                     f"{4 * (2 ** 0.25 - 1):.4f} |")
    lines.append(f"\nMAC cost per command: {mac:.3f} ms (accept minus plain).")
    text = "\n".join(lines) + "\n"
    with open(a.out, "w") as f:
        f.write(text)
    print(text)


if __name__ == "__main__":
    main()
