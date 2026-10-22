# Session 6: Model-based development of the vehicle mode manager

The Session 5 mode manager, `session-05/lab-5.3-safe-mode/modes.c`, is hand written: 114 lines that act on each request as it arrives. In this session that logic was:

- written as an explicit model;
- turned into C by a generator;
- compared against the hand written version, cycle by cycle;
- traced from requirements down to generated lines and tests;
- flown inside the Session 5 application in place of `modes.c`.

| Lab | Deliverable |
|---|---|
| 6.1 | `lab-6.1-model/`: model, semantics statement, ambiguities |
| 6.2 | `lab-6.2-generation/`: generator, generated module, integration |
| 6.3 | `lab-6.3-equivalence/`: harness, sequences, divergence log, coverage |
| 6.4 | `lab-6.4-traceability/`: requirements and matrix |

## 1. Path taken, and why

**Path B: a YAML model and a generator I wrote** (`modelgen.py`, about 780 lines of Python), producing C99.

- Path A (Simulink and Embedded Coder, or SCADE) was not possible: no licence is available.
- More importantly, Path B makes the hidden part of commercial MBD visible. With Path A the semantics are the tool's, and they are documented somewhere I would have to trust. With Path B I had to decide each rule myself and implement it, and the generator refuses a model that does not state every rule (`semantics:` block).

That cost time, but it is the point of the exercise. Every question in Section 2 had to be answered in writing before a line of C existed.

The generator does more than translate. Before writing anything it checks that:

- every (state, event) pair is handled or explicitly ignored (18 pairs: 9 handled, 9 ignored, 0 unhandled; `lab-6.1-model/completeness.md`);
- guards on the same event are exhaustive over all inputs;
- priorities are unique within each state;
- every state is reachable from a boot state.

Eleven tests show that it rejects a model broken in each of these ways (`lab-6.2-generation/tests/results.txt`).

## 2. Semantics and ambiguities

The full statement is `lab-6.1-model/semantics.md`. In short:

- **Evaluation.** The machine is evaluated once per supervisor period (2 s), in the supervisor task, after the liveness check. Inputs are sampled once at the start of the step.
- **Transitions.** At most one transition fires per step. When several are enabled, the unique priority within the state decides.
- **No self transitions.** Internal transitions and explicit ignore lists cover what a self transition would be used for.
- **Event consumption.** Ground commands are one shot: consumed every cycle, and reported when they lose to a higher priority. Faults are latched: kept for the next cycle if another transition fired.
- **Invalid state.** A state variable outside the model recovers to SAFE.

Writing the model surfaced ten ambiguities in the hand written code (`lab-6.1-model/ambiguities.md`). The important ones are all about requests that meet in the same cycle:

- **A1, order decides.** The outcome of two requests in one cycle depends on which task called first.
- **A2, a ground command can undo safing.** A ground NOMINAL that crosses a critical fault in flight undoes the SAFE entry, if the ground command happens to be applied last.
- **A3, a fault can be forgotten.** A fault that arrives while the mode already matches it is dropped, so a following ground de-escalation leaves the vehicle in NOMINAL with an alarm still active.

Two other items were found only by writing outputs as actions. Hand written code recomputes every output from the mode, so some behaviours are implicit:

- **A5:** sun pointing has to be released on SAFE exit.
- **A6:** the monitoring rate in SAFE is set in a different file, `fdir_hk_cycle()`. I missed this one in model version 1 (see D-01).

## 3. Model to code mapping

Every model element produces a fixed, named construct (table in `lab-6.2-generation/README.md`):

| Model element | Generated construct |
|---|---|
| state | an enum value and `enter_<STATE>()` |
| on_exit | `exit_SAFE()`, emitted only where exit actions exist |
| transition `Txx` | `fire_Txx()`, with a comment repeating the model line and its requirement IDs |
| priority and guard | one `if / else if` arm in `select_<STATE>()`, in priority order |
| ignore list | the `IGNORED[]` mask |
| consumption rules | the block at the end of `mode_sm_step()` |
| invalid state recovery | the `default:` case |

**Policy stays out of the generated code.** Every action is an extern hook, implemented in `app/fdir6.c`.

`mode_sm_trace.json` records the line range of each element, so the traceability matrix gives line numbers rather than file names.

**Static checks.**

- The first generated file failed `-Wswitch-enum` and gave 36 clang-tidy findings: lower case literal suffixes, a missing include, and a cognitive complexity of 30.
- Each was fixed in the generator, never in the output. The output now has 0 warnings with the strict GCC set and 0 clang-tidy findings.
- A banner carries the SHA-256 of the model and of the generator, and `make check-generated` fails on any hand edit.

**Integration.** The generated module replaces `modes.c` in the Session 5 application; `make check-no-hand` confirms `modes.c` is not linked. The unchanged Session 5 system scenarios give the following results:

| Scenario | Result |
|---|---|
| `check_flight.py` | 11/11 |
| fault campaign | six of six detected and recovered, and 0 false positives in 300 s |
| `gs_modes.py` | 49/51 |

The two `gs_modes` failures are D-07, below. These runs are on the Session 5 host simulation of the RTEMS API. The same sources build for RTEMS 6 / LEON3 with `make rtems`; the SIS runs follow in a later commit.

## 4. Equivalence results

The harness `b2b.c` links both implementations and drives them with the same sequences:

- every state × event × inhibit combination;
- every ordered pair and triple of events;
- two 5000-cycle seeded soaks.

In total that is 10 726 cycles and 12 752 events. Each cycle it compares the mode, five outputs and the ordered report list (`divergence-log.md`).

| Run | Single-event cycles diverging | Multi-event cycles diverging |
|---|---|---|
| 1, model v1 | 502 of 7996 | 1140 of 2730, 223 unclassified |
| 2, model v2 | **0** of 7996 | 998 of 2730, 0 unclassified |

**With one event per cycle the two are identical.** All remaining divergences need two or more requests in one cycle, where the hand written code has no defined behaviour.

| ID | Divergence | Count | Side at fault |
|---|---|---:|---|
| D-01 | SAFE entered with 1 Hz monitoring | 502 + 223 (run 1) | **The model**: v1 was incomplete. Fixed in v2. |
| D-02 | Hand version passes through an intermediate mode | 236 | Neither: same end state and outputs. |
| D-03 | Ground command undoes a same-cycle safing | 105 | **The hand written code**: a genuine weakness (A2). |
| D-04 | Fault lost to arrival order | 158 | **The hand written code**: a genuine defect (A3). |
| D-05 | Two conflicting ground commands | 365 | A specification choice: last wins vs priority wins. The losing command is still dropped silently in one case; recorded as an open item. |
| D-06 | Same end state, cause reported differently | 134 | Neither. |
| D-07 | Mode change up to 2 s after the TC | 2 of 51 system checks | Neither: the cost of cyclic evaluation. The two checks encode the hand version's immediate effect. |

**No divergence was a defect of the generated code with respect to its model.** The one real modelling error, D-01, was found by the harness and not by me. That is the strongest argument for back-to-back testing: I had read `modes.c` carefully, and the defect was in a file I had not thought to read.

## 5. Coverage

gcov over the whole Lab 6.3 suite gives the following results for the flight part of `mode_sm.c`:

- **statements:** 231/231 (100 %);
- **branches:** 52/55 (94.5 %).

The three branches not taken are the false outcome of the guard term in T05, T06 and T10, the inhibited refusals. Each is unreachable by construction:

1. Each of those transitions is tested only after a higher priority transition on the same event, with the opposite guard (`!fdir_inhibited`), has been found not enabled.
2. The inputs are a constant copy taken at the start of the step, so they cannot change between the two evaluations.
3. So `fdir_inhibited` is necessarily true when these guards are evaluated.

The guards are kept on purpose: each condition then reads exactly as in the model, and it stays correct if priorities are ever reordered. `coverage_report.py` derives this argument from the model and checks it, rather than it being written by hand. That is also the line between code that is covered, code that is unreachable and argued, and code that is untested (0 items).

The test-only name strings (`MODE_SM_WITH_NAMES`) are reported separately and are not in flight builds.

## 6. Was MBD worth it?

**Yes, for the specification and the verification. Not for the code generation, at this size.**

- **The generator cost more than the code it replaces.** It is about 780 lines of Python, plus tests, to produce about 390 lines of C that replace 114 hand written lines. For one three-state machine, writing the C by hand is faster, and a reviewer can read `modes.c` in ten minutes. If the generator were the only gain, this would be a loss.
- **The model's value was in what it forced into the open.**
  - The question "what happens when two requests meet in one cycle?" has no place to hide in a model that requires a priority on every transition and a consumption rule for every event class. Session 5 never asked it.
  - D-03 and D-04 are real weaknesses that every Session 5 test missed, because every Session 5 test sends one request at a time. The completeness check made "forgot to handle event X in state Y" impossible.
  - The back-to-back harness, which only exists because there were two implementations to compare, found my own modelling error within one run.
- **The trace is cheaper than it looks.** Because model elements carry requirement tags and the generator emits line ranges, the matrix is built by a script and cannot drift. Done by hand, it would be out of date after the first edit.
- **The costs are real.**
  - The stated semantics added 2 s of latency to ground mode commands (D-07). That is a behavioural change the operators would notice.
  - Coalescing loses the cause of the second of two identical faults.
  - The generator is now flight-critical tooling. On a real project it would need its own qualification, or the output would need to be verified as if hand written, which is what Lab 6.3 did.

**Position.** For a mode manager this small, I would keep the model and the back-to-back harness as the specification and the test oracle, and I would accept either generated or hand written code checked against it. The model earned its place by finding three defects (D-01, D-03, D-04) and by turning ten implicit behaviours into stated ones. The code generation itself earns its place only when the machine grows, or when the model changes often enough that keeping hand code and specification in step becomes the dominant cost.

### Reversal: requirements derived from code

The requirements in Lab 6.4 were written after the code, which inverts real practice. The risk is that a requirement phrased as "what the code does" is satisfied by construction, so the matrix confirms the code instead of checking it.

The mitigations used here:

- requirements written from the Session 5 design rationale, with a source for each;
- requirements added where the model and the harness exposed undefined behaviour: MM-110 (determinism) and MM-130 (faults are not lost).

The full discussion is in `lab-6.4-traceability/README.md`.
