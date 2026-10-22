#!/usr/bin/env python3
"""Lab 6.3 - Test sequence generator for the back-to-back harness.

Derives the sequences from the model (Lab 6.1), so that "every
transition, every guard both ways, every unhandled event in every state"
is a property of how the sequences are built, not a hope:

  suite_a.seq   one event per cycle (exact equivalence expected)
                - every (state, event, inhibit) combination, reached by a
                  path of events from the cold boot state; this covers
                  every transition, both values of every guard and every
                  ignored event in every state
                - each fault event with every cause value the hand written
                  code distinguishes, plus undefined ones (fail closed)
  suite_b.seq   several events in one cycle (competing transitions)
                - every ordered pair of distinct events, in every state,
                  with inhibit off and on
                - every ordered triple of {one ground command, two faults}
  soak_a.seq    random, one event per cycle, seeded
  soak_b.seq    random, 0 to 3 events per cycle, seeded

Line format, read by b2b.c:
    CASE <id> <suite>
    CYCLE <inhibit 0|1> <event[:src]>[,<event[:src]>...] | -
    END

src is the cause value given to the hand written mode_request(); for
ground commands it is always 1 (CAUSE_GROUND). Faults default to 2
(CAUSE_MONITOR).
"""

import itertools
import os
import random
import sys

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL = os.path.join(HERE, "..", "lab-6.1-model", "mode_machine.yaml")
OUT = os.path.join(HERE, "sequences")

# Cause values of Session 5 modes.h, plus values outside the enum.
CAUSES = {"BOOT": 0, "MONITOR": 2, "LIVENESS": 3, "WATCHDOG_RESET": 4,
          "UNDEFINED_5": 5, "UNDEFINED_99": 99, "UNDEFINED_255": 255}
FAULTS = ("fault_noncritical", "fault_critical", "fault_cleared")
SEED_A, SEED_B = 20261021, 6


def load():
    with open(MODEL) as f:
        return yaml.safe_load(f)


def path_to(m, target):
    """Shortest event path from the initial state (breadth first)."""
    start = m["initial"]
    prev = {start: None}
    frontier = [start]
    while frontier:
        nxt = []
        for s in frontier:
            for t in m["transitions"]:
                if t["from"] == s and not t.get("internal") and \
                        t["to"] not in prev and t.get("guard", "true") in \
                        ("true", "!fdir_inhibited"):
                    prev[t["to"]] = (s, t["event"])
                    nxt.append(t["to"])
        frontier = nxt
    path, s = [], target
    while prev[s] is not None:
        s, e = prev[s]
        path.append(e)
    return list(reversed(path))


def tok(event, src=None):
    if event.startswith("tc_"):
        return event
    return f"{event}:{src}" if src is not None else event


class Out:
    def __init__(self, name):
        self.f = open(os.path.join(OUT, name), "w")
        self.n = 0

    def case(self, cid, suite, cycles, comment=None):
        if comment:
            self.f.write(f"# {comment}\n")
        self.f.write(f"CASE {cid} {suite}\n")
        for inh, evs in cycles:
            self.f.write(f"CYCLE {inh} {','.join(evs) if evs else '-'}\n")
        self.f.write("END\n")
        self.n += 1

    def close(self):
        self.f.close()
        return self.n


def main():
    m = load()
    states = [s["name"] for s in m["states"]]
    events = [e["name"] for e in m["events"]]
    os.makedirs(OUT, exist_ok=True)

    # ---- suite A -------------------------------------------------------------
    a = Out("suite_a.seq")
    a.f.write("# Suite A: one event per cycle, generated from the model.\n")
    k = 0
    for s in states:
        setup = [(0, [e]) for e in path_to(m, s)]
        for e in events:
            for inh in (0, 1):
                k += 1
                a.case(f"A-{k:03d}", "A",
                       setup + [(inh, [tok(e)]), (inh, [])],
                       f"{s}: {e}, inhibit {inh}")
    for s in states:
        setup = [(0, [e]) for e in path_to(m, s)]
        for e in FAULTS:
            for name, src in CAUSES.items():
                for inh in (0, 1):
                    k += 1
                    a.case(f"A-{k:03d}", "A",
                           setup + [(inh, [tok(e, src)])],
                           f"{s}: {e} with cause {name}, inhibit {inh}")
    # A long single-event walk through every transition in turn.
    walk = []
    for t in m["transitions"]:
        for e in path_to(m, t["from"]):
            walk.append((0, [e]))
        inh = 1 if t.get("guard") == "fdir_inhibited" else 0
        walk.append((inh, [tok(t["event"])]))
        walk.append((0, ["tc_mode_nominal"]))
    k += 1
    a.case(f"A-{k:03d}", "A", walk, "every transition in one run")
    na = a.close()

    # ---- suite B -------------------------------------------------------------
    b = Out("suite_b.seq")
    b.f.write("# Suite B: several events in one cycle (competing transitions).\n")
    k = 0
    for s in states:
        setup = [(0, [e]) for e in path_to(m, s)]
        for e1, e2 in itertools.permutations(events, 2):
            for inh in (0, 1):
                k += 1
                b.case(f"B-{k:03d}", "B", setup + [(inh, [tok(e1), tok(e2)])],
                       f"{s}: {e1} then {e2}, inhibit {inh}")
        grounds = [e for e in events if e.startswith("tc_")]
        for g in grounds:
            for order in itertools.permutations(
                    [g, "fault_noncritical", "fault_critical"], 3):
                k += 1
                b.case(f"B-{k:03d}", "B",
                       setup + [(0, [tok(e) for e in order])],
                       f"{s}: {' then '.join(order)}")
    nb = b.close()

    # ---- soak -------------------------------------------------------------------
    rnd = random.Random(SEED_A)
    sa = Out("soak_a.seq")
    sa.f.write(f"# Soak A: random, one event per cycle, seed {SEED_A}.\n")
    cycles = []
    for _ in range(5000):
        inh = 1 if rnd.random() < 0.2 else 0
        if rnd.random() < 0.15:
            cycles.append((inh, []))
            continue
        e = rnd.choice(events)
        src = rnd.choice(list(CAUSES.values())) if e.startswith("fault") else None
        cycles.append((inh, [tok(e, src)]))
    sa.case("SOAK-A", "SA", cycles, "5000 cycles")
    sa.close()

    rnd = random.Random(SEED_B)
    sb = Out("soak_b.seq")
    sb.f.write(f"# Soak B: random, 0 to 3 events per cycle, seed {SEED_B}.\n")
    cycles = []
    n_events = 0
    for _ in range(5000):
        inh = 1 if rnd.random() < 0.2 else 0
        evs = []
        for e in rnd.sample(events, rnd.choice([0, 1, 1, 2, 2, 3])):
            src = rnd.choice(list(CAUSES.values())) if e.startswith("fault") else None
            evs.append(tok(e, src))
        n_events += len(evs)
        cycles.append((inh, evs))
    sb.case("SOAK-B", "SB", cycles, f"5000 cycles, {n_events} events")
    sb.close()

    print(f"test_gen: suite A {na} cases, suite B {nb} cases, "
          f"soak A 5000 cycles, soak B 5000 cycles ({n_events} events)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
