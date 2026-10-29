# MM-150: ground mode command latency (host-sim)

Source log: `host-sim-modes.log`. Measured in onboard time, from the TM(1,7) completion report of each TC(8,1) to the mode transition report with cause GROUND.

| # | Commanded mode | TM(1,7) at (s) | Latency (s) |
|---|---|---:|---:|
| 1 | SAFE | 0.002 | 1.999 |
| 2 | NOMINAL | 18.003 | 1.998 |
| 3 | NOMINAL | 74.003 | no report in log |

Commands measured: 2. Maximum 1.999 s, minimum 1.998 s. Requirement: at most 3.0 s (supervisor period 2.0 s plus margin).

1 command(s) without a mode report before the log ended (run stopped first); not counted.

**Result: PASS**
