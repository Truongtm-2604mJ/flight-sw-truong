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
| OBS-01 | OBS | (none; candidate FDIR-100) | - | **open** | VT-OBS-01 (exploratory), campaign runs 1, 2 and 3 |

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

## OBS-01: Validation observation, (none; candidate FDIR-100)

**Status:** open.

**Description.** With T_BAT still in alarm, a ground TC(8,1) NOMINAL sent in a later cycle than the alarm takes the vehicle to NOMINAL, and it stays there. HK shows the T_BAT monitor in ALARM, but no event reminds the operators, and the FDIR does not act again, because the monitor reports transitions only and its alarm was raised before the command. MM-130 covers only the same-cycle case, so no requirement is violated. Whether the behaviour is right is a mission question.

**Disposition.** Raised for review, not changed. Candidate requirement FDIR-100: "Leaving a mode by ground command while a monitor that requests a more severe mode is in alarm shall either be refused, or be reported with the active alarm." Two ways to meet it: re-post the latched fault on any ground de-escalation while the monitor is in ALARM, or have TC(8,1) acceptance check the monitor states. The choice belongs to operations, not to software.
