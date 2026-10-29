# Divergence summary

Source: `results/divergences.csv` (1562 rows).

| Class | Suite A | Suite B | Soak A | Soak B | Distinct suite B patterns |
|---|---:|---:|---:|---:|---:|
| conflicting ground commands | 0 | 18 | 0 | 347 | 18 |
| fault lost to order (hand) | 0 | 10 | 0 | 148 | 10 |
| ground overrides same-cycle fault (hand) | 0 | 17 | 0 | 88 | 17 |
| intermediate transitions (hand) | 0 | 21 | 0 | 215 | 21 |
| same end state but different cause attribution | 0 | 16 | 0 | 118 | 16 |
| timing only (not a divergence) | 0 | 49 | 0 | 515 | 49 |

## conflicting ground commands

| Start | Inhibit | Events in arrival order (event:cause) | Hand written | Generated |
|---|---|---|---|---|
| NOMINAL | 0 | tc_mode_degraded:1 tc_mode_nominal:1 | NOMINAL>DEGRADED(c1) DEGRADED>NOMINAL(c1) -> **NOMINAL** | NOMINAL>DEGRADED(c1) discard:tc_mode_nominal -> **DEGRADED** |
| NOMINAL | 1 | tc_mode_degraded:1 tc_mode_nominal:1 | NOMINAL>DEGRADED(c1) DEGRADED>NOMINAL(c1) -> **NOMINAL** | NOMINAL>DEGRADED(c1) discard:tc_mode_nominal -> **DEGRADED** |
| NOMINAL | 0 | tc_mode_safe:1 tc_mode_nominal:1 | NOMINAL>SAFE(c1) SAFE>NOMINAL(c1) -> **NOMINAL** | NOMINAL>SAFE(c1) discard:tc_mode_nominal -> **SAFE** |
| NOMINAL | 1 | tc_mode_safe:1 tc_mode_nominal:1 | NOMINAL>SAFE(c1) SAFE>NOMINAL(c1) -> **NOMINAL** | NOMINAL>SAFE(c1) discard:tc_mode_nominal -> **SAFE** |
| NOMINAL | 0 | tc_mode_safe:1 tc_mode_degraded:1 | NOMINAL>SAFE(c1) SAFE>DEGRADED(c1) -> **DEGRADED** | NOMINAL>SAFE(c1) discard:tc_mode_degraded -> **SAFE** |
| NOMINAL | 1 | tc_mode_safe:1 tc_mode_degraded:1 | NOMINAL>SAFE(c1) SAFE>DEGRADED(c1) -> **DEGRADED** | NOMINAL>SAFE(c1) discard:tc_mode_degraded -> **SAFE** |
| ... | | 12 more in the CSV | | |

## fault lost to order (hand)

| Start | Inhibit | Events in arrival order (event:cause) | Hand written | Generated |
|---|---|---|---|---|
| DEGRADED | 0 | fault_noncritical:2 tc_mode_nominal:1 | DEGRADED>NOMINAL(c1) -> **NOMINAL** | DEGRADED>NOMINAL(c1) NOMINAL>DEGRADED(c2) -> **DEGRADED** |
| SAFE | 0 | fault_noncritical:2 tc_mode_nominal:1 | SAFE>NOMINAL(c1) -> **NOMINAL** | SAFE>NOMINAL(c1) NOMINAL>DEGRADED(c2) -> **DEGRADED** |
| SAFE | 0 | fault_critical:2 tc_mode_nominal:1 | SAFE>NOMINAL(c1) -> **NOMINAL** | SAFE>NOMINAL(c1) NOMINAL>SAFE(c2) -> **SAFE** |
| SAFE | 0 | fault_critical:2 tc_mode_degraded:1 | SAFE>DEGRADED(c1) -> **DEGRADED** | SAFE>DEGRADED(c1) DEGRADED>SAFE(c2) -> **SAFE** |
| SAFE | 0 | fault_noncritical:2 fault_critical:2 tc_mode_nominal:1 | SAFE>NOMINAL(c1) -> **NOMINAL** | SAFE>NOMINAL(c1) NOMINAL>SAFE(c2) -> **SAFE** |
| SAFE | 0 | fault_critical:2 tc_mode_nominal:1 fault_noncritical:2 | SAFE>NOMINAL(c1) NOMINAL>DEGRADED(c2) -> **DEGRADED** | SAFE>NOMINAL(c1) NOMINAL>SAFE(c2) -> **SAFE** |
| ... | | 4 more in the CSV | | |

## ground overrides same-cycle fault (hand)

| Start | Inhibit | Events in arrival order (event:cause) | Hand written | Generated |
|---|---|---|---|---|
| NOMINAL | 0 | fault_noncritical:2 tc_mode_nominal:1 | NOMINAL>DEGRADED(c2) DEGRADED>NOMINAL(c1) -> **NOMINAL** | NOMINAL>DEGRADED(c2) discard:tc_mode_nominal -> **DEGRADED** |
| NOMINAL | 0 | fault_critical:2 tc_mode_nominal:1 | NOMINAL>SAFE(c2) SAFE>NOMINAL(c1) -> **NOMINAL** | NOMINAL>SAFE(c2) discard:tc_mode_nominal -> **SAFE** |
| NOMINAL | 0 | fault_critical:2 tc_mode_degraded:1 | NOMINAL>SAFE(c2) SAFE>DEGRADED(c1) -> **DEGRADED** | NOMINAL>SAFE(c2) discard:tc_mode_degraded -> **SAFE** |
| NOMINAL | 0 | fault_noncritical:2 fault_critical:2 tc_mode_nominal:1 | NOMINAL>DEGRADED(c2) DEGRADED>SAFE(c2) SAFE>NOMINAL(c1) -> **NOMINAL** | NOMINAL>SAFE(c2) discard:tc_mode_nominal -> **SAFE** |
| NOMINAL | 0 | fault_critical:2 tc_mode_nominal:1 fault_noncritical:2 | NOMINAL>SAFE(c2) SAFE>NOMINAL(c1) NOMINAL>DEGRADED(c2) -> **DEGRADED** | NOMINAL>SAFE(c2) discard:tc_mode_nominal -> **SAFE** |
| NOMINAL | 0 | fault_critical:2 fault_noncritical:2 tc_mode_nominal:1 | NOMINAL>SAFE(c2) SAFE>NOMINAL(c1) -> **NOMINAL** | NOMINAL>SAFE(c2) discard:tc_mode_nominal -> **SAFE** |
| ... | | 11 more in the CSV | | |

## intermediate transitions (hand)

| Start | Inhibit | Events in arrival order (event:cause) | Hand written | Generated |
|---|---|---|---|---|
| NOMINAL | 0 | tc_mode_degraded:1 tc_mode_safe:1 | NOMINAL>DEGRADED(c1) DEGRADED>SAFE(c1) -> **SAFE** | NOMINAL>SAFE(c1) discard:tc_mode_degraded -> **SAFE** |
| NOMINAL | 1 | tc_mode_degraded:1 tc_mode_safe:1 | NOMINAL>DEGRADED(c1) DEGRADED>SAFE(c1) -> **SAFE** | NOMINAL>SAFE(c1) discard:tc_mode_degraded -> **SAFE** |
| NOMINAL | 0 | tc_mode_degraded:1 fault_critical:2 | NOMINAL>DEGRADED(c1) DEGRADED>SAFE(c2) -> **SAFE** | NOMINAL>SAFE(c2) discard:tc_mode_degraded -> **SAFE** |
| NOMINAL | 0 | fault_noncritical:2 tc_mode_safe:1 | NOMINAL>DEGRADED(c2) DEGRADED>SAFE(c1) -> **SAFE** | NOMINAL>SAFE(c1) -> **SAFE** |
| NOMINAL | 0 | fault_noncritical:2 fault_critical:2 | NOMINAL>DEGRADED(c2) DEGRADED>SAFE(c2) -> **SAFE** | NOMINAL>SAFE(c2) -> **SAFE** |
| NOMINAL | 0 | tc_mode_nominal:1 fault_noncritical:2 fault_critical:2 | NOMINAL>DEGRADED(c2) DEGRADED>SAFE(c2) -> **SAFE** | NOMINAL>SAFE(c2) discard:tc_mode_nominal -> **SAFE** |
| ... | | 15 more in the CSV | | |

## same end state but different cause attribution

| Start | Inhibit | Events in arrival order (event:cause) | Hand written | Generated |
|---|---|---|---|---|
| NOMINAL | 0 | fault_noncritical:2 tc_mode_degraded:1 | NOMINAL>DEGRADED(c2) -> **DEGRADED** | NOMINAL>DEGRADED(c1) -> **DEGRADED** |
| NOMINAL | 1 | fault_noncritical:2 tc_mode_degraded:1 | refused(c2) NOMINAL>DEGRADED(c1) -> **DEGRADED** | NOMINAL>DEGRADED(c1) -> **DEGRADED** |
| NOMINAL | 1 | fault_noncritical:2 tc_mode_safe:1 | refused(c2) NOMINAL>SAFE(c1) -> **SAFE** | NOMINAL>SAFE(c1) -> **SAFE** |
| NOMINAL | 0 | fault_critical:2 tc_mode_safe:1 | NOMINAL>SAFE(c2) -> **SAFE** | NOMINAL>SAFE(c1) -> **SAFE** |
| NOMINAL | 1 | fault_critical:2 tc_mode_safe:1 | refused(c2) NOMINAL>SAFE(c1) -> **SAFE** | NOMINAL>SAFE(c1) -> **SAFE** |
| NOMINAL | 0 | fault_critical:2 tc_mode_safe:1 fault_noncritical:2 | NOMINAL>SAFE(c2) -> **SAFE** | NOMINAL>SAFE(c1) -> **SAFE** |
| ... | | 10 more in the CSV | | |

## timing only (not a divergence)

| Start | Inhibit | Events in arrival order (event:cause) | Hand written | Generated |
|---|---|---|---|---|
| NOMINAL | 0 | tc_mode_degraded:1 fault_noncritical:2 | NOMINAL>DEGRADED(c1) -> **DEGRADED** | NOMINAL>DEGRADED(c1) -> **DEGRADED** |
| NOMINAL | 1 | tc_mode_degraded:1 fault_noncritical:2 | NOMINAL>DEGRADED(c1) -> **DEGRADED** | NOMINAL>DEGRADED(c1) -> **DEGRADED** |
| NOMINAL | 1 | tc_mode_degraded:1 fault_critical:2 | NOMINAL>DEGRADED(c1) refused(c2) -> **DEGRADED** | NOMINAL>DEGRADED(c1) refused(c2) -> **DEGRADED** |
| NOMINAL | 0 | tc_mode_safe:1 fault_noncritical:2 | NOMINAL>SAFE(c1) -> **SAFE** | NOMINAL>SAFE(c1) -> **SAFE** |
| NOMINAL | 1 | tc_mode_safe:1 fault_noncritical:2 | NOMINAL>SAFE(c1) -> **SAFE** | NOMINAL>SAFE(c1) -> **SAFE** |
| NOMINAL | 0 | tc_mode_safe:1 fault_critical:2 | NOMINAL>SAFE(c1) -> **SAFE** | NOMINAL>SAFE(c1) -> **SAFE** |
| ... | | 43 more in the CSV | | |

