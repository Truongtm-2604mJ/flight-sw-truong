# What to expect on SIS (predictions, not results)

Nothing in this file was measured. It exists so the first SIS run can be
compared against something. **Do not copy these numbers into the design
note or into a log.** Replace them with what SIS prints.

## Lab 4.2: reply to a hand-typed ping

Type `TC:1810C00000062911010001306A` and press Enter. Expected: three lines.
`tttttttttttt` is the 6-octet onboard time and `cccc` the CRC; both
depend on when the command arrives. Everything else is fixed.

```
TM:0810C0000012 200101 0000 0001 tttttttttttt 1810C000 cccc    TM(1,1)  acceptance
TM:0810C001000E 201102 0000 0001 tttttttttttt          cccc    TM(17,2) test report
TM:0810C0020012 200107 0000 0001 tttttttttttt 1810C000 cccc    TM(1,7)  completion
```

(Spaces added here for reading; the real lines have none.)

If the last CRC digit is changed (`...306B`), expect one line only:
`TM:0810C0xx0013 200102 0000 0000 tttttttttttt 1810C000 03 cccc`,
that is TM(1,2) with failure code 3 and destination 0.

## Lab 4.3: events

The simulated sensors are deterministic, so the sequence is the same on
every platform: `TEMP_BAT_HIGH` on the 7th sample (about 3.0 s onboard
time, value 45.45 C), `TEMP_BAT_NOMINAL` on the 48th (about 23.5 s,
value 42.41 C), then again every 30 s. On RTEMS the timestamps will be
shifted by a few tens of ms, because TM_HK starts its first period only
after ACS_CTRL's first job.

## Lab 4.3: timing

| Quantity | Lab 3.2 measured | Predicted for Lab 4.3 | Basis |
|----------|-----------------:|----------------------:|-------|
| ACS_CTRL max CPU | 19.812 ms | about 19.8 ms | unchanged code |
| TM_HK max CPU | 49.782 ms | 50.1 to 50.3 ms | + 0.3 to 0.5 ms estimated for `hk_cycle()` |
| PL_LOG max CPU | 99.979 ms | about 100.0 ms | unchanged code |
| Total U | 0.3977 | about 0.3985 | |
| Missed periods | 0 | 0 | U far below 0.7798 |
| HK_EXEC minimum | n/a | 300 to 500 us | same estimate |
| TM_HK wall time | 69.6 ms | about 50 ms | see below |
| PL_LOG wall time | 119.8 ms | about 120 ms | see below |

Wall times depend on phasing. With the period call at the top of each
loop, I expect ACS_CTRL released at 0 ms, TM_HK at about 20 ms and
PL_LOG at about 70 ms. TM_HK then runs from 20 to 70 ms between two
ACS_CTRL jobs and is not preempted, and PL_LOG is preempted once. These
are less certain than the CPU-time rows; a TM_HK wall time near 70 ms is
also legitimate and only means the phasing differs.

If TM_HK CPU time comes out far above 50.3 ms, the first suspect is the
bitwise CRC built without optimisation; check that `-O2` reached the
compiler.
