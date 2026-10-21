# FDIR Design Note: Monitoring, Liveness, Safe Mode and Fault Injection

**Scope:** Session 5, Labs 5.1 to 5.4. Builds on the Session 3 task set and the Session 4 TM/TC chain. Target: RTEMS 6 on LEON3 (SIS).

**Evidence.** The FDIR logic is portable C, verified by unit tests (217 + 10 775 + 189 checks). The integrated application runs unmodified on a POSIX implementation of the RTEMS Classic API (`host-sim/`), where every result in Section 5 was measured: the Lab 5.3 scenario (51 checks), the six injections of the Lab 5.4 campaign, and a one-hour false positive run. The latencies are set by the onboard periods, not by CPU speed.

---

## 1. Failure mode table

Levels follow the session's hierarchy: Unit, Subsystem, System, Vehicle. A fault is handled at the lowest level that can handle it.

| Failure mode | Detection | Isolation | Recovery action | Level |
|---|---|---|---|---|
| Single sample glitch (EMI, ADC hiccup) | Limit violation shorter than the persistence | The monitor of that parameter | None: counted as a filtered transient in HK | Unit |
| Corrupted telecommand | CRC and 10 step validation (Session 4) | Per packet | Rejected, TM(1,2) with failure code, never executed | Unit |
| Corrupted NV record | Magic, version, CRC-16 | NV record | Ignored, cold start defaults | Unit |
| Task hung or deadlocked, once | Heartbeat counter unchanged for a whole 2 s check | Per task: each task has its own counter | Restart that task | Subsystem |
| Battery temperature out of range | T_BAT > 45.00 C or < 0.00 C for 3 samples | T_BAT monitor | DEGRADED: payload off, monitoring at 2 Hz | System |
| OBC temperature out of range | T_OBC > 70.00 C or < -20.00 C for 5 samples | T_OBC monitor | DEGRADED | System |
| Bus over voltage | V_BUS > 29.5 V for 3 samples | V_BUS monitor | DEGRADED | System |
| Payload logging task fails repeatedly | 4th miss with 3 restarts in the last 300 s | Per task | No more restarts; task marked FAILED; DEGRADED | System |
| Bus under voltage | V_BUS < 26.0 V for 3 samples | V_BUS monitor, single sensor (see Section 6) | SAFE | Vehicle |
| ACS or HK task fails repeatedly (permanent or systematic fault) | 4th miss with 3 restarts in 300 s | Per task | No more restarts; task FAILED; SAFE | Vehicle |
| Supervisor hung, or scheduling broken | Watchdog not kicked for 5 s | Only the supervisor kicks, so it is the supervisor or what it depends on | Cause written to NV, processor reset, boot into SAFE | Vehicle |
| A queued command would fire into an unhealthy vehicle | Entry into SAFE | n/a | Onboard schedule suspended; only the ground re-enables it | Vehicle |
| FDIR misfiring during a ground pass | Operators | n/a | TC(19,9) inhibits autonomous actions, bounded to 1 h | Ground |

Two cases are deliberately absent from the recovery side. A monitor returning to its limits raises an informative event but does not undo DEGRADED or SAFE, because an automatic way back is how a vehicle re-enters the condition that caused the problem. And no fault switches to redundant equipment, because this software has none to switch to (Section 6).

## 2. Monitoring parameters

All three parameters are sampled by TM_HK: at 1 Hz in NOMINAL, and at 2 Hz in DEGRADED and SAFE (the "monitoring tightened" of the mode table). A violation must last `persistence` consecutive samples to raise an alarm, and clearing needs the same number of consecutive samples inside the clear thresholds. The limit values are representative of a 28 V bus and a lithium-ion battery; on a real mission they come from the power and battery interface documents, not from me.

| Parameter | Low | Low clear | High clear | High | Persistence | Event on alarm |
|---|---:|---:|---:|---:|---:|---|
| V_BUS | 26.0 V | 26.3 V | 29.2 V | 29.5 V | 3 | TM(5,3); low → SAFE, high → DEGRADED |
| T_BAT | 0.00 C | 2.00 C | 43.00 C | 45.00 C | 3 | TM(5,2) → DEGRADED |
| T_OBC | -20.00 C | -15.00 C | 65.00 C | 70.00 C | 5 | TM(5,2) → DEGRADED |

**Why these numbers.**

- **Hysteresis is larger than the noise.** Each band (limit minus clear threshold) is wider than the parameter's peak to peak noise: V_BUS 0.3 V against 0.2 V of noise, T_BAT 2.0 C against 1.0 C, T_OBC 5.0 C against 2.0 C. A value sitting on a limit therefore cannot cross back and forth through the band.
- **Persistence 3 for V_BUS and T_BAT.** The glitches modelled (and typical of EMI) last one sample, so a persistence of 2 would already reject them; 3 adds margin for two glitches in a row. The price is 2 to 3 s of confirmation at 1 Hz. That is negligible against battery thermal time constants (minutes) and against the time a 28 V bus takes to discharge to a damaging level.
- **Persistence 5 for T_OBC.** It is the least urgent of the three and the slowest physically. A longer persistence buys more immunity at no real cost.
- **0 C and 45 C for the battery.** These are the usual lithium-ion charge limits.
- **Only under voltage goes to SAFE.** It is the one violation that threatens survival (power negative). Every other violation goes to DEGRADED: switching the payload off cuts load and dissipation, and the ground decides the rest.

The state machine of one monitor is OK → WARN (violation being counted, no event) → ALARM (event) → OK (event). WARN appears in housekeeping only, so operators can see a parameter flirting with its limit without receiving an event for it. Enable and disable work per parameter through TC(12,1) and TC(12,2), and the enable mask and the three states are in housekeeping.

## 3. Mode state machine

```
                           ground TC(8,1) SAFE  (from any mode)
          +-----------------------------------------------------------+
          |                                                           v
     +---------+   non-critical alarm    +----------+   critical alarm,    +--------+
     |         |   (T_BAT, T_OBC,        |          |   ACS/HK escalation, |        |
     | NOMINAL |-----------------------> | DEGRADED |--------------------->|  SAFE  |
     |         |    V_BUS high,          |          |   ground TC(8,1)     |        |
     |         |    PL_LOG escalation)   |          |                      |        |
     +---------+                         +----------+                      +--------+
       ^  ^  |  critical alarm (V_BUS low), ACS/HK escalation                  |
       |  |  +--------------------------------------------------------------->|
       |  |                                                                   |
       |  +----------- ground TC(8,1) NOMINAL  (the ONLY way out of SAFE) ----+
       +-------------- ground TC(8,1) NOMINAL  (the only way out of DEGRADED)

     boot after a watchdog reset ---------------------------------------> SAFE
     cold boot ---------------------------------------------------------> NOMINAL
```

| Mode | Payload | Monitoring | Housekeeping | Other |
|---|---|---|---|---|
| NOMINAL | on | 1 Hz | full report, SID 1, 1 Hz | |
| DEGRADED | suspended | 2 Hz | full report, 2 Hz | |
| SAFE | off | 2 Hz | essential report only, SID 2, 0.5 Hz | schedule suspended, sun pointing commanded, receiver never touched |

The rules live in one function (`mode_request()`):

- **Ground privilege fails closed.** Only the cause value GROUND is a privileged request. Every other value, including BOOT, WATCHDOG_RESET and any value not defined today (a future cause or a corrupted one), is treated as autonomous: it can only escalate and it is subject to the inhibit. Unit tests check this for every defined non-ground cause and for out-of-range values.
- **Autonomous requests only escalate.** They can go NOMINAL → DEGRADED or NOMINAL/DEGRADED → SAFE, never back and never sideways.
- **Only the ground leaves SAFE or DEGRADED.** There is no timeout and no automatic exit.
- **The inhibit blocks only autonomous requests.** While it is set, they are refused and an ACTION_INHIBITED event is reported; ground commands always work.
- **Every transition is reported.** A TM(5,x) event carries from, to, the cause (BOOT, GROUND, MONITOR, LIVENESS or WATCHDOG_RESET) and the detail (parameter ID or task index).
- **The cause is also in housekeeping.** Every report carries the mode and the last cause; the last cause of SAFE entry is also kept in NV.

**FDIR inhibit.** TC(19,9) takes a duration of 1 to 3600 s. Detection and reporting carry on during the inhibit; only actions (task restart, mode change) are suppressed, and each suppression is reported once. The inhibit expires on its own: an inhibit forgotten after loss of signal would otherwise leave the vehicle unprotected until the next pass. It is shown in every housekeeping report as a status bit plus the seconds remaining, and both setting and clearing it raise an event. When the inhibit is lifted, a task that is still silent is restarted at the next check. A monitor that went into alarm during the inhibit is not acted on retroactively; the operators have seen its event and its state in housekeeping.

## 4. Escalation policy

| Level | Trigger | Action | Bound |
|---|---|---|---|
| 1, Subsystem | Heartbeat counter unchanged over one 2 s check | Restart the task (`rtems_task_restart`) | 3 restarts per task in any 300 s window |
| 2, System or Vehicle | Heartbeat missed with 3 restarts already in the window | No restart; task marked FAILED and no longer checked; SAFE for ACS_CTRL and TM_HK, DEGRADED for PL_LOG; escalation recorded in NV | Once per task per boot |
| 3, Vehicle | Watchdog not kicked for 5 s | Early warning: cause written to NV; 1 s later, processor reset; boot into SAFE | Hardware |

**Why these numbers.**

- **2 s check period.** It is longer than the slowest supervised task (1 s), as the session requires, and it is twice that period, so one late job of PL_LOG cannot cause a false miss. `sup_register()` refuses a task whose period is not shorter than the check period.
- **3 restarts in 300 s.** One restart handles a genuine transient. A fault that comes back three times within five minutes is not transient, and restarting it again is a loop. 300 s is long compared with the 2 s check, so a fault recurring every few seconds is caught; it is also short enough that two unrelated upsets hours apart do not add up to an escalation. The window is computed with wrap-safe unsigned arithmetic.
- **5 s watchdog timeout.** That is 2.5 supervisor periods, so one late supervisor cycle is tolerated and two are not.
- **1 s grace after the warning.** It covers the NV write and gives TM_HK, which runs every 500 ms, a chance to downlink the warning event.
- **Once warned, the reset is committed.** A late kick does not cancel it.

**Heartbeats are counters, not flags.** The session describes flags that the supervisor clears. With a flag, both the task (set) and the supervisor (clear) write the same word, and a heartbeat landing between the supervisor's read and its clear is lost, which is a false miss. With a counter, the task increments its own word and the supervisor compares against its own copy, so every word has one writer. The behaviour is the same and there is no race.

**Who supervises the supervisor.** The chain is: tasks ← supervisor task (priority 5, above every supervised task, so a runaway lower-priority task cannot starve it) ← watchdog ← reset hardware. It terminates in the watchdog because the watchdog does not depend on any task being scheduled, has no software path that stops it, and cannot be talked out of a reset once it has warned. On a real LEON3 board that endpoint is the GPTIMER watchdog output or an external watchdog driving the reset line, which is trustworthy because it is outside the processor that failed. In this lab it is a timer service routine in clock tick interrupt context. That routine is independent of task scheduling but not of the clock interrupt. Section 6 is explicit about that gap.

**Concurrency.** Telecommands arrive from TC_RX and from the schedule, and mode requests arrive from three tasks, so the mode manager, the telecommand handler and event reporting are each under a mutex with priority inheritance (the Session 3 lesson). The lock order is fixed: tc lock, then mode lock, then event lock. Every critical section is short (an event is about one packet build and a queue send, about 100 µs on SIS judging by Session 4's HK_EXEC). The watchdog routine never takes a lock: it touches only its own counter and the NV record, with interrupts disabled. The supervisor's work is a few comparisons per task every 2 s, so the Session 4 utilisation of 0.381 is essentially unchanged; `make rtems-timing` builds the run that reports the rate monotonic statistics.

## 5. Fault injection campaign

The injection harness (`lab-5.4-fault-injection/finj.c`, TC service 200) exists only in the test build. In the flight build the injection points compile to nothing. `make check-flight` provides two pieces of evidence. It checks statically that no `finj_` symbol is present in the flight executable. It then runs the flight build and sends every service 200 subtype (`ground/check_flight.py`, 11 checks): each is answered with TM(1,2) code 7, "service not implemented", and nothing else, while a TC(17,1) before and after shows the command handler is alive, and housekeeping shows no effect (log: `logs/host-sim-check-flight.log`).

Each row runs on a freshly started spacecraft. Latency is onboard time from the injection command's completion report TM(1,7) to the detecting event.

**Results** (`lab-5.4-fault-injection/results/campaign-host-sim.md`, log in `logs/host-sim-campaign.log`):

| Injected fault | Expected detection | Detected | Latency | Correct isolation | Recovery result |
|---|---|---|---|---|---|
| T_BAT held at 48 C | Monitor alarm, event raised | yes | 2999 ms (alarm on the 3rd consecutive sample) | yes, T_BAT only | DEGRADED, payload off, no repeat event |
| One sample of T_BAT at 48 C | No event, filtered by persistence | filtered, no event | n/a | counted on T_BAT | none, NOMINAL |
| PL_LOG stops its heartbeat once | Heartbeat miss, task restart | yes | 3999 ms | yes, PL_LOG only | restarted, heartbeat back, NOMINAL |
| ACS_CTRL fails five times | Escalation to SAFE after bounded retries | yes | first miss 4000 ms, escalation 10 000 ms | yes, ACS_CTRL only | 3 restarts, then SAFE |
| Corrupted TC checksum | Rejection plus failure report, no execution | yes | 0.5 ms ground round trip | code 3 CHECKSUM | rejected, not executed |
| Supervisor hangs | Watchdog reset, cause recorded | yes | early warning 5499 ms, reset about 1 s later | watchdog, not a task | reset; NV read back at boot (reset 1, cause WATCHDOG); boot in SAFE |

**Latencies against their analytic bounds.**

| Injection | Analytic bound | Measured | How the bound arises |
|---|---|---|---|
| Sustained limit violation | 2 to 3 s | 3.0 s | Next 1 Hz sample within 1 s, alarm on the 3rd sample (1 to 1.5 s in DEGRADED or SAFE) |
| Heartbeat loss | 2 to 4 s, plus up to one task period | 4.0 s | Detection needs a whole silent 2 s check interval |
| Repeated task failure | first miss + 3 × 2 s ≤ 10 s | 10.0 s | One restart per check, three restarts before escalation |
| Supervisor hang | 3 to 5 s, plus the 0.5 s HK cycle that reports the warning | 5.5 s | The hang starts at the supervisor's next cycle, up to 2 s after its last kick; the warning fires 5 s after that kick |

**False positive measurement.** I ran the healthy vehicle with no injection. Its sensor model produces natural single sample glitches: V_BUS drops to 25.5 V every 137th sample and T_BAT jumps to 46 C every 211th, each below or above an alarm limit. A monitoring system that alarms on those is crying wolf.

| Run | Onboard time | Detections | Glitches filtered | Final state |
|---|---:|---:|---:|---|
| Host simulation, real time | 300 s | **0** | 3 of 3 | NOMINAL, 0 restarts |
| Host simulation, ×10 clock | 3600 s | **0** | 43 of 43 | NOMINAL, 0 restarts |

The Lab 5.3 scenario (`gs_modes.py`, 51 checks) passes on the host simulation. It verifies that:

- a scheduled command does not fire in SAFE, and fires only after the ground both exits SAFE and re-enables the schedule;
- SAFE has no timeout;
- under inhibit, a limit violation is detected but the mode change is refused, and a hung task is detected but not restarted until the inhibit is lifted;
- the inhibit expires on its own;
- a disabled monitor stays silent at 48 C and detects again once enabled;
- DEGRADED does not return to NOMINAL by itself;
- five malformed FDIR commands are rejected with code 9.

## 6. Known limitations and what I did not implement

**About the evidence**

- **Measured on the host simulation.** Tasks are threads with no priorities there, so preemption and priority inheritance are not exercised, and a task restart takes effect at the task's next blocking call instead of immediately. The RTEMS build and the SIS runs follow in a later commit.
- **The one-hour false positive run** used a ×10 onboard clock.
- **Persistence across a reset has been shown on the host only**, where a file stands in for the non volatile memory. On RTEMS the NV record is a RAM array, and on SIS a reset ends the simulation, so the record cannot survive it there.

**About the design**

- **The watchdog is a software model.** It is a timer service routine in clock tick interrupt context, not the LEON3 GPTIMER watchdog or an external one. A fault that stops the clock interrupt, or leaves interrupts disabled, defeats it; real hardware would not share that dependency.
- **Watchdog resets are counted but not bounded.** A reset loop would repeat. A real design escalates after N resets, typically to the redundant computer, and there is none here.
- **No redundancy anywhere:** one computer, one sensor per parameter. Isolation of under voltage is therefore one level deep: a failing V_BUS sensor and a failing battery look identical, and both send the vehicle to SAFE. That is deliberately conservative, and it is a false positive path a consistency check against a second sensor or the battery current would close. No plausibility checks are implemented, only limit checks.
- **Restarting a task that holds a mutex would deadlock the system.** The injection points sit outside every critical section, but a real fault inside one would not be so polite. Robust mutexes, or restarting only at safe points, would be needed.
- **TC_RX, TM_TX and SCHED are not supervised.** They are aperiodic or support tasks. A hung command path would be found only by the ground, and there is no hardware decoded "high priority command" path that bypasses the software.
- **A FAILED task stays failed until reboot.** No telecommand re-arms its supervision.
- **The watchdog warning event comes from TM_HK**, because the interrupt routine cannot send telemetry under a lock. It is delayed by up to 500 ms, and if TM_HK is dead too the event is lost. The NV record written by the routine is not.
- **No memory scrubbing or EDAC handling**, although the session lists them as detection mechanisms.

**About the standards**

- **The PUS services are the shape, not the standard.**
  - Service 12 has only per-parameter enable and disable, with fixed definitions and no check transition reports.
  - Service 19 is a single global inhibit, not event-action definitions.
  - Service 11 inserts one activity per command, and overdue activities are released when the schedule is re-enabled.
  - TC(8,1) function IDs are mission defined.
  - The subtype numbers follow open-source PUS-C implementations and still need checking against ECSS-E-ST-70-41C.
- **The limit values are representative, not from interface documents.** The sensor model is simulated.
