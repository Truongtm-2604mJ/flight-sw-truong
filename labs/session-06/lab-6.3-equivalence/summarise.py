#!/usr/bin/env python3
"""Lab 6.3 - Summarise divergences.csv by class and by pattern.

A pattern is (start state, inhibit, ordered event types) in suite B,
where every combination occurs once by construction; the soak suites
only add counts. Writes markdown to stdout.

Usage: summarise.py results/divergences.csv
"""

import collections
import csv
import sys


def main():
    rows = list(csv.DictReader(open(sys.argv[1])))
    by_class = collections.defaultdict(list)
    for r in rows:
        by_class[r["class"]].append(r)
    out = ["# Divergence summary", "",
           f"Source: `{sys.argv[1]}` ({len(rows)} rows).", "",
           "| Class | Suite A | Suite B | Soak A | Soak B | Distinct suite B patterns |",
           "|---|---:|---:|---:|---:|---:|"]
    for cls, rs in sorted(by_class.items()):
        n = collections.Counter(r["suite"] for r in rs)
        pats = {(r["hand_start"], r["inhibit"], r["events"]) for r in rs
                if r["suite"] == "B"}
        out.append(f"| {cls} | {n['A']} | {n['B']} | {n['SA']} | {n['SB']} | "
                   f"{len(pats)} |")
    for cls, rs in sorted(by_class.items()):
        out += ["", f"## {cls}", "",
                "| Start | Inhibit | Events in arrival order (event:cause) | "
                "Hand written | Generated |", "|---|---|---|---|---|"]
        seen = set()
        for r in rs:
            if r["suite"] != "B":
                continue
            key = (r["hand_start"], r["inhibit"], r["events"])
            if key in seen:
                continue
            seen.add(key)
            out.append(f"| {r['hand_start']} | {r['inhibit']} | {r['events']} | "
                       f"{r['hand_reports']} -> **{r['hand_final']}** | "
                       f"{r['generated_reports']} -> **{r['generated_final']}** |")
            if len(seen) >= 6:
                out.append(f"| ... | | {len({(x['hand_start'], x['inhibit'], x['events']) for x in rs if x['suite'] == 'B'}) - 6} more in the CSV | | |")
                break
    print("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
