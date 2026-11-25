# Nonconformance log

Generated from `ncr.yaml` by `tests/validation/build_matrix.py`.

Kinds: **NCR** the product does not meet a requirement; **TA** test anomaly, the test was wrong and the product was not changed; **OBS** validation observation, no requirement violated but the behaviour may not meet the mission need.

Severity (NCR): critical, loss of vehicle or mission possible; major, requirement violated with operational impact and no workaround; minor, violated, with a workaround and no safety impact.

| ID | Kind | Requirement | Severity | Status | Found by |
|---|---|---|---|---|---|
| NCR-01 | NCR | TMTC-040 | minor | **deferred** | VT-TMTC-040, campaign run 1 (host simulation), repeated in runs 2 and 3 |
| NCR-02 | NCR | TMTC-030 | minor | **fixed** | VT-TMTC-030, campaign run 1 (host simulation) |
| TA-01 | TA | MM-030 | - | **fixed** | VT-MM-030, campaign run 1 |
| TA-02 | TA | TMTC-020, TMTC-010 | - | **fixed** | integration suite, first run (labs/session-07/test-runs/integration-host-sim-first-run.log) |
| TA-03 | TA | (integration level, all IT cases) | - | **fixed** | first run of the integration image on SIS (make -C tests target) |
| OBS-01 | OBS | (none; candidate FDIR-120) | - | **open** | VT-OBS-01 (exploratory), campaign runs 1, 2 and 3 |
| OBS-02 | OBS | (none; MM-030 essential telemetry) | - | **open** | Lab 8.3, writing procedure SAFE-01 (Yamcs stack), host simulation |
| NCR-03 | NCR | FDIR-110 | major | **fixed** | tests/unit/test_thermal.c (test_no_new_sequence_until_degraded_left), first run |
| NCR-04 | NCR | SEC-020 | major | **open** | Session 8 system test [8.1-6] on SIS; analysis in the capstone |
| TA-04 | TA | MM-090 | - | **fixed** | VT-01 (cold boot, MM-090), first host run of the integrated campaign |
| TA-05 | TA | TMTC-020 | - | **fixed** | VT-TMTC-020, first host run of the integrated campaign (exception) |
| TA-06 | TA | SEC-040 | - | **fixed** | IT-16, first run |
| TA-07 | TA | SEC-030 | - | **fixed** | VT-SEC-020, first host run |
| OBS-03 | OBS | (none; static analysis process) | - | **closed** | Capstone integration, static analysis set-up |
| NCR-05 | NCR | (coding standard - MISRA C:2012 rule 5.9, advisory) | minor | **fixed** | fast-checks CI, static analysis job, first push of the integrated tree (commit 45eb083) |
| TA-08 | TA | SEC-040 | - | **fixed** | First SIS run of the integrated campaign (make sis), 29 of 35 tests failed |

## NCR-01: Nonconformance, TMTC-040

**Status:** deferred. **Severity:** minor.

**Description.** TC(8,1) "set mode" is acknowledged with TM(1,7), completion of execution, when the request is posted to the mode machine. The mode changes at the next supervisor cycle, up to 2.0 s later: measured 1.999 s in all three runs. An operator or procedure that moves on as soon as TM(1,7) arrives sees a vehicle still in the old mode; the first HK after TM(1,7) shows it.

**Cause.** Introduced in Session 6 by the cyclic evaluation (divergence D-07). The service interface (tc_handler exec returns once, and TM(1,7) follows at once) assumes every command takes effect inside exec. With the generated mode machine that is no longer true for TC(8,1). MM-150 bounds the delay but does not say which report marks completion.

**Disposition.** Deferred, with an operational workaround until the fix. Workaround: procedures wait for the TM(5,x) mode transition report (MM-080), within 3.0 s (MM-150), not for TM(1,7). Planned fix (Session 8 or capstone): - an exec result "completion pending" in tc_handler: TM(1,1) is sent,
  TM(1,7) is not;
- the services keep the request ID and ack flags of the pending TC(8,1); - the mode machine's transition hook marks it complete, and the
  supervisor sends TM(1,7) after it releases the mode lock (lock order:
  tc lock before mode lock, Session 5 Section 4);
- a command discarded by the model (MM-140) gets TM(1,8) instead. This touches the Session 4 handler, the services and fdir6.c. It is not done in this session because it changes the service 1 contract that every TC uses, and needs its own unit, integration and validation tests.

**Retest.** VT-TMTC-040 must pass; IT-06 must then expect TM(1,7) after the transition report.

## NCR-02: Nonconformance, TMTC-030

**Status:** fixed. **Severity:** minor.

**Description.** A received line longer than the TC_RX buffer (here 121 octets, 245 characters) was dropped without being counted. The HK TC rejected counter rose by 3 for the 4 undecodable lines sent. The ground cannot see that kind of link noise.

**Cause.** tmtc5.c tc_rx_task(): at the end of a line it called process_line() only if the line had not overflowed, and the overflow branch counted nothing. process_line() counts every other undecodable line.

**Disposition.** Fixed in labs/session-05/app/tmtc5.c: an overflowed line is counted with tc_count_link_error() under the tc lock, like the other link errors.

**Retest.** VT-TMTC-030 PASS in campaign run 2 (counter +4).

## TA-01: Test anomaly, MM-030

**Status:** fixed.

**Description.** VT-MM-030 failed in run 1, reporting HK SID 1 after the SAFE transition. The log shows only SID 2 in that boot. The test compared onboard times against the telemetry of every run of the campaign, and onboard time restarts at every boot, so SID 1 reports from run R1 (onboard times above 2.0 s) were counted.

**Disposition.** Test corrected to use the current boot only. The same fix was applied to the two schedule tests that had the same pattern (VT-MM-020a/b), where it had not yet caused a failure.

**Retest.** VT-MM-030 PASS in run 2. The product was not changed.

## TA-02: Test anomaly, TMTC-020, TMTC-010

**Status:** fixed.

**Description.** IT-02 and IT-04 expected packet-level rejections to be addressed to the TC's source ID. By design, a TC that fails a packet-level check has an untrusted source ID, and its report goes to destination 0 (Session 4 design note, Section 3). IT-08 expected TM(1,7) right after TM(1,1), but the inhibit event is emitted during execution, between them.

**Disposition.** Tests corrected to the documented behaviour. The product was not changed.

**Retest.** Integration suite 11/11, and 20 consecutive runs passing.

## TA-03: Test anomaly, (integration level, all IT cases)

**Status:** fixed.

**Description.** On RTEMS 6 / LEON3 under SIS, the integration image stopped at start with RTEMS fatal error 38 (INTERNAL_ERROR_ILLEGAL_USE_OF_FLOATING_POINT_UNIT) in its Init task. The same image passes on the host simulation.

**Cause.** Test harness configuration. The application runs the TC path in tasks with the RTEMS_FLOATING_POINT attribute (Init in lab5.c, TC_RX and TM_TX in tmtc5.c); the integration program gave its Init task and its two dispatching tasks the default attributes. The host simulation of the RTEMS API ignores task attributes, so only the target could show this: an example of the defect class the host level cannot catch (verification report, Section 1).

**Disposition.** int_tc_path.c now configures its tasks like the application. The flight code was not changed. The SIS runner also needed a pseudo-terminal, like the ground scripts (run_image.py --pty).

**Retest.** Integration image on SIS (make -C tests target).

## OBS-01: Validation observation, (none; candidate FDIR-120)

**Status:** open.

**Description.** With T_BAT still in alarm, a ground TC(8,1) NOMINAL sent in a later cycle than the alarm takes the vehicle to NOMINAL, and it stays there. HK shows the T_BAT monitor in ALARM, but no event reminds the operators, and the FDIR does not act again, because the monitor reports transitions only and its alarm was raised before the command. MM-130 covers only the same-cycle case, so no requirement is violated. Whether the behaviour is right is a mission question.

**Disposition.** Raised for review, not changed. Candidate requirement FDIR-120: "Leaving a mode by ground command while a monitor that requests a more severe mode is in alarm shall either be refused, or be reported with the active alarm." Two ways to meet it: re-post the latched fault on any ground de-escalation while the monitor is in ALARM, or have TC(8,1) acceptance check the monitor states. The choice belongs to operations, not to software.

## OBS-02: Validation observation, (none; MM-030 essential telemetry)

**Status:** open.

**Description.** In SAFE only the essential HK (SID 2) is sent, and it carries no monitor states. The recovery procedure cannot read "T_BAT monitor CLEARED"; it has to compare the raw values with the onboard clear thresholds (V_BUS >= 26300 mV, T_BAT <= 43.00 C), which duplicates onboard constants in a ground procedure.

**Disposition.** Open, raised for review. Workaround in SAFE-01 step 5 (values against the thresholds). Candidate change: add the 6-bit monitor state word to SID 2 (one octet), and the MDB entry; the downlink cost is 1 octet per 2 s in SAFE.

## NCR-03: Nonconformance, FDIR-110

**Status:** fixed. **Severity:** major.

**Description.** After THERMAL_EXHAUSTED the vehicle stays in DEGRADED until the next supervisor cycle applies SAFE (up to 2 s). In that gap the next HK cycle saw the alarm still present in DEGRADED and started a second attempt sequence: a second ATTEMPT 1 event, and the attempt bound of FDIR-100 broken.

**Cause.** The recovery state machine went back to idle on EXHAUSTED with nothing to remember that the sequence was spent; the SAFE request is deferred by design (cyclic mode machine, D-07).

**Disposition.** Fixed: a "spent" latch, set on EXHAUSTED and cleared only when the mode leaves DEGRADED (src/fdir/thermal.c). The unit test that found it is kept.

**Retest.** test_thermal (6 tests), VT-FDIR-100 and the demo scenario step 5, all PASS.

## NCR-04: Nonconformance, SEC-020

**Status:** open. **Severity:** major.

**Description.** On SIS the key store (keys and counters) is held in RAM, because the simulator has no non volatile memory. After a processor reset the slot counters restart from the factory state, so a telecommand recorded before the reset could be accepted again once the floor is reset. On the host simulation the key store is a file and the test passes.

**Cause.** Platform limit, not a code defect: nv_load/nv_store in src/security/security.c are a RAM image under __rtems__.

**Disposition.** Open. Mitigation in place: after any reset the counter is untrusted (factory state) and commanding needs a recovery TC(210,10) under SPI 0 with the onboard challenge, and the ground sets the floor above its high-water mark, so recorded commands stay below it. Closure needs the NV driver of the real OBC (write with read-back, two copies) and a retest of [8.1-6] and VT-SEC-010 on the target.

**Retest.** Session 8 [8.1-6] and VT-SEC-010 on the flight OBC or an emulator with NV memory.

## TA-04: Test anomaly, MM-090

**Status:** fixed.

**Description.** The first validation test saw no boot event. The secure ground link performs the first contact (TC(210,10) under SPI 0) on the first boot, and that exchange read and discarded the boot event before the test started.

**Cause.** Test harness - the first contact was added to the link without restarting the spacecraft.

**Disposition.** Fixed in tests/validation/run_campaign.py: after the first contact the link restarts the spacecraft cold (FDIR NV removed, key store kept), so every run starts with the boot it expects. No flight code change.

**Retest.** Integrated campaign on the host, VT-MM-090 PASS.

## TA-05: Test anomaly, TMTC-020

**Status:** fixed.

**Description.** The campaign crashed decoding a 41-octet TM(3,25) as HK SID 1. The packet was security housekeeping (SID 3, APID 0x022), new in the integrated build.

**Cause.** Test harness selected HK by service and subtype only.

**Disposition.** Fixed - every HK selection in the campaign also checks APID 0x020.

**Retest.** Integrated campaign on the host.

## TA-06: Test anomaly, SEC-040

**Status:** fixed.

**Description.** The scheduled command was never released. IT-14 (run just before) had put the vehicle in SAFE, which suspends the schedule (MM-020), so the test was checking a schedule that was correctly stopped.

**Cause.** Test order dependency in int_tc_path.c.

**Disposition.** IT-16 now enables the schedule with an authenticated TC(11,1) first.

**Retest.** Integration level 16/16 PASS.

## TA-07: Test anomaly, SEC-030

**Status:** fixed.

**Description.** The test set the counter copies to N, N, N + 7 and expected a COUNTER_VOTE event. None came. Two copies agreeing is a majority: the key store repairs the odd copy silently and the counter stays N.

**Cause.** The test's expectation was wrong, not the code. keystore.c reports a vote (and takes the largest copy) only when no two copies agree; a single disagreeing copy is an ordinary repair. That is the Lab 8.2 design (design-analysis.md, "counter upsets").

**Disposition.** Test corrected - copies N, N + 3, N + 7 (no majority). The test now also checks that the counter becomes N + 7, the largest copy, so no used value becomes acceptable again, and that the ground must move its counter above it.

**Retest.** Integrated campaign on the host, VT-SEC-020.

## OBS-03: Validation observation, (none; static analysis process)

**Status:** closed.

**Description.** cppcheck and its MISRA C:2012 addon have not been run on the Session 8 security files and the capstone thermal recovery (tc_auth.c, keystore.c, sec_service.c, sec_report.c, security.c, thermal.c). GCC and clang at the maximum warning set, GCC -fanalyzer and the clang analyser have, with every finding dispositioned.

**Disposition.** Closed. The 12 findings (fast-checks run 45637380657) were triaged one by one. 2 were fixed in the code (SA-51 MISRA 10.7 in keystore.c, SA-52 MISRA 13.4 in security.c). 2 were accepted with a stated risk (SA-53 void-pointer callback context, SA-54 memcpy of the key store image). 3 were justified (SA-55 host-only stdio and getenv, SA-56 cppcheck configuration, SA-57 the always-false check on the SIS RAM stand-in). The broad SA-50 rule is removed, so any new finding in these files fails CI. History of the item - The fast-checks CI job runs cppcheck on every push. Findings in these files that no project-wide rule covers match SA-50 (disposition "open") and are listed in findings-current.md, so they are visible without failing every build. To close - triage each one into fixed, justified or accepted, then delete SA-50.

## NCR-05: Nonconformance, (coding standard - MISRA C:2012 rule 5.9, advisory)

**Status:** fixed. **Severity:** minor.

**Description.** cppcheck's whole-program MISRA check found static helper functions with the same name in different files of the one application (put16, put32, get32, get_u32, event, nv_path, persist, sat8, sat16), and one variable whose scope could be reduced (aux in fdir_payload_command). Each lab had been analysed on its own, where no two files shared a program.

**Cause.** Integration. The Session 8 security files reuse the helper names of the Session 4 and 5 files. Rule 5.9 asks for unique internal identifiers across the program, so that a reader, or a debugger symbol table, never confuses two different functions.

**Disposition.** Fixed in commit 5fcd74f. The security files use their own prefixes (rep_, svc_, ks_, auth_, sec_, inj_), and aux moved into its block. All test levels were rerun, and the CI static analysis job is green.

**Retest.** fast-checks run 44120294053 green; unit 142, integration 16/16, scenario 28/28.

## TA-08: Test anomaly, SEC-040

**Status:** fixed.

**Description.** On SIS every command after the first spacecraft run was refused with SEC_STATE and no TM(1,x). SID 3 showed FIRST CONTACT NEEDED (slot 1 counter untrusted) at every boot.

**Cause.** The campaign performed the first contact once, when the link was created. On the host the key store is a file, so the floor survives every restart. On SIS it is RAM (NCR-04): every boot is a factory key store and needs its own first contact. The flight code behaved as SEC-040 requires (fail closed); the harness assumed host behaviour.

**Disposition.** Fixed in run_campaign.py. The first contact runs after every boot, once the boot report has been captured, and is a no-op when the counter is trusted. FDIR event checks now select APID 0x021, so the RECOVERY_USED security event of each first contact is not counted as an unexpected FDIR event.

**Retest.** Host campaign rerun (unchanged verdicts); SIS campaign rerun by the user.
