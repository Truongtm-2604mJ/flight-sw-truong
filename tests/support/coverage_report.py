#!/usr/bin/env python3
"""Lab 7.2 - Coverage report by module and by test level.

gcovr (7.2) is run on the gcov data of each level, cumulatively:
    unit                    build/cov/unit
    + integration           build/cov/integration
    + validation            build/cov/validation   (optional, see make coverage-full)
and produces, in --out:
    coverage.md             statement and branch coverage per module and level,
                            and every flight line still not executed, with its
                            classification from justifications.yaml
    html/index.html         gcovr detailed HTML report of the last level
    summary-<level>.json    gcovr JSON summaries

Every flight line not executed at the last level must match an entry of
justifications.yaml (unreachable by construction, defensive and not
triggerable in test, or excluded from the flight build); otherwise it is
listed as UNCLASSIFIED and the script exits 1.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys

import yaml

FLIGHT = [
    ("labs/session-04/lab-4.1-space-packet/sp.c", "core"),
    ("labs/session-04/lab-4.2-pus-service17/crc16.c", "core"),
    ("labs/session-04/lab-4.2-pus-service17/pus.c", "core"),
    ("labs/session-04/lab-4.2-pus-service17/tc_handler.c", "core"),
    ("labs/session-05/lab-5.1-monitoring/monitor.c", "core"),
    ("labs/session-05/lab-5.2-liveness/supervisor.c", "core"),
    ("labs/session-05/lab-5.2-liveness/watchdog.c", "core"),
    ("labs/session-05/lab-5.2-liveness/nvstate.c", "core"),
    ("labs/session-05/lab-5.3-safe-mode/schedule.c", "core"),
    ("labs/session-06/lab-6.2-generation/generated/mode_sm.c", "core (generated)"),
    ("labs/session-05/app/tmtc5.c", "application"),
    ("labs/session-05/app/services.c", "application"),
    ("labs/session-06/app/fdir6.c", "application"),
    ("labs/session-05/app/sensors5.c", "application"),
    ("labs/session-05/app/lab5.c", "application"),
    ("labs/session-05/lab-5.4-fault-injection/finj.c", "test build only"),
]


def gcovr(root, dirs, out_json, html=None):
    # Multithreaded processes dumped while running (gcov_periodic.c) can give
    # gcov a slightly inconsistent snapshot; counters are atomic
    # (-fprofile-update=atomic) and a residual negative count is reported as a
    # warning rather than aborting the report.
    cmd = ["gcovr", "--root", root, "--json-summary", out_json, "--json-summary-pretty",
           "--gcov-ignore-parse-errors=negative_hits.warn"]
    for f, _ in FLIGHT:
        cmd += ["--filter", re.escape(os.path.join(root, f))]
    if html:
        cmd += ["--html-details", html]
        cmd += ["--json", out_json.replace("summary-", "detail-")]
    cmd += dirs
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"gcovr failed:\n{r.stderr}")
    return {os.path.normpath(e["filename"]): e for e in json.load(open(out_json))["files"]}


def pct(c, t):
    return f"{100.0 * c / t:.1f} %" if t else "-"


def parse_ranges(s):
    out = set()
    for part in str(s).split(","):
        part = part.strip()
        if "-" in part:
            a, b = part.split("-")
            out.update(range(int(a), int(b) + 1))
        elif part:
            out.add(int(part))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--objdir", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--justifications", help="classification file (default: <out>/justifications.yaml)")
    ap.add_argument("--no-enforce", action="store_true",
                    help="report unclassified lines without failing (partial levels)")
    a = ap.parse_args()
    jpath = a.justifications or os.path.join(a.out, "justifications.yaml")
    root = os.path.abspath(a.root)
    obj = os.path.abspath(a.objdir)
    os.makedirs(a.out, exist_ok=True)
    levels = []
    dirs = []
    for name, sub in (("unit", "unit"), ("+ integration", "integration"),
                      ("+ validation", "validation")):
        d = os.path.join(obj, sub)
        if os.path.isdir(d) and any(f.endswith(".gcda") for f in os.listdir(d)):
            dirs.append(d)
            levels.append((name, list(dirs)))
    html_dir = os.path.join(a.out, "html")
    shutil.rmtree(html_dir, ignore_errors=True)
    os.makedirs(html_dir)
    results = []
    for i, (name, ds) in enumerate(levels):
        tag = re.sub(r"\W+", "", name) or "unit"
        last = i == len(levels) - 1
        js = os.path.join(a.out, f"summary-{tag}.json")
        res = gcovr(root, ds, js, os.path.join(html_dir, "index.html") if last else None)
        results.append((name, res))

    # Uncovered lines at the last level, from the detailed JSON.
    last_tag = re.sub(r"\W+", "", levels[-1][0]) or "unit"
    detail = json.load(open(os.path.join(a.out, f"detail-{last_tag}.json")))
    uncovered = {}
    for f in detail["files"]:
        fn = os.path.normpath(f["file"])
        uncovered[fn] = sorted(l["line_number"] for l in f["lines"]
                               if l["count"] == 0 and not l.get("gcovr/noncode"))
    just = yaml.safe_load(open(jpath))["justifications"]

    def classify(fn, line):
        for j in just:
            if j["file"] == fn and line in parse_ranges(j["lines"]):
                return j
        return None

    lines = ["# Coverage by module and test level", "",
             "Generated by `tests/support/coverage_report.py` (gcov from GCC 13.3, gcovr 7.2) "
             "from the instrumented host builds. Levels are cumulative. Branch coverage is "
             "gcov's: each outcome of each conditional jump.", ""]
    hdr = "| Module | Kind | " + " | ".join(f"{n}: lines | branches" for n, _ in results) + " |"
    lines += [hdr, "|---|---|" + "---:|---:|" * len(results)]
    tot = [[0, 0, 0, 0] for _ in results]
    core = [[0, 0, 0, 0] for _ in results]
    for f, kind in FLIGHT:
        row = f"| `{f.split('/')[-1]}` | {kind} |"
        for k, (_, res) in enumerate(results):
            e = res.get(os.path.normpath(f))
            if e is None:
                row += " not built | |"
                continue
            row += (f" {e['line_covered']}/{e['line_total']} ({pct(e['line_covered'], e['line_total'])}) |"
                    f" {pct(e['branch_covered'], e['branch_total'])} |")
            for acc in ([tot[k]] + ([core[k]] if kind.startswith("core") else [])):
                acc[0] += e["line_covered"]; acc[1] += e["line_total"]
                acc[2] += e["branch_covered"]; acc[3] += e["branch_total"]
        lines.append(row)
    for label, acc in (("**Core modules**", core), ("**All flight code**", tot)):
        lines.append(f"| {label} | |" + "".join(
            f" **{c[0]}/{c[1]} ({pct(c[0], c[1])})** | **{pct(c[2], c[3])}** |" for c in acc))
    lines += ["", f"Levels: " + "; ".join(f"{n} = {', '.join(os.path.relpath(d, obj) for d in ds)}"
                                        for n, ds in levels), ""]

    lines += ["## Flight lines not executed at the last level", "",
              "| Module | Lines | Classification | Argument |", "|---|---|---|---|"]
    unclassified = []
    groups = {}
    for f, _ in FLIGHT:
        fn = os.path.normpath(f)
        for ln in uncovered.get(fn, []):
            j = classify(fn, ln)
            if j is None:
                unclassified.append((fn, ln))
            else:
                groups.setdefault((fn, j["id"]), []).append(ln)
    for (fn, jid), lns in groups.items():
        j = next(x for x in just if x["id"] == jid)
        lines.append(f"| `{fn.split('/')[-1]}` | {j['lines']} ({len(lns)} not executed) | "
                     f"{j['class']} ({jid}) | {j['argument']} |")
    for fn, ln in unclassified:
        lines.append(f"| `{fn.split('/')[-1]}` | {ln} | **UNCLASSIFIED** | |")
    lines += ["", f"{sum(len(v) for v in uncovered.values())} flight lines not executed; "
              f"{len(unclassified)} unclassified.", ""]
    after = yaml.safe_load(open(jpath)).get(
        "covered_after_analysis", [])
    if after:
        lines += ["## Covered after the first analysis (genuinely untested, now covered by a new test)",
                  "", "The first full analysis is kept in `initial/coverage-initial.md`.", "",
                  "| ID | Where | What | New test |", "|---|---|---|---|"]
        for c in after:
            lines.append(f"| {c['id']} | {c['where']} | {c['what']} | {c['tests']} |")
        lines.append("")
    with open(os.path.join(a.out, "coverage.md"), "w") as fo:
        fo.write("\n".join(lines))
    print("\n".join(lines[: 6 + len(FLIGHT) + 4]))
    if unclassified:
        print(f"{len(unclassified)} uncovered flight lines have no classification:")
        cur = None
        for fn, ln in unclassified:
            if fn != cur:
                print(f"  {fn}:", end="")
                cur = fn
            print(f" {ln}", end="")
        print()
        return 0 if a.no_enforce else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
