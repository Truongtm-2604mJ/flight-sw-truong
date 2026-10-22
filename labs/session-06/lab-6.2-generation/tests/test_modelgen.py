#!/usr/bin/env python3
"""Lab 6.2 - the generator refuses models it must not generate from.

Each case mutates the real model in one way and checks that modelgen
rejects it with the expected message. Run: python3 test_modelgen.py
"""

import copy
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))

import yaml  # noqa: E402
import modelgen  # noqa: E402

MODEL = os.path.join(HERE, "..", "..", "lab-6.1-model", "mode_machine.yaml")


def base():
    with open(MODEL) as f:
        return yaml.safe_load(f)


def find(m, tid):
    return next(t for t in m["transitions"] if t["id"] == tid)


def state(m, name):
    return next(s for s in m["states"] if s["name"] == name)


def mutate_unhandled(m):        # SAFE stops ignoring fault_critical
    state(m, "SAFE")["ignore"].remove("fault_critical")


def mutate_guard_gap(m):        # NOMINAL: fault_critical refusal removed
    m["transitions"].remove(find(m, "T05"))


def mutate_priority_clash(m):
    find(m, "T03")["priority"] = 1


def mutate_handled_and_ignored(m):
    state(m, "NOMINAL")["ignore"].append("tc_mode_safe")


def mutate_unknown_input(m):
    find(m, "T02")["guard"] = "!fdir_disabled"


def mutate_self_transition(m):
    t = find(m, "T03")
    t["to"] = "NOMINAL"


def mutate_unreachable(m):      # DEGRADED can no longer be entered
    for tid in ("T03", "T04", "T11"):
        m["transitions"].remove(find(m, tid))
    state(m, "NOMINAL")["ignore"] += ["tc_mode_degraded", "fault_noncritical"]
    state(m, "SAFE")["ignore"].append("tc_mode_degraded")
    m["transitions"].remove(find(m, "T06"))


def mutate_semantics(m):
    m["semantics"]["max_transitions_per_cycle"] = 2


def mutate_semantics_missing(m):
    del m["semantics"]["self_transition_runs_entry"]


def mutate_bad_guard(m):
    find(m, "T02")["guard"] = "fdir_inhibited &&"


CASES = [
    (mutate_unhandled, "neither handled nor ignored"),
    (mutate_guard_gap, "enables no transition"),
    (mutate_priority_clash, "share a priority"),
    (mutate_handled_and_ignored, "both handled and ignored"),
    (mutate_unknown_input, "not a declared input"),
    (mutate_self_transition, "self transitions are not supported"),
    (mutate_unreachable, "unreachable states"),
    (mutate_semantics, "not supported"),
    (mutate_semantics_missing, "must be stated explicitly"),
    (mutate_bad_guard, "expected operand"),
]


def main():
    failed = 0
    good = base()
    try:
        modelgen.validate(copy.deepcopy(good))
        print("[ OK ] real model accepted")
    except modelgen.ModelError as exc:
        print(f"[FAIL] real model rejected: {exc}")
        failed += 1
    for fn, expected in CASES:
        m = copy.deepcopy(good)
        fn(m)
        try:
            modelgen.validate(m)
            print(f"[FAIL] {fn.__name__}: accepted")
            failed += 1
        except modelgen.ModelError as exc:
            ok = expected in str(exc)
            failed += 0 if ok else 1
            print(f"[{' OK ' if ok else 'FAIL'}] {fn.__name__}: {exc}")
    print(f"\n{len(CASES) + 1} cases, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
