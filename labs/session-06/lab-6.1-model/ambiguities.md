# Ambiguities found in the hand written Lab 5.3 implementation

Writing `session-05/lab-5.3-safe-mode/modes.c` down as a model forced a stated answer to questions the C code answered only through the order of its `if` statements, or through the order in which tasks happened to call it. Each item below names the question, what the hand written code does, and what the model now states.

**Origin** says where each item surfaced:

- **writing**: while writing the model;
- **b2b**: only when the back-to-back harness compared the two versions (Lab 6.3, `divergence-log.md`).

| # | Question | Hand written (`modes.c`) | Model | Origin |
|---|---|---|---|---|
| A1 | Two mode requests in the same cycle: which wins? | Undefined. `mode_request()` acts on each call immediately, so the outcome depends on call order. Call order depends on which task runs first: TM_HK (monitor), SUP (escalation), TC_RX (ground). | Unique priorities per state; at most one transition per cycle (`semantics.md`, Q1). | writing |
| A2 | Ground NOMINAL and a critical fault together: who wins? | Whoever calls last. Fault then ground ends in NOMINAL, which undoes a safing decided by FDIR. | SAFE wins; the ground command is discarded and reported. | b2b (D-03) |
| A3 | A fault arrives in a mode already at its severity: what happens to it? | `NO_CHANGE`, and the fault is forgotten. If the ground de-escalates next, the vehicle is in NOMINAL with an active alarm and no further event (the monitor reports transitions only). | Faults are latched and re-evaluated after any transition in the same cycle. | b2b (D-04) |
| A4 | While inhibited, which faults are "refused" and which are simply "no change"? | Decided by the order of two checks: severity first, then inhibit. A critical fault in DEGRADED is refused (reported); a non-critical fault in DEGRADED is no change (silent). | The same outcome, now explicit: `fault_noncritical` is in DEGRADED's ignore list regardless of the inhibit; `fault_critical` with the inhibit set is the internal transition T10, which reports. | writing |
| A5 | What undoes a SAFE action on leaving SAFE? | Nothing needs to. `mode_apply()` recomputes every output from the mode (`sun_pointing = mode == SAFE`), so leaving SAFE clears sun pointing implicitly. | Outputs are actions, so the implicit undo has to be stated: SAFE `on_exit: [sun_pointing_release]`. Without it, sun pointing would stay commanded in NOMINAL. | writing |
| A6 | What is the monitoring rate in SAFE? | 2 Hz, derived in a different file (`fdir.c`, `fdir_hk_cycle()`: `mode != MODE_NOMINAL`), not in the mode manager. | Model version 1 **missed this**: SAFE entry had no `monitors_tighten`, so SAFE entered from NOMINAL kept 1 Hz monitoring. Fixed in version 2. A modelling error, not a defect in the hand written code. | b2b (D-01) |
| A7 | Can the ground go from SAFE to DEGRADED, or only to NOMINAL? | Any mode, by any ground request (`mode_request()` with `CAUSE_GROUND`). | Stated: T11 (SAFE → DEGRADED) and T12 (SAFE → NOMINAL). Two at once: DEGRADED wins. | writing |
| A8 | A ground command for the current mode: a transition with entry actions, or nothing? | Nothing (`target == from` → `NO_CHANGE`), and no report. | Explicitly ignored in each state's `ignore` list. Since model v3 the command is reported as discarded if another event moves the vehicle out of that mode in the same cycle (MM-140, closes D-05). | writing; v3 after the Session 6 review |
| A9 | Is the boot mode a transition? | `mode_init()` runs `apply()`, with no transition report. | The same, stated: boot states are NOMINAL and SAFE, entry actions run, and no report. | writing |
| A10 | Several faults with different causes in one cycle: whose cause is reported? | Each call reports its own transition with its own cause. | One bit per event, last payload wins (coalescing); stated as a limitation. | writing |

## Which side was "right"

None of the items is a defect that Session 5 tests could have caught: every Session 5 scenario sends one request at a time, and all of them still pass against the generated build (`lab-6.2-generation/logs/`).

A1 to A3 are real weaknesses of the hand written code. They matter only when requests from different tasks meet in the same cycle. That is rare, but it is not impossible: an alarm while the operators are recovering the vehicle is exactly when it happens.

The model resolves them deliberately, and the back-to-back log shows every case where the two versions therefore differ.
