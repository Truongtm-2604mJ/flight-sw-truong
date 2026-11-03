# Lab 8.1 - Command authentication and anti-replay

| File | What |
|---|---|
| `tc_auth.h/.c` | Verify (authentication, then freshness), commit (counter, after full acceptance), reject (class counters). Plugs into the Session 4 handler through `tc_ctx_t.security` |
| `sec_report.h/.c` | Rate limited, aggregated rejection events, run by the HK task |
| `tests/test_auth_system.py` | System tests [8.1-1] to [8.1-6] and [8.2-0] to [8.2-6], host simulation or SIS (`--pty`) |
| `tests/system-test.log` | Last host run: 58/58 |
| `tests/regression/` | The Session 5 scenarios, unchanged, through the ground security unit |
| `../tests/unit/test_tc_auth.c` | Unit tests through the real `tc_handle()` (marked [8.1-1] to [8.1-5]) |

**Checks, in order:** structure and CRC (Session 4) -> MAC (HMAC-SHA-256/128, constant-time compare) -> counter (> last, <= last + 65 536) -> service check -> commit -> execute.

**Rejection classes:** STRUCTURE, CRC, AUTH, REPLAY, WINDOW, STATE. No TM(1,2) for any of them. Events: REJECT_FIRST (at once), REJECT_SUMMARY (per 10 s window), per class.

**Far-future policy:** more than 65 536 ahead is rejected (WINDOW) and the counter does not move. The ground resynchronises from HK SID 3.

| Required test | Unit | System |
|---|---|---|
| Valid command executes, counter advances | `test_valid_executes_and_advances` | [8.1-1] |
| Wrong key: rejected at authentication, never executes | `test_wrong_key_rejected_at_auth` | [8.1-2] (TC(8,1) SAFE, mode unchanged) |
| Replay rejected | `test_replay_rejected` | [8.1-3], and across a reset [8.1-6] |
| Far-future counter, stated policy | `test_far_future_rejected_by_window` | [8.1-4] |
| Burst of hundreds of invalid TCs: bounded events | `test_burst_gives_bounded_events`, `test_sustained_flood_bounded` | [8.1-5]: 400 TCs, 8 events |
