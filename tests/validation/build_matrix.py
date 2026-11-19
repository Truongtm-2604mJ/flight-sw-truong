#!/usr/bin/env python3
"""LOTUS-1 traceability matrix: every requirement to its tests at every level
(started in Lab 7.4).

Reads only evidence files, never typed results:
  evidence/requirements.yaml                      (the capstone requirement set)
  evidence/lower-level-tests.yaml
  evidence/test-runs/unit-results.txt             (Unity output of every suite)
  evidence/test-runs/integration-host-sim.log
  labs/session-06/lab-6.3-equivalence/results/req-tests.txt, lab-6.2-generation/logs/latency-*.txt,
  lab-6.2-generation/tests/results.txt            (Session 6 tests of the same generated mode_sm.c)
  evidence/validation/results-<target>/results.json (validation campaigns)
  evidence/ncr.yaml

Writes evidence/traceability-matrix.md and evidence/ncr-log.md. A requirement
with no test at any level is a GAP; a test named in the mapping but absent
from the results is MISSING. Either makes the script exit 1.
"""
import glob
import json
import os
import re
import sys

import yaml

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EV = os.path.join(ROOT, "evidence")
S6 = os.path.join(ROOT, "labs", "session-06")



def load_requirements():
    reqs = yaml.safe_load(open(os.path.join(EV, "requirements.yaml")))["requirements"]
    order = {}
    for r in reqs:
        order.setdefault(r["group"], len(order))
    return sorted(reqs, key=lambda r: (order[r["group"]], r["id"]))


def main():
    reqs = load_requirements()
    mapping = yaml.safe_load(open(os.path.join(EV, "lower-level-tests.yaml")))
    unit = {}
    for line in open(os.path.join(EV, "test-runs", "unit-results.txt")):
        m = re.match(r"^\S+\.c:\d+:(test_\w+):(PASS|FAIL)", line.strip())
        if m:
            unit[m.group(1)] = m.group(2)
    integ = {}
    for line in open(os.path.join(EV, "test-runs", "integration-host-sim.log"), errors="replace"):
        m = re.match(r"^(IT-\d+)\s.*\s(PASS|FAIL)\s*$", line)
        if m:
            integ[m.group(1)] = m.group(2)
    s6 = {}
    files = [os.path.join(S6, "lab-6.3-equivalence", "results", "req-tests.txt")]
    files += glob.glob(os.path.join(S6, "lab-6.2-generation", "logs", "latency-*.txt"))
    for f in files:
        for line in open(f):
            m = re.match(r"^(TC-\S+)\s+\S+\s+(PASS|FAIL)\s*$", line)
            if m:
                s6[m.group(1)] = m.group(2)
    gen = open(os.path.join(S6, "lab-6.2-generation", "tests", "results.txt")).read()
    s6["TC-GEN-01"] = "PASS" if re.search(r"\d+ cases, 0 failed", gen) else "FAIL"
    # Requirement links come from the test definitions (run_campaign.TESTS),
    # the single source; verdicts come from each campaign's results.json.
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import run_campaign  # noqa: E402
    links = {t["id"]: t["reqs"] for t in run_campaign.TESTS}
    campaigns = {}
    for f in sorted(glob.glob(os.path.join(EV, "validation", "results-*", "results.json"))):
        d = json.load(open(f))
        for tid, res in d["results"].items():
            res["reqs"] = links.get(tid, res["reqs"])
        campaigns[d["target"]] = d
    ncrs = yaml.safe_load(open(os.path.join(EV, "ncr.yaml")))["ncrs"]

    missing, gaps = [], []
    rows = []
    counts = {}
    for r in reqs:
        rid = r["id"]
        m = mapping.get(rid) or {}

        def cell(names, results):
            out = []
            for n in names:
                v = results.get(n)
                if v is None:
                    missing.append(f"{rid}: {n}")
                    v = "MISSING"
                out.append(f"{n} {v}")
            return out

        u = cell(m.get("unit", []), unit)
        it = cell(m.get("integration", []), integ)
        s = cell(m.get("s6", []), s6)
        for an in m.get("analysis", []):
            path = os.path.join(ROOT, an["file"])
            if not os.path.exists(path):
                v = "NOT RUN"
            else:
                v = "PASS" if re.search(an["expect"], open(path, errors="replace").read()) \
                    else "FAIL"
            s.append(f"{an['name']} (`{an['file']}`) {v}")
        vals = {}
        for target, d in campaigns.items():
            vals[target] = [f"{tid} {res['verdict']}" for tid, res in d["results"].items()
                            if rid in res["reqs"]]
        all_v = u + it + s + [x for v in vals.values() for x in v]
        verdicts = ["NOT RUN" if x.endswith("NOT RUN") else x.rsplit(" ", 1)[1] for x in all_v]
        linked = [n for n in ncrs if n["kind"] == "NCR" and rid in n["requirement"]]
        if not all_v:
            status = "GAP"
            gaps.append(rid)
        elif "FAIL" in verdicts or "MISSING" in verdicts:
            status = "FAIL" + "".join(f" ({n['id']}, {n['status']})" for n in linked)
        elif "NOT RUN" in verdicts:
            status = "PASS (target part NOT RUN)"
        elif "N/A" in verdicts:
            status = "PASS (part N/A on a target)"
        else:
            status = "PASS"
        key = status.split(" ")[0]
        counts[key] = counts.get(key, 0) + 1
        rows.append((r, u, it, s, vals, status))

    targets = list(campaigns)
    out = ["# LOTUS-1 traceability matrix: requirements to tests, all levels", "",
           "Generated by `tests/validation/build_matrix.py` from the test outputs. Levels: unit "
           "(Unity, host), integration (host simulation of RTEMS), analysis (timing evidence) and Session 6 model tests "
           "(generated mode machine), validation (ground interface only"
           + "".join(f"; {t}: {campaigns[t]['executed']} UTC" for t in targets) + ").", "",
           "Method: T test through the ground interface; T-I test at integration level (the ground "
           "cannot set up the condition); A analysis.", "",
           f"**{len(reqs)} requirements: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items()))
           + f"; {len(gaps)} without any test (GAP).**", ""]
    group = None
    for r, u, it, s, vals, status in rows:
        if r["group"] != group:
            group = r["group"]
            out += [f"## {group}", "",
                    "| Req | Requirement | Method | Design | Code (src/) | Unit | Integration | Analysis, model tests | "
                    + " | ".join(f"Validation ({t})" for t in targets) + " | Status |",
                    "|---|---|---|---|---|---|---|---|" + "---|" * len(targets) + "---|"]
        out.append(f"| {r['id']} | {r['text']} | {r['method']} | {r.get('design', '-')} | "
                   f"{r.get('code', '-')} | {'<br>'.join(u) or '-'} | "
                   f"{'<br>'.join(it) or '-'} | {'<br>'.join(s) or '-'} | "
                   + " | ".join('<br>'.join(vals.get(t, [])) or '-' for t in targets)
                   + f" | **{status}** |")
        if r is rows[-1][0] or rows[[x[0] for x in rows].index(r) + 1][0]["group"] != group:
            out.append("")
    out += ["## Requirements without a validation test, and why", ""]
    for r, u, it, s, vals, status in rows:
        if not any(vals.values()):
            out.append(f"- **{r['id']}** (method {r['method']}): verified at "
                       f"{'unit' if u else ''}{', ' if u and (it or s) else ''}"
                       f"{'integration' if it else ''}{', ' if it and s else ''}"
                       f"{'analysis or model tests' if s else ''} level only.")
    out.append("")
    if missing:
        out += ["## MISSING results", ""] + [f"- {x}" for x in missing] + [""]
    with open(os.path.join(EV, "traceability-matrix.md"), "w") as f:
        f.write("\n".join(out))
    write_ncr_log(ncrs)
    print(f"build_matrix: {len(reqs)} requirements; {counts}; gaps {gaps}; missing {missing}")
    return 1 if gaps or missing else 0


def write_ncr_log(ncrs):
    """ncr-log.md from ncr.yaml: the single source of the NCR records."""
    kinds = {"NCR": "Nonconformance", "TA": "Test anomaly", "OBS": "Validation observation"}
    out = ["# Nonconformance log", "",
           "Generated from `ncr.yaml` by `tests/validation/build_matrix.py`.", "",
           "Kinds: **NCR** the product does not meet a requirement; **TA** test anomaly, the test "
           "was wrong and the product was not changed; **OBS** validation observation, no "
           "requirement violated but the behaviour may not meet the mission need.", "",
           "Severity (NCR): critical, loss of vehicle or mission possible; major, requirement "
           "violated with operational impact and no workaround; minor, violated, with a "
           "workaround and no safety impact.", "",
           "| ID | Kind | Requirement | Severity | Status | Found by |", "|---|---|---|---|---|---|"]
    for n in ncrs:
        out.append(f"| {n['id']} | {n['kind']} | {n['requirement']} | {n.get('severity', '-')} | "
                   f"**{n['status']}** | {n['found_by']} |")
    out.append("")
    for n in ncrs:
        out += [f"## {n['id']}: {kinds[n['kind']]}, {n['requirement']}", "",
                f"**Status:** {n['status']}" + (f". **Severity:** {n['severity']}"
                                                  if n.get('severity') else "") + ".", "",
                f"**Description.** {n['description']}", ""]
        if n.get("cause"):
            out += [f"**Cause.** {n['cause']}", ""]
        out += [f"**Disposition.** {n['disposition']}", ""]
        if n.get("retest"):
            out += [f"**Retest.** {n['retest']}", ""]
    with open(os.path.join(EV, "ncr-log.md"), "w") as f:
        f.write("\n".join(out))


if __name__ == "__main__":
    sys.exit(main())
