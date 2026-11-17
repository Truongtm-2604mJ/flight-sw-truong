# Oral defence: prepared answers

Short answers to say aloud (about 45 to 60 seconds each), with where to point on screen.

## 1. Why did you assign that task priority rather than a different one?

The periodic tasks follow rate monotonic order, because it is the optimal fixed-priority assignment
for implicit deadlines:

- ACS_CTRL, 100 ms, priority 10;
- TM_HK, 500 ms, priority 20;
- PL_LOG, 1 s, priority 30.

Two tasks are deliberately out of that order.

- **SUP is at priority 5, the highest, although its period is the longest (2 s).** It exists to detect
  tasks that stop running. If a busy higher-priority task could delay it, it would go blind exactly when
  it is needed. It costs about 5 ms every 2 s, so it takes almost nothing from anyone.
- **TC_RX is at 40, below every periodic task except SCHED.** A flood of uplink frames, forged or real,
  then only uses slack time. It can never delay the control loop or housekeeping. The cost is command
  latency, which is irrelevant at 4 kbit/s. SCHED (45) sits below TC_RX so ground commands go first.

*Point at:* the task table in `docs/architecture.md` Section 1, `src/app/mission.h` lines 89 to 95.

## 2. Your utilisation is 0.39. What happens at 1.3 times the load?

U becomes 0.51, still under the Liu and Layland bound of 0.74 for five tasks, so it is schedulable
without even needing the exact test. PL_LOG's response time goes from about 193 ms to about 274 ms
against a 1000 ms deadline.

If the load keeps rising, the failure is ordered: PL_LOG misses first, then SCHED. ACS_CTRL can only be
delayed by SUP. A miss shows up as a missed heartbeat, and the supervisor restarts the task. After three
restarts it escalates PL_LOG to DEGRADED, which sheds the payload: the system degrades the way the
priorities say it should.

The numbers are an estimate for SUP and SCHED (design allowances). The SIS run (`make sis`) replaces
them with measurements.

## 3. Show me where a telecommand is validated, and why the checks are in that order.

`src/app/tmtc.c` (TC_RX) → `tc_handle` in `src/core/tc_handler.c` → the security hook
(`src/security/tc_auth.c`) → the service check in `src/app/services.c`.

The order is structure → CRC → MAC → freshness → semantic check → commit the counter → execute.

- **Cheapest and most general first.** A packet whose length or CRC is wrong cannot be trusted for
  anything, and noise is far more common than attack. The CRC is cheap: the whole plain TC path is about 0.55 ms on LEON3, the MAC alone about 1.44 ms.
- **MAC before freshness.** An unauthenticated counter must never be compared with, or update, the
  stored counter. Otherwise forged packets with high counters could lock out the real ground.
- **Semantic check before commit.** A command the service refuses must not burn its counter value.
- **No TM(1,2) before authentication passes.** A forger learns nothing per packet. The operator still
  sees the rejections per class in SID 3, and rate-limited events.

## 4. How do you know your monitoring does not produce false alarms?

- **By design:** persistence (3 consecutive samples for V_BUS and T_BAT, 5 for T_OBC) and hysteresis
  (an alarm clears only 2 °C or 0.3 V inside the limit).
- **By test:**
  - unit tests show a single out-of-limit sample raises nothing;
  - VT-FDIR-010 does the same through the ground (with the filtered count in HK going +1);
  - VT-FDIR-070 runs 300 s of nominal operation with sensor noise and asserts zero events, restarts or
    mode changes.

What I cannot claim: the noise model is mine, not measured sensor data. On a real vehicle the
persistence values would be tuned against flight-representative noise and transients.

## 5. Why does SAFE mode not exit automatically?

SAFE is entered because something happened that the software could not fix. The same software would
have to decide that the cause is gone, using the thin telemetry available in SAFE. A temperature back
inside its limits tells you nothing about why it left them.

A vehicle that cycles NOMINAL → SAFE → NOMINAL unnoticed between passes is worse than one waiting in
SAFE. SAFE is stable, power positive and sun pointing, so waiting costs only mission time.

It is requirement MM-010, and the model has no autonomous transition out of SAFE or DEGRADED. The exit
is procedure SAFE-01: assess, fix, authenticated command, confirm.

## 6. Where does your liveness supervision chain terminate, and why is that endpoint trustworthy?

The chain is: tasks → supervisor (SUP, highest priority, checks heartbeats every 2 s) → watchdog. SUP
kicks the watchdog only after its own check has run. If SUP stops, the watchdog warns at 5 s and resets
the processor. The boot cause is kept in NV memory, so the vehicle boots into SAFE and reports why
(FDIR-060: warning at 5.5 s, measured).

The endpoint is trustworthy because it shares nothing with what it watches except the clock: no task,
no lock, no queue. On a flight OBC it is an independent hardware counter that software can restart but
not stop. Honest limit: on SIS it is a timer service routine on the same clock interrupt as RTEMS, so it
models the hardware, it is not independent of it.

## 7. If the onboard key is corrupted, how does the operator recover the vehicle?

Keys are stored three times, voted on read and scrubbed once a second.

- **One bad copy:** repaired automatically, with event KEY_REPAIRED.
- **Two bad copies:**
  1. The slot goes CORRUPT and commands fail with AUTH. The operator sees this in SID 3 and in the
     KEY_CORRUPT event.
  2. The operator reads the one-time challenge from SID 3 and sends RECOVERY_ACTIVATE under the recovery
     key (slot 0, in ROM). This switches to the spare slot, which holds the previous key.
  3. Normal commanding resumes.
- **Counter corrupted high** (everything looks like a replay): RECOVERY_SET_COUNTER sets a floor above
  every value the ground has ever used.

The recovery key can do only these two things. It cannot change mode, load keys or command the payload.
It needs a fresh challenge, so a recorded recovery command is useless, and every use is reported. This
is contingency C-SEC in `mcs/procedures/recover-from-safe.md`.

## 8. Your coverage is X percent. What is in the uncovered remainder?

Every uncovered flight line is classified in `evidence/coverage/justifications.yaml`. The report fails
on any unclassified line. The remainder is three kinds:

- **Defensive code** for failures the test environment cannot produce. Examples: RTEMS object creation
  failing, a console write error, a full TM queue, a corrupted value reaching the mode machine's default
  branch.
- **Code not in the flight build**: the fault injection service, and the name tables of the mode machine.
- **Boot failure paths whose inputs are compile-time constants.**

Each entry has an argument. Where a line was genuinely untested, I wrote a test instead of a
justification (C-01 to C-04 in Session 7). MC/DC on the FDIR guards: 9 of 10 mutants killed. The
survivor is an equivalent mutant, and I can show why.

## 9. What did integration break that unit testing did not catch?

Five things, all from two correct parts meeting:

- **The thermal recovery re-triggered** (NCR-03, major, fixed). EXHAUSTED requests SAFE, but SAFE is
  applied at the next 2 s supervisor cycle. In between, the HK cycle saw "DEGRADED with an alarm" and
  started a second sequence. The fix is a "spent" latch.
- **The first contact consumed the boot event** the first validation test needed (TA-04).
- **The security HK** is a second (3,25) stream, which the campaign decoded as FDIR HK (TA-05).
- **A test order dependency through SAFE** suspending the schedule (TA-06).
- **NCR-01, still deferred.** The Session 4 contract "TM(1,7) when exec returns" met the Session 6
  cyclic mode machine, so a mode command reports completion up to 2 s before the mode changes. It is
  open on purpose, with an MCS workaround.

## 10. What is the weakest part of this system?

**Non volatile state across resets.** Two of the most important properties depend on it:

- the anti-replay counter;
- the boot cause that sends the vehicle into SAFE after a watchdog reset.

Neither has a real implementation on the target: a file on the host, RAM on SIS (NCR-04, open). The
design is sound on paper (three copies, voting, CRC, an untrusted state after loss that forces a
recovery command), but it has never been run against real flash with its write times, wear, and partial
writes on power loss. It is the first thing I would build and test on real hardware.

Close second: the timing evidence comes from a simulator without caches or bus contention. The margins
are large, but they are not measured on a board.
