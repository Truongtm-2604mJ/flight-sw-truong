#!/usr/bin/env python3
"""Lab 6.3 - Coverage report for the generated module.

Reads the gcov output (gcov -b -c) and the generator's trace file, and
reports statement and branch coverage of the FLIGHT code of mode_sm.c.
The `#ifdef MODE_SM_WITH_NAMES` block (name strings for test logs) is
compiled out of every flight build and is reported separately.

Every uncovered line or branch is listed with the model element it
belongs to and a classification:
  unreachable by construction   with the argument, derived from the model
  untested                      needs a test (none expected)

Usage: coverage_report.py mode_sm.c.gcov mode_sm_trace.json
"""

import json
import re
import sys

LINE = re.compile(r"^\s*([0-9#=-]+)\*?:\s*(\d+):(.*)$")
BRANCH = re.compile(r"^branch\s+(\d+)\s+(taken (\d+)|never executed)")


def element_of(trace, lineno):
    """The narrowest model element whose generated lines contain lineno."""
    best, width = "step", None
    for key, spans in trace["source"].items():
        if not key.startswith(("select:", "transition:", "entry:", "exit:",
                               "invalid_state", "semantics:", "boot_states")):
            continue
        for sp in spans:
            a, b = sp["lines"]
            if a <= lineno <= b and (width is None or b - a < width):
                best, width = key, b - a
    return best


def argue(trace, element, branch_no):
    """Argument for an untaken guard branch on a transition selector."""
    if not element.startswith("select:"):
        return None
    tid = element.split(":")[1]
    ts = trace["transitions"]
    t = next(x for x in ts if x["id"] == tid)
    if branch_no != 3 or t["guard"] == "true":
        return None
    comp = t["guard"][1:] if t["guard"].startswith("!") else "!" + t["guard"]
    earlier = [x for x in ts if x["from"] == t["from"] and x["event"] == t["event"]
               and x["priority"] < t["priority"] and x["guard"] == comp]
    if not earlier:
        return None
    e = earlier[0]
    return (f"unreachable by construction: {tid} is tested only after "
            f"{e['id']} (same state {t['from']}, same event {t['event']}, "
            f"priority {e['priority']} < {t['priority']}) has been found not "
            f"enabled. With {t['event']} pending, {e['id']} not enabled means "
            f"its guard `{e['guard']}` is false, so `{t['guard']}` is true "
            f"whenever this condition is evaluated (the inputs are a const copy "
            f"taken at the start of step(), so they cannot change between the "
            f"two evaluations): the false outcome of the "
            f"guard term cannot occur. The guard is kept so each transition's "
            f"condition reads as in the model and stays correct if priorities "
            f"are reordered.")


def main():
    gcov, trace = sys.argv[1], json.load(open(sys.argv[2]))
    rows = []                     # (lineno, count or None, text, branches)
    cur = None
    in_names = False
    for raw in open(gcov):
        m = LINE.match(raw)
        if m:
            cnt, no, text = m.group(1), int(m.group(2)), m.group(3)
            if "#ifdef MODE_SM_WITH_NAMES" in text:
                in_names = True
            cur = {"no": no, "cnt": cnt, "text": text.strip(),
                   "branches": [], "names": in_names}
            rows.append(cur)
            if in_names and text.strip() == "#endif":
                in_names = False
            continue
        b = BRANCH.match(raw)
        if b and cur is not None:
            taken = int(b.group(3)) if b.group(3) else None
            cur["branches"].append((int(b.group(1)), taken))

    def stats(sel):
        exe = [r for r in sel if r["cnt"] != "-"]
        hit = [r for r in exe if r["cnt"] not in ("#####", "=====")]
        brs = [(r, n, t) for r in sel for n, t in r["branches"]]
        taken = [x for x in brs if x[2]]
        return exe, hit, brs, taken

    flight = [r for r in rows if not r["names"]]
    names = [r for r in rows if r["names"]]
    exe, hit, brs, taken = stats(flight)
    nexe, nhit, nbrs, ntaken = stats(names)

    out = ["# Coverage of the generated module mode_sm.c", "",
           f"Model sha256 `{trace['model_sha256'][:16]}...`, measured with "
           f"gcov over the whole Lab 6.3 suite (back-to-back sequences and "
           f"requirement tests).", "",
           "| Code | Statements | Branches taken |", "|---|---|---|",
           f"| Flight code | {len(hit)}/{len(exe)} "
           f"({100 * len(hit) / len(exe):.1f} %) | {len(taken)}/{len(brs)} "
           f"({100 * len(taken) / len(brs):.1f} %) |",
           f"| Test-only names block (`MODE_SM_WITH_NAMES`, not in flight "
           f"builds) | {len(nhit)}/{len(nexe)} | {len(ntaken)}/{len(nbrs)} |",
           "", "## Uncovered items in flight code", ""]
    n_untested = 0
    items = []
    for r in flight:
        if r["cnt"] in ("#####", "====="):
            items.append((r["no"], "statement", r["text"],
                          element_of(trace, r["no"]), None))
        for n, t in r["branches"]:
            if not t:
                el = element_of(trace, r["no"])
                items.append((r["no"], f"branch {n}", r["text"], el,
                              argue(trace, el, n)))
    if not items:
        out.append("None.")
    else:
        out += ["| Line | Item | Model element | Classification |",
                "|---|---|---|---|"]
        for no, what, text, el, why in items:
            if why is None:
                n_untested += 1
            out.append(f"| {no} | {what} | `{el}` | "
                       f"{why or '**untested**'} |")
    out += ["", f"Untested items: {n_untested}.", ""]
    print("\n".join(out))
    return 1 if n_untested else 0


if __name__ == "__main__":
    sys.exit(main())
