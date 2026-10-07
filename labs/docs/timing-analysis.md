# Timing Analysis: Rate Monotonic Scheduling and Priority Inversion

**Target:** RTEMS 6 on LEON3 (SPARC), executed on the SIS simulator 2.30 (`sis -leon3`, 1 CPU).
**Note on environment:** the labs were written for QEMU. All numbers below come from SIS, so absolute values are simulator values and should not be read as flight-hardware timings. The relative behaviour (ordering, and how blocking time scales) is the evidence.
**Sources:** Lab 3.2 (`lab3_2_rms.c`, rate-monotonic task set) and Lab 3.3 (`lab33_priority_inversion.c`). Clock tick is 10 ms (`CONFIGURE_MICROSECONDS_PER_TICK 10000`).

---

## 1. Task set, utilisation and the Rate Monotonic bound

Lab 3.2 task set (deadline = period). C is the maximum CPU time per period reported by `rtems_rate_monotonic_report_statistics()` over a 10 s run, after calibrating the workload loops (Section 3):

| Task | Period T (ms) | Target C (ms) | Measured max C (ms) | Utilisation U = C/T |
|------|--------------:|--------------:|--------------------:|--------------------:|
| ACS_CTRL | 100  | 20  | 19.812 | 0.1981 |
| TM_HK    | 500  | 50  | 49.782 | 0.0996 |
| PL_LOG   | 1000 | 100 | 99.979 | 0.1000 |
| **Total** | | | | **0.3977** |

**Liu & Layland bound for n = 3:**

U ≤ n(2^(1/n) − 1) = 3(2^(1/3) − 1) ≈ **0.7798**

**Result:** 0.398 < 0.7798, so the set is guaranteed schedulable under Rate Monotonic priorities (the bound is sufficient, not necessary). The margin is about 0.38.

**Exact check (response-time analysis, measured C).** R_i = C_i + Σ ceil(R_i / T_j) · C_j over higher-priority tasks j, assuming all tasks are released together (the critical instant, the worst case):

| Task | Response time R | Deadline | Met? |
|------|----------------:|---------:|------|
| ACS_CTRL | 19.8 ms | 100 ms | yes |
| TM_HK    | 49.8 + 19.8 = 69.6 ms | 500 ms | yes |
| PL_LOG   | 100.0 + 2·19.8 + 1·49.8 = 189.4 ms | 1000 ms | yes |

Workload loop iteration counts used in `do_bounded_work()`: ACS_CTRL 43 000, TM_HK 108 000, PL_LOG 217 000. These are iteration counts, not milliseconds, and are specific to SIS (about 2 170 iterations per ms).

## 2. Priority assignment and justification

RTEMS priorities: a *smaller* number is a *higher* priority. Rate Monotonic gives higher priority to shorter periods.

| Task | RTEMS priority | Justification |
|------|---------------:|---------------|
| ACS_CTRL | 10 | Shortest period (100 ms), so RM gives it the highest priority. It is also the attitude control loop, whose deadline is the most critical. |
| TM_HK    | 20 | Middle period (500 ms), so middle priority. |
| PL_LOG   | 30 | Longest period (1000 ms), so lowest priority. Late payload logging is the cheapest failure. |

`Init` runs at priority 1, above all tasks, so that task start-up is deterministic.

The source defines `ACS_CTRL_PRIORITY 10`, `TM_HK_PRIORITY 20` and `PL_LOG_PRIORITY 30`, which is the Rate Monotonic order. The measured wall times in Section 3 agree with it: ACS_CTRL's wall time equals its CPU time, while TM_HK and PL_LOG are each delayed by about one ACS_CTRL execution (about 20 ms).

## 3. Measured results and deadline misses

### Initial run with uncalibrated workload (not a valid nominal case)

The first run used workload loops that had not been calibrated. ACS_CTRL missed 20 of 20 periods, with a minimum CPU time of 0.46 s per period against a 100 ms period (about 23 times the intended 20 ms). TM_HK and PL_LOG never ran, because ACS_CTRL, as the highest-priority task, kept the CPU. Lesson: the utilisation check is only meaningful once C has been measured, not assumed. I then calibrated the loops from the measured CPU times.

### Case 1: nominal Rate Monotonic priorities (10 s run)

| Task | Periods | Missed | CPU time min / max (ms) | Wall time min / max (ms) |
|------|--------:|-------:|------------------------:|-------------------------:|
| ACS_CTRL | 100 | 0 | 19.808 / 19.812 | 19.812 / 19.816 |
| TM_HK    | 20  | 0 | 49.776 / 49.782 | 69.599 / 69.603 |
| PL_LOG   | 10  | 0 | 99.974 / 99.979 | 119.796 / 119.798 |

Observations:
- **No deadline misses** in 130 periods, as the bound predicts.
- CPU times are within about 1% of the 20 / 50 / 100 ms targets and very stable (spread under 0.01 ms).
- TM_HK wall time (69.6 ms) equals the predicted response time (69.6 ms): it runs 49.8 ms and is preempted once by ACS_CTRL (19.8 ms).
- PL_LOG wall time (119.8 ms) is well below the analytic worst case (189.4 ms). The likely reason is phasing: each task starts its first period only after its first workload, so the tasks are not released together and the run never reaches the critical instant. The analytic value is therefore the guaranteed bound, and the measurement is one observed schedule inside it.

## 4. Priority inversion: Stage 1 vs. Stage 2 (Lab 3.3)

Lab 3.3 uses its own three-task experiment (priorities HIGH = 10, MED = 50, LOW = 100) that runs once, not periodically.

### Method (deterministic, no delays)

Execution order is forced with signalling semaphores (`RTEMS_SIMPLE_BINARY_SEMAPHORE`, initial count 0), not with sleeps:

1. HIGH and MED start first and block on their signal semaphores (count 0).
2. LOW obtains the real mutex, then signals `low_has_mutex`.
3. HIGH wakes, preempts LOW, signals `high_attempting`, records its request time and requests the mutex. LOW owns it, so HIGH blocks.
4. MED wakes and starts CPU-bound work.

The real mutex is `RTEMS_BINARY_SEMAPHORE | RTEMS_PRIORITY`. Stage 2 adds `RTEMS_INHERIT_PRIORITY`. No `printf` is called inside any task; timestamps (`rtems_clock_get_uptime_nanoseconds()`) are stored and printed afterwards by `Init`.

**Metric:** HIGH blocking time = `high_acquire` − `high_request`.

### Test procedure

1. Set `USE_PRIORITY_INHERIT` (0 or 1) and `MED_MULTIPLIER` (1, 2 or 4) at the top of the source.
2. Clean build, run on SIS, save the full log. Check the banner shows the intended Stage and MED multiplier (otherwise the old binary ran).
3. Sanity check: in Stage 1 `MED start` must be earlier than `LOW release`; in Stage 2 it must be later than `HIGH acquire`.
4. Record `LOW critical section`, `MED work` and `HIGH blocking time`.
5. Repeat for the six configurations (Stage 1 and 2, each with x1, x2, x4).

### Measured: Stage 2 (priority inheritance), MED x1

| Event | Time (µs, relative to LOW acquire) |
|-------|-----------------------------------:|
| LOW acquire | 0 |
| HIGH request | 34 |
| LOW release | 116 186 |
| HIGH acquire | 116 218 |
| MED start | 116 297 |
| MED end | 174 364 |

| Duration | µs |
|----------|---:|
| LOW critical section | 116 186 |
| MED work | 58 066 |
| **HIGH blocking time** | **116 184** |

Observations:
- MED started *after* HIGH acquired the mutex (116 297 > 116 218), so MED did not delay LOW.
- HIGH blocking (116 184 µs) is almost identical to LOW's critical section (116 186 µs). HIGH waited only for LOW's own work.

### Comparison across MED workloads

Interference = HIGH blocking minus LOW's own critical-section work (116 186 µs, taken from the Stage 2 run, where LOW is never delayed by MED).

| MED workload | MED work (µs) | Stage 1 HIGH blocking (µs) | Stage 1 interference (µs) | Stage 2 HIGH blocking (µs) | Stage 2 interference (µs) |
|--------------|--------------:|---------------------------:|--------------------------:|---------------------------:|--------------------------:|
| x1 | 58066 | 174308 | 58056 | 116184 | ≈ 0 |
| x2 | 116120 | 232373 | 116120 | 116184 | 0 |
| x4 | 232248 | 348490 | 232248 | 116184 | 0 |

The expectations come from the model:

- Without inheritance: HIGH blocking ≈ LOW remaining critical section + MED interference, so it grows with MED's workload.
- With inheritance: HIGH blocking ≈ LOW remaining critical section, so it stays flat as MED grows.

## 5. What I would do differently on a real mission

**Priority inheritance is a mitigation, not a solution.** In Stage 2 it did what it promises: once HIGH blocked, LOW ran at HIGH's priority, MED could not preempt it, and HIGH's blocking time stayed at about 116 ms regardless of MED's workload. But look at what is left. HIGH still waited for LOW's entire critical section. If that section is longer than the high-priority task's period, the deadline is missed even though the inversion is "fixed". (The 116 ms here is a length I chose for the experiment, so the comparison with the 100 ms ACS period in Lab 3.2 is only an illustration of scale.) Inheritance bounds the damage to one critical section; it does not remove the dependency of a high-priority task on a low-priority one. It also does not help with chained blocking across several mutexes, and it does not prevent deadlock. The architecture is still coupled.

**For the ACS control loop I would redesign the resource sharing rather than rely on inheritance.** The attitude control loop is the task with the tightest deadline, so it should never wait on housekeeping or payload code. In order of preference:

1. Take the lock off the control loop's path: let the low-priority task publish data through a message queue or a snapshot that the control loop reads without blocking.
2. Keep any remaining critical section very short and bounded, and measure its worst case on the target rather than assuming it.
3. If a lock is unavoidable, use a priority ceiling (`RTEMS_PRIORITY_CEILING`), which bounds blocking to one critical section and prevents chained blocking and deadlock.
4. Include the blocking time in the schedulability analysis. The Liu-Layland check in Section 1 assumes no blocking; the real condition adds a term B_i/T_i for each task. The ACS loop passed the check with a wide margin in Case 1, but that result says nothing about a design that blocks it.

**What I learned about the evidence itself.** My first Lab 3.2 run was invalid because I trusted the nominal 20 ms instead of measuring it; the real value was over twenty times larger and the utilisation check meant nothing until C was measured. The same applies to everything here: these results come from SIS, one run per configuration, so on a real mission I would repeat them on the target hardware, take WCET with margin from many runs, and keep these tests as regression checks so that a later change to priorities, critical-section length or workload cannot silently break the timing budget.