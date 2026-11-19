# Session 8: Security in flight software and ground integration

The Session 6 flight software (Session 5 tasks, generated mode manager) now accepts only authenticated telecommands, manages its own keys, and is operated from Yamcs through a ground security unit.

| Lab | Deliverable | Result |
|---|---|---|
| 8.1 | `lab-8.1-authentication/`: authentication, anti-replay, rate limited rejection reporting | 18 + 11 unit tests; system test 58/58 on the host simulation |
| 8.2 | `lab-8.2-key-management/`: 3 key slots, rekey, activate, recovery set, key state in HK | included above; `design-analysis.md` |
| 8.3 | `lab-8.3-ground-segment/`: Yamcs 5.13.7, MDB, bridge, displays, SAFE procedure | end to end check 19/19; procedure SAFE-01 run to completion |
| 8.4 | `lab-8.4-security-analysis/security-analysis.md` | 15 threats, 10 residual risks (one closed by a design change), 9 lock-out paths |

## 1. Authentication (Lab 8.1)

**Format.** A 21-octet trailer inside the packet, before the CRC: SPI (key slot), a 32-bit counter, and the first 16 octets of HMAC-SHA-256 over everything from the primary header to the counter. The length field counts the trailer, so a Session 4 parser still sees a valid packet.

**Library primitive.** HMAC-SHA-256 and AES-128-CCM from tinycrypt (vendored, version and patch recorded). The ground uses Python `hmac` and `cryptography`: two independent implementations, which agree in the unit tests (known-answer values from Python) and in every system test. UBSan found two undefined shifts in tinycrypt's AES key schedule on the first test run; they are patched and recorded.

**Order of checks.** Structure and CRC (Session 4, unchanged), then authentication, then freshness, then the service's semantic check, then the counter is committed, then execution. The Session 4 handler gained one optional hook (`tc_ctx_t.security`, NULL by default), so Sessions 4 to 7 behave exactly as before: the Session 4 tests, 102 unit and 13 integration tests and the static analysis (129 findings, all dispositioned) are unchanged, and four unit tests of the hook's contract were added to the Session 7 suite (106). The Session 8 code is attached to the Session 6 application by three linker wraps; no Session 5 or 6 file was edited.

**Rejections.** Six classes (STRUCTURE, CRC, AUTH, REPLAY, WINDOW, STATE). A rejection up to freshness gets **no** TM(1,2): an unauthenticated sender learns nothing per packet. The TC path only counts; the HK task reports one REJECT_FIRST event at once and one REJECT_SUMMARY per 10 s window per class. 400 invalid TCs of 4 classes produced 8 events, and the summaries account for every one of them.

**Policies.** Counter committed only after full acceptance (a semantically rejected TC does not consume its value). Far-future counter: rejected if more than 65 536 ahead (WINDOW), never jumped to. Telecommands released by the onboard schedule are trusted: the TC(11,4) that carried them was authenticated.

## 2. Key management and recovery (Lab 8.2, full analysis in `lab-8.2-key-management/design-analysis.md`)

Slot 0 holds the recovery key (ROM); slots 1 and 2 the operational keys, one ACTIVE. A new key is loaded wrapped (AES-CCM under a ROM key encryption key, slot bound) into the inactive slot, its check value is compared on the ground with HK SID 3, and a separate command activates it; the old key stays as the fallback. Keys, check values, counters and the recovery challenge are each stored three times, voted, and scrubbed at 1 Hz.

**Upset.** One copy wrong: repaired by the scrub. Two copies: the slot is CORRUPT, and the recovery key activates the fallback. **Clock reset.** No effect: freshness is a counter, not a time. **Counter corrupted high.** Every command becomes a replay; the recovery key sets a floor above every value the ground ever used. **NV loss.** Found during the analysis (R-02): a blank key store re-installs the provisioned key with counter 0, so every command ever sent under it would be valid again. Closed in the design: in factory state slot 1 accepts nothing until a recovery command sets its floor ("first contact", test [8.2-0]).

**Position on the recovery set: for, minimal, separate key.** Two commands only (set a counter floor; activate a loaded spare), under the ROM recovery key, with a one-time challenge from HK. It cannot change modes, load keys or run payload commands. Its real power, lowering a counter and so re-enabling recorded traffic, is stated in the analysis together with its mitigations (every use is reported; the procedure ends with a rekey). Without it, two credible single events leave a healthy spacecraft that can no longer be commanded.

## 3. Ground segment (Lab 8.3)

Yamcs 5.13.7 runs the instance `s8`. The MDB (2 215 lines of XTCE) is generated from one table that names the C function building each packet, and is checked field by field against the independent ground decoders (5 460 comparisons, 0 mismatches). TM: HK SID 1, 2 and 3, all events with templates, TM(1,x) and TM(17,2). TC: 18 commands; PUS 1 verifiers match TM(1,x) by request ID. SET_MODE completes only when HK shows the new mode, which is the Session 7 NCR-01 workaround made executable.

The bridge connects to Yamcs over TCP (10015 TM, 10025 TC) and stands for the ground security unit. **Yamcs never holds a key:** LOAD_KEY carries a slot number, and the bridge generates and wraps the key. All service 210 commands are "critical" and wait in a supervised queue for a second operator. Operator display: `operator.par` (mode, health, key slots, KCVs, counters, rejections); event log: the Yamcs event page; procedure SAFE-01 is a Yamcs command stack with checks and verifications, run end to end (fault injected, SAFE, recovery to NOMINAL, schedule re-enabled). Writing it found OBS-02: in SAFE, the essential HK has no monitor states, so the procedure checks the values against the clear thresholds instead.

## 4. Timing budget with crypto

**Operation costs.** Estimated before target measurement from instruction counts (valgrind) calibrated on the Session 4 SIS run of the Lab 3.2 work loop: authenticating a 120-octet TC adds about 1.4 ms; a rejected forgery costs 1.8 ms; the 1 Hz key scrub 2.4 ms; a key load 2.2 ms. The calibration loop is memory-bound, so these are probably high. `app/run-sis.sh` measures them on SIS (`sec-timing.exe`), together with 60 s rate monotonic statistics of the full flight build (`lab8-timing.exe`), and regenerates the budget (`docs/timing/budget-sis.md`).

| Task | T (ms) | C (ms) | U | Blocking B (ms) | Response R (ms) |
|---|---:|---:|---:|---:|---:|
| ACS_CTRL | 100 | 18.957 | 0.1896 | 0 | 19.0 |
| TM_HK (+ scrub, SID 3) | 500 | 47.764 + 2.6 | 0.1007 | 4.2 | 73.5 |
| PL_LOG | 1000 | 95.644 | 0.0956 | 4.2 | 188.1 |

**The task set still closes.** U = 0.386 against 0.381 in Session 3/4 and a Liu and Layland bound of 0.780; every response time is far inside its period. Two new terms appear:

- **Blocking.** TM_HK now takes the tc lock (snapshot, scrub, event queue), so it can wait for one telecommand in progress at lower priority. With priority inheritance this is one critical section, at worst a key load, 4.2 ms; PL_LOG inherits the same term indirectly.
- **Aperiodic TC load.** TC_RX is aperiodic and below the three periodic tasks (priority 40), so no amount of telecommand traffic, forged or not, can delay them. At the lab console's maximum rate (47 TC/s) a flood of forgeries costs 0.086 of the CPU, taken from slack; on a 4 kbit/s S-band uplink, 0.007. The tasks below TC_RX (SCHED, TM_TX) would be slowed by such a flood, not stopped.

**Estimate against measurement.** The values above were the estimate made before running on the target. The block below is generated by `docs/timing/budget.py` from the SIS logs (`app/run-sis.sh`) and compares the two; the full table, with every periodic task of the application, is `docs/timing/budget-sis.md`.

<!-- SIS-TIMING:BEGIN -->
**Measured on SIS** (`docs/timing/budget-sis.md`, generated from `sis-sec-timing.log` and `sis-rm.log` by `budget.py`).

| Quantity | Estimate | SIS | Difference |
|---|---:|---:|---:|
| plain, max (ms) | 0.553 | 0.288 | -48 % |
| accept, max (ms) | 1.996 | 1.326 | -34 % |
| reject, max (ms) | 1.823 | 1.223 | -33 % |
| scrub, max (ms) | 2.384 | 1.771 | -26 % |
| load, max (ms) | 2.160 | 1.593 | -26 % |
| MAC per TC (ms) | 1.443 | 1.038 | -28 % |
| ACS_CTRL C max (ms) | 18.957 | 18.958 | +0 % |
| TM_HK C max (ms) | 50.348 | 48.797 | -3 % |
| PL_LOG C max (ms) | 95.644 | 95.671 | +0 % |
| U, three Session 3 tasks | 0.3859 | 0.3828 | -1 % |
| U, all 5 periodic tasks | - | 0.3829 | - |
| Missed periods in 60 s | 0 | 0 | - |

Differences above 25 % for operations, or 5 % for task C: `plain` measured 0.288 ms against an estimate of 0.553 ms (-48 %); `accept` measured 1.326 ms against an estimate of 1.996 ms (-34 %); `reject` measured 1.223 ms against an estimate of 1.823 ms (-33 %); `scrub` measured 1.771 ms against an estimate of 2.384 ms (-26 %); `load` measured 1.593 ms against an estimate of 2.160 ms (-26 %).

With the measured values the task set still closes: U = 0.3829 for 5 periodic tasks (bound 0.7435), largest R/T = 0.190, 0 missed periods.
<!-- SIS-TIMING:END -->

## 5. Evidence and limits

| Check | Result |
|---|---|
| Unit tests (ASan, UBSan) | 29/29 (key store 11, authentication and service 210: 18) |
| System tests, host simulation | 58/58 (`lab-8.1-authentication/tests/system-test.log`) |
| Session 5 scenarios through the security unit | gs_modes 49/51 and check_flight 11/11, as in Session 6; campaign 32/33: the corrupted-CRC row now gets no TM(1,2), by design (Section 1) |
| Yamcs end to end | 19/19 (`lab-8.3-ground-segment/logs/yamcs-check-host-sim.log`) |
| Sessions 4 to 7 after the handler hook | unchanged; Session 7 unit suite 102 -> 106 (the hook's contract) |

Limits: SIS has no non-volatile memory, so persistence across a reset ([8.1-6]) is shown on the host only. The downlink is neither authenticated nor encrypted (R-01). One consequence bears on recovery: the recovery challenge is read from that telemetry, so spoofed telemetry can show the ground a false challenge and make legitimate recovery commands fail. It gives an attacker no key and no commanding authority, but it can deny recovery until the spoofing stops; the procedure is to re-read the challenge and retry (a rejected attempt does not consume it). Only an authenticated downlink closes it. Yamcs runs without users or TLS, and the lab keys are public.
