# SIS results against what I predicted

Before the first SIS run I wrote down what I expected, so the run could
be checked against something. This is the comparison.

| Quantity | Predicted | Measured on SIS | Verdict |
|----------|----------:|----------------:|---------|
| Reply to a hand-typed TC(17,1) | TM(1,1), TM(17,2), TM(1,7), sequence 0, 1, 2 | the same, CRCs valid | as predicted |
| `TEMP_BAT_HIGH` event | about 3.0 s, 45.45 C | 3.0 s, 45.45 C | as predicted |
| `TEMP_BAT_NOMINAL` event | about 23.5 s, 42.41 C | 23.5 s, 42.41 C | as predicted |
| Missed periods | 0 | 0 | as predicted |
| ACS_CTRL max CPU | about 19.8 ms | 18.957 ms | 4 % low |
| TM_HK max CPU | 50.1 to 50.3 ms | 47.764 ms | 5 % low |
| PL_LOG max CPU | about 100.0 ms | 95.644 ms | 4 % low |
| Total U | about 0.3985 | 0.3807 | lower |
| Cost of `hk_cycle()` (HK_EXEC) | 300 to 500 us | 99 to 100 us, 156 us with an event | 3 to 5 times too high |
| TM_HK wall time | about 50 ms | 47.8 ms | phasing as predicted |
| PL_LOG wall time | about 120 ms | 114.6 to 122.5 ms | phasing as predicted |

Where I was wrong:

- **CPU times.** I carried the Lab 3.2 numbers over, but this build
  (Makefile, `-O2`) runs the same loops about 4 % faster than the Lab 3.2
  build (`waf`). A workload calibration belongs to one build, not to the
  source file.
- **TM cost.** Overestimated; the bitwise CRC over 39 octets is cheaper
  than I assumed.

Two things I did not predict at all, both about the simulator:

- **SIS time runs ahead of wall time.** Without `-rt` the first
  hand-typed command was answered with an onboard time of 2179 s. With
  `-rt` it tracks real time.
- **SIS forwards stdin to the UART only when stdin is a terminal.** The
  ground station failed through a pipe (12 of 16 checks, no TM received)
  and passes 16 of 16 through a pseudo-terminal (`--pty`). `-nouartrx`, used in
  Lab 3, must be removed for these labs.
