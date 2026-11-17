# SAFE-01 - Recovery from SAFE

Executable form: `SAFE-01-recover-from-safe.ycs`, a Yamcs command stack (Stacks > SAFE-01-recover-from-safe > Run). Each step has a check or a verification; the stack stops on a failed verification. Scripted run for testing: `tools/run_procedure.py` (log: `logs/SAFE-01-run-host-sim.log`).

**Roles.** Commander sends. Checker reads each check aloud and releases critical commands from the supervised queue.

**Entry conditions.** `/LOTUS1/mode` = SAFE. HK SID 2 arrives every 2 s (SAFE sends essential HK only). TM link OK in Links.

| Step | Action | Pass when | If not |
|---|---|---|---|
| 1 | Read `mode_cause`, `mode_cause_detail`, `last_reset`, `boot_count`; read the events | The cause is understood: MONITOR (which parameter), LIVENESS (which task), WATCHDOG_RESET (BOOT_REPORT), GROUND | Stay in SAFE, escalate |
| 2 | Read key state: `sec_active_slot`, slot states, `sec_rej_*`, `sec_flag_slot*_untrusted` | Active slot ACTIVE; untrusted flags OFF; rejection counters not rising | C-SEC |
| 3 | PING | Accepted and Complete OK | Accepted TIMEOUT with no TM(1,2): the TC was rejected before authentication. C-SEC |
| 4 | Corrective action for the cause (lab: TEST_INJECT_CLEAR) | Complete OK | Stay in SAFE |
| 5 | Verify the cause is gone: `vbus` >= 26300 mV and `tbat` <= 43.00 C (the onboard clear thresholds). SID 2 has no monitor states (OBS-02), so values are checked | Both true within 20 s | Stay in SAFE |
| 6 | SET_MODE DEGRADED | Complete OK: HK shows DEGRADED (up to 2 s after TM(1,7), MM-150) | Read MODE_CMD_DISCARDED; retry once |
| 7 | Verify in DEGRADED, full HK back: every `mon_state_*` OK | All OK within 8 s | SET_MODE SAFE (OBS-01: a ground command does not re-check an alarm raised earlier) |
| 8 | Hold, watch trends for one minute | No new monitor event | SET_MODE SAFE |
| 9 | SET_MODE NOMINAL | Complete OK | Stay in DEGRADED |
| 10 | Verify NOMINAL, `payload_on` true | Both within 6 s | Stay in DEGRADED |
| 11 | SCHED_ENABLE (SAFE entry suspended the schedule) after reviewing the timeline | Complete OK | Leave disabled, replan |
| 12 | Verify `sched_enabled` true | | |
| 13 | Log: cause, time in SAFE, `tc_rejected`, `sec_rej_*` | | |

## Contingency C-SEC: commanding refused

Commands time out at Accepted, with no TM(1,2). Read the security events and SID 3 to find which case it is.

| Seen | Cause | Action (critical commands: supervised queue) |
|---|---|---|
| `sec_flag_slotN_untrusted` ON, FACTORY flag | Key store in factory state (NV blank or lost) | First contact: RECOVERY_SET_COUNTER slot N, floor 0 (the security unit fills in its high-water mark + 1) |
| `sec_rej_replay` rising, `sec_slotN_counter` far above the ground's | Counter corrupted high, or ground state lost the other way | Ground behind: the bridge resynchronises from SID 3 by itself. Onboard too high: RECOVERY_SET_COUNTER slot N, floor 0 |
| `sec_rej_window` rising | Ground counter too far ahead | Restart the bridge (it follows SID 3) |
| `sec_slotN_state` CORRUPT, KEY_CORRUPT event | Active key lost | RECOVERY_ACTIVATE of the LOADED slot; then ERASE_KEY the corrupt slot and LOAD_KEY a new spare |
| `sec_rej_auth` rising | Wrong key on the ground, or someone else transmitting | Check which key the security unit uses (bridge log, active slot vs SID 3). If the ground is right, the uplink is being attacked: report; nothing executes |

Every recovery command uses the challenge in SID 3, which the bridge reads; after each one the challenge goes up by one. If a recovery command times out with no TM(1,x) and the REPLAY or WINDOW count rises, the challenge the ground used was wrong: wait for a fresh SID 3 and retry. A rejected attempt does not consume the onboard challenge. Repeated failure with a consistent-looking SID 3 can mean spoofed telemetry (security analysis, R-01): report it. After any recovery of a counter, finish with a rekey (LOAD_KEY into the spare slot, compare KCV, ACTIVATE_KEY), so commands recorded earlier are under a key the spacecraft no longer accepts.
