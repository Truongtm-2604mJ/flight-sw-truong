# Execution semantics of `mode_machine.yaml`

These rules are what the generator implements (`lab-6.2-generation/modelgen.py`). Each one is also a key in the model's `semantics` block; the generator refuses a model that states a value it does not implement, or leaves a key out.

## The machine and its inputs

The machine has three states: NOMINAL, DEGRADED and SAFE. It is driven by six **events** and one boolean **input**.

| Event | Source | Class | Meaning |
|---|---|---|---|
| `tc_mode_nominal`, `tc_mode_degraded`, `tc_mode_safe` | ground (TC(8,1)) | one shot | Ground commands a mode |
| `fault_noncritical` | autonomous | latched | A non-critical fault is confirmed (monitor alarm, PL_LOG escalation) |
| `fault_critical` | autonomous | latched | A critical fault is confirmed (V_BUS low, ACS or HK escalation) |
| `fault_cleared` | autonomous | one shot | A monitor returned inside its limits |

The one input is `fdir_inhibited`, true while the ground has inhibited autonomous FDIR actions (TC(19,9)).

**Privilege is a property of the event, not of a value carried with it.** Only the three `tc_*` events are ground commands, and only telecommand service 8 posts them. The application maps a mode request to an event fail closed (`app/fdir6.c`, `fdir_mode_request()`): only the GROUND cause becomes a `tc_*` event; every other cause value, including BOOT, WATCHDOG_RESET and undefined ones, becomes an autonomous fault event. `mode_sm_post()` rejects an event value outside the model.

## Answers to the five questions of A3

**1. Two enabled transitions out of one state: which wins?**

Every transition has an integer priority, unique within its source state (the generator checks this). The enabled transition with the lowest number wins. There is no other tie-breaker, and none is needed.

The priorities encode one policy:

- **A transition to SAFE comes first**, whatever its source. In NOMINAL and DEGRADED, ground SAFE has priority 1 and autonomous SAFE priority 2.
- **Then ground de-escalation or ground DEGRADED.**
- **Then autonomous DEGRADED.**
- **Then refusals (internal transitions), last.** A refusal report must never pre-empt a real mode change in the same cycle.
- **In SAFE, two conflicting ground commands are resolved conservatively:** DEGRADED (priority 1) beats NOMINAL (priority 2).

**2. Are transitions evaluated on an event, on a cycle, or both?**

On a cycle. Events can be posted at any time, from any task, under the mode lock. They are only marked pending. The machine is evaluated by `mode_sm_step()`, called once per supervisor period (2 s), in the supervisor task, after the liveness check (`app/fdir6.c`, `fdir_supervisor_cycle()`).

The step runs in the supervisor task because the supervisor is the root of the liveness chain, with the watchdog behind it. A step placed in TM_HK would stop when TM_HK hangs, which is exactly the fault (TM_HK escalation → SAFE) it must handle.

The cost is latency: a mode request takes effect up to one supervisor period after it is made. The system tests show this (divergence log, D-07).

**3. Do entry and exit actions run on a self transition?**

There are no self transitions. The generator rejects `from == to` on an external transition (`self_transition_runs_entry: false`, and nothing would satisfy it anyway). Two other constructs cover what a self transition would be used for:

- **Internal transitions** (T05, T06, T10) run their own action only, never exit or entry actions, and do not report a mode change.
- **A request for the current mode is ignored** (each state's `ignore` list), so no entry actions are re-run. For example, ground SAFE while in SAFE does not suspend the schedule a second time.

**4. When are inputs sampled, and are they stable for the whole cycle?**

At the start of the step, once (`input_sampling: start_of_cycle`). `mode_sm_step()` copies the pending event set and the inputs into local constants before evaluating anything, and nothing else reads them.

The application holds the mode lock for the whole step, so no event can be posted and the inhibit cannot change during it. An event posted while a step runs waits for the next cycle.

**5. Does the step terminate, with a bounded number of transitions?**

Yes. `mode_sm_step()` contains no recursion and one loop, bounded by the number of events (6). It takes at most one transition (`max_transitions_per_cycle: 1`); an internal transition counts as that one.

The worst case for one step is:

- one exit action;
- one transition action;
- six entry actions (entering SAFE);
- one transition report;
- up to three discard reports (one per ground command).

Each action is one application hook call.

## Event consumption: what happens to events that did not fire

With at most one transition per cycle, several pending events have to go somewhere. These rules are part of the model (`unconsumed_one_shot`, `unconsumed_latched`). They were chosen after the back-to-back tests showed what each alternative does (divergence log).

| Event | Fired this cycle | Ignored in the start state | Handled but lost to a higher priority |
|---|---|---|---|
| one shot (ground commands, `fault_cleared`) | consumed | consumed silently | consumed and **reported** (`mode_sm_on_discard`, event 0x0404) |
| latched (faults) | consumed | consumed if no transition fired this cycle; **kept** if one fired | **kept** for the next cycle |

The reasoning behind each rule:

- **Ground commands are one shot.** They were decided by operators looking at the vehicle before this cycle. A command that loses to a fault must not be replayed afterwards, so it is dropped. It is also reported, so the operators know to resend it. Replaying it would let a ground NOMINAL undo a SAFE entry caused by a critical fault in the same cycle.
- **Faults are latched.** A fault is still true after the transition that pre-empted it. Example: in DEGRADED, a ground NOMINAL and a new non-critical alarm arrive in the same cycle. NOMINAL is entered, the alarm is kept, and the next cycle goes back to DEGRADED. The hand written version loses that alarm when the requests arrive in the order alarm, then ground (D-04).
- **A latched event pending in a state that ignores it** is dropped once nothing fired, because its effect is already achieved: a fault that asks for DEGRADED while in DEGRADED or SAFE.
- **The drain is bounded.** Each cycle consumes at least one pending event or drops all ignored ones, so a burst drains within a few cycles. The worst observed is 3 cycles (b2b summary, "max drain").

**Coalescing.** Pending events are one bit each. Two posts of the same event in one cycle become one event, and the payload (cause, detail) of the last post wins. The transition report then names the last cause only. This is a stated limitation, not an accident (Section 6 of the report).

## Boot, and an invalid state

- **Boot.** `mode_sm_init()` enters a boot state, NOMINAL or SAFE, and runs its entry actions with no transition report. This matches Session 5's `mode_init()`. Any other value enters SAFE and returns -1.
- **Invalid state.** If the state variable holds a value outside the model, for example after a memory upset, the step enters SAFE (`invalid_state_recovery`), keeps latched events, drops one shot events, and reports `mode_sm_on_invalid_state` (event 0x0405). The requirement test TC-FAIL-02 exercises this path.
