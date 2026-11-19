# LOTUS-1 timing budget (integrated application)

Operation costs: ESTIMATE (instruction counts calibrated on Session 4 SIS). Task set: Session 4 SIS C max plus the Session 8 additions to TM_HK (estimate); SUPERVISOR and SCHED: design allowances (SUPERVISOR 5.0 ms, SCHED 4.0 ms).

## Security operations

| Operation | estimate (ms) | 
|---|---:|
| plain | 0.553 |
| accept | 1.996 |
| reject | 1.823 |
| scrub | 2.384 |
| load | 2.160 |

MAC cost per command (accept minus plain): 1.443 ms.

## Task set

| Task | P | T (ms) | C (ms) | U | B (ms) | R (ms) | R <= T |
|---|---:|---:|---:|---:|---:|---:|---|
| SUPERVISOR | 5 | 2000 | 5.000 | 0.0025 | 0.000 | 5.0 | yes |
| ACS_CTRL | 10 | 100 | 18.957 | 0.1896 | 0.000 | 24.0 | yes |
| TM_HK | 20 | 500 | 50.348 | 0.1007 | 4.157 | 78.5 | yes |
| PL_LOG | 30 | 1000 | 95.644 | 0.0956 | 4.157 | 193.1 | yes |
| SCHED | 45 | 1000 | 4.553 | 0.0046 | 4.157 | 197.6 | yes |

Periodic utilisation U = 0.3930 (Session 4, three tasks: 0.3807; estimate for this task set: 0.3930). Liu and Layland bound for n = 5: 0.7435. Missed periods: not measured.

TC_RX (priority 40, below the periodic tasks except SCHED) under a flood of invalid TCs, each costing one rejection:

| Uplink | TC/s max | TC_RX load | U with TC_RX |
|---|---:|---:|---:|
| lab console, 115200 baud, hex lines | 47.2 | 0.0861 | 0.4791 |
| S-band TC uplink, 4 kbit/s, 120 octet TCs | 3.9 | 0.0071 | 0.4001 |

Verdict: the task set CLOSES (every R <= T).
