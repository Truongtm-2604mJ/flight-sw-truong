# Divergence log: hand written `modes.c` vs generated `mode_sm.c`

Harness: `b2b.c`. Sequences: `test_gen.py`, derived from the model. Raw records: `results/divergences.csv`. Counts and one example per pattern: `results/divergence-summary.md`.

## Runs

| Run | Model | Single-event cycles (suites A, SA) | Multi-event cycles (suites B, SB) | Outcome |
|---|---|---|---|---|
| 1 | version 1 (`results/run1-model-v1/`) | **502** of 7996 diverge | 1140 of 2730 diverge, 223 of them unclassified (all D-01: same mode, monitoring rate differs) | D-01 found; model fixed |
| 2 | version 2 (final, `results/`) | **0** of 7996 diverge | 998 of 2730 diverge, 0 unclassified, plus 564 timing-only differences | D-02 to D-06 explained |

Together the suites compare 10 726 cycles carrying 12 752 events.

Suite A covers every state × event × inhibit combination, and every fault with seven cause values (four defined causes other than GROUND, plus 5, 99 and 255). Suite B covers every ordered pair of events and every ground-plus-two-faults triple, in every state. Each soak is 5000 seeded random cycles.

With one event per cycle the two implementations are **identical** once the model is corrected: same mode, same outputs, and same reports in the same order, every cycle. All remaining divergences need two or more requests in the same cycle. There, the hand written code has no defined behaviour (ambiguity A1), so a difference is expected; the question for each is which behaviour is acceptable.

"Timing only" means the same final mode, outputs and reports, but the generated machine needed more than one step to drain a latched event. It is not counted as a divergence; the maximum drain is 3 steps.

## Divergences

| ID | Class (as in the CSV) | Count | Example (start state: events in arrival order) | Hand written | Generated | Cause | At fault |
|---|---|---:|---|---|---|---|---|
| D-01 | single-event mismatch (run 1 only) | 502 + 223 | NOMINAL: `tc_mode_safe` | SAFE, monitoring 2 Hz | SAFE, monitoring **1 Hz** | Model v1: SAFE entry lacked `monitors_tighten`. Session 5 derives the monitoring rate from the mode in another file (`fdir_hk_cycle`), which I missed when listing entry actions. | **Specification incomplete.** Fixed in model v2; 0 occurrences in run 2. |
| D-02 | intermediate transitions (hand) | 236 | NOMINAL: `tc_mode_degraded`, `tc_mode_safe` | NOMINAL → DEGRADED → SAFE, both entry action sets run | NOMINAL → SAFE; `tc_mode_degraded` discarded and reported | Hand acts on each request at once; the model takes one transition per cycle. | **Neither.** Same end state and outputs. The hand version briefly suspends the payload for a mode it immediately leaves; harmless here. |
| D-03 | ground overrides same-cycle fault (hand) | 105 | DEGRADED: `fault_critical`, `tc_mode_nominal` | DEGRADED → SAFE → NOMINAL | DEGRADED → SAFE; ground command discarded and reported | Ground command sent before the operators could know of the fault; hand applies it last. | **Hand written, genuine weakness.** A safing decision can be undone by a command that crossed it in flight. The model is safer, and the operators are told their command was dropped. |
| D-04 | fault lost to order (hand) | 158 | DEGRADED: `fault_noncritical`, `tc_mode_nominal` | NOMINAL (fault ignored before the ground command arrived) | NOMINAL, then DEGRADED next cycle | Hand forgets a fault that arrives when the mode already matches it (ambiguity A3). | **Hand written, genuine defect.** The vehicle ends in NOMINAL with an alarm active and no further event to act on. |
| D-05 | conflicting ground commands | 365 | NOMINAL: `tc_mode_degraded`, `tc_mode_nominal` | DEGRADED → NOMINAL (last wins) | DEGRADED (`tc_mode_nominal` ignored, as NOMINAL is the start state) | Two contradictory ground commands in one 2 s cycle. | **Specification choice.** Hand: last command wins. Model: priority wins, conservatively. Operators should not do this; the model's answer is at least deterministic. A gap remains: the losing command is dropped **silently** here, because it is in the start state's ignore list (see Recommendations). |
| D-06 | same end state but different cause attribution | 134 | NOMINAL: `fault_noncritical`, `tc_mode_degraded` | NOMINAL → DEGRADED, cause MONITOR | NOMINAL → DEGRADED, cause GROUND | Priority: ground DEGRADED (T03) outranks autonomous DEGRADED (T04). | **Neither.** Same mode. The report names the ground, because the ground asked for it too. In inhibited variants the model reports a refusal that the hand version does not, which is extra information, not a loss. |
| D-07 | system level, latency (`lab-6.2-generation/logs/host-sim-modes.log`) | 2 of 51 checks | Ground TC(8,1) SAFE, then TC(8,1) NOMINAL | Transition when the TC is executed | Transition at the next supervisor cycle, up to 2 s later | Cyclic evaluation (`semantics.md`, Q2). | **Neither; it is the cost of the chosen semantics.** The two Session 5 checks encode the hand version's immediate effect. One counts a full HK report sent in the up to 2 s before SAFE is entered; the other ends 1 s after the TC. The behaviour after the transition is as required (log lines 2 to 8 s). |

## Summary

| Classification | Divergences |
|---|---|
| Specification incomplete, fixed | D-01 |
| Genuine weakness or defect in the hand written version, exposed by the formalisation | D-03, D-04 |
| Specification choice (the hand version has no defined behaviour) | D-05 |
| Harmless difference in path or report | D-02, D-06 |
| Cost of the execution semantics, accepted | D-07 |

No divergence was a defect of the generated code with respect to its model. Every difference traces back to the model, or to the hand written code.

## Recommendations carried forward

1. **Back-port D-04 to Session 5**, for as long as the hand written version is the one that flies: keep a pending-fault flag, so a fault that arrives while already at its severity survives a ground de-escalation.
2. **Report a ground command dropped because the start state ignores it** (D-05), as well as one lost to priority. The model treats "ignored" as "already satisfied", which is true for a single command but not for one that conflicts with another in the same cycle.
3. **Decide on the 2 s latency (D-07) explicitly.** If it is too slow for ground operations, step the machine from a faster task, or once more right after TC(8,1). Either is a change to the stated semantics, not an implementation detail.
