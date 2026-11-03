# Timing budget with authentication

Operation costs: ESTIMATE (instruction counts, calibrated on Session 4 SIS).

| Operation | mean (ms) | max (ms) |
|---|---:|---:|
| plain | 0.553 | 0.553 |
| accept | 1.996 | 1.996 |
| reject | 1.823 | 1.823 |
| scrub | 2.384 | 2.384 |
| load | 2.160 | 2.160 |

Task set: Session 4 SIS C max, plus the Session 8 additions to TM_HK.

| Task | P | T (ms) | C (ms) | U | B (ms) | R (ms) | R <= T |
|---|---:|---:|---:|---:|---:|---:|---|
| ACS_CTRL | 10 | 100 | 18.957 | 0.1896 | 0.000 | 19.0 | yes |
| TM_HK | 20 | 500 | 50.348 | 0.1007 | 4.157 | 73.5 | yes |
| PL_LOG | 30 | 1000 | 95.644 | 0.0956 | 4.157 | 188.1 | yes |

Periodic utilisation U = 0.3859 (Session 4: 0.3807); Liu and Layland bound for n = 3: 0.7798.

TC_RX (priority 40, below the three periodic tasks) under a flood of invalid TCs, each costing one rejection:

| Uplink | TC/s max | TC_RX load | U with TC_RX | bound n = 4 |
|---|---:|---:|---:|---:|
| lab console, 115200 baud, hex lines | 47.2 | 0.0861 | 0.4720 | 0.7568 |
| S-band TC uplink, 4 kbit/s, 120 octet TCs | 3.9 | 0.0071 | 0.3930 | 0.7568 |

MAC cost per command: 1.443 ms (accept minus plain).
