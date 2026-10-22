# Divergence summary

Source: `results/run1-model-v1/divergences.csv` (2120 rows).

| Class | Suite A | Suite B | Soak A | Soak B | Distinct suite B patterns |
|---|---:|---:|---:|---:|---:|
| UNCLASSIFIED | 0 | 36 | 0 | 187 | 36 |
| conflicting ground commands | 0 | 20 | 0 | 373 | 20 |
| fault lost to order (hand) | 0 | 10 | 0 | 148 | 10 |
| ground overrides same-cycle fault (hand) | 0 | 17 | 0 | 88 | 17 |
| intermediate transitions (hand) | 0 | 8 | 0 | 148 | 8 |
| same end state but different cause attribution | 0 | 11 | 0 | 94 | 11 |
| single-event mismatch | 68 | 78 | 274 | 82 | 1 |
| timing only (not a divergence) | 0 | 38 | 0 | 440 | 38 |

## UNCLASSIFIED

| Start | Inhibit | Events in arrival order (event:cause) | Hand written | Generated |
|---|---|---|---|---|
| NOMINAL | 0 | tc_mode_nominal:1 tc_mode_safe:1 | NOMINAL>SAFE(c1) -> **SAFE** | NOMINAL>SAFE(c1) -> **SAFE** |
| NOMINAL | 1 | tc_mode_nominal:1 tc_mode_safe:1 | NOMINAL>SAFE(c1) -> **SAFE** | NOMINAL>SAFE(c1) -> **SAFE** |
| NOMINAL | 0 | tc_mode_nominal:1 fault_critical:2 | NOMINAL>SAFE(c2) -> **SAFE** | NOMINAL>SAFE(c2) -> **SAFE** |
| NOMINAL | 0 | tc_mode_degraded:1 fault_critical:2 | NOMINAL>DEGRADED(c1) DEGRADED>SAFE(c2) -> **SAFE** | NOMINAL>SAFE(c2) discard:tc_mode_degraded -> **SAFE** |
| NOMINAL | 0 | tc_mode_safe:1 fault_noncritical:2 | NOMINAL>SAFE(c1) -> **SAFE** | NOMINAL>SAFE(c1) -> **SAFE** |
| NOMINAL | 1 | tc_mode_safe:1 fault_noncritical:2 | NOMINAL>SAFE(c1) -> **SAFE** | NOMINAL>SAFE(c1) -> **SAFE** |
| ... | | 30 more in the CSV | | |

## conflicting ground commands

| Start | Inhibit | Events in arrival order (event:cause) | Hand written | Generated |
|---|---|---|---|---|
| NOMINAL | 0 | tc_mode_degraded:1 tc_mode_nominal:1 | NOMINAL>DEGRADED(c1) DEGRADED>NOMINAL(c1) -> **NOMINAL** | NOMINAL>DEGRADED(c1) -> **DEGRADED** |
| NOMINAL | 1 | tc_mode_degraded:1 tc_mode_nominal:1 | NOMINAL>DEGRADED(c1) DEGRADED>NOMINAL(c1) -> **NOMINAL** | NOMINAL>DEGRADED(c1) -> **DEGRADED** |
| NOMINAL | 0 | tc_mode_degraded:1 tc_mode_safe:1 | NOMINAL>DEGRADED(c1) DEGRADED>SAFE(c1) -> **SAFE** | NOMINAL>SAFE(c1) discard:tc_mode_degraded -> **SAFE** |
| NOMINAL | 1 | tc_mode_degraded:1 tc_mode_safe:1 | NOMINAL>DEGRADED(c1) DEGRADED>SAFE(c1) -> **SAFE** | NOMINAL>SAFE(c1) discard:tc_mode_degraded -> **SAFE** |
| NOMINAL | 0 | tc_mode_safe:1 tc_mode_nominal:1 | NOMINAL>SAFE(c1) SAFE>NOMINAL(c1) -> **NOMINAL** | NOMINAL>SAFE(c1) -> **SAFE** |
| NOMINAL | 1 | tc_mode_safe:1 tc_mode_nominal:1 | NOMINAL>SAFE(c1) SAFE>NOMINAL(c1) -> **NOMINAL** | NOMINAL>SAFE(c1) -> **SAFE** |
| ... | | 14 more in the CSV | | |

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
| NOMINAL | 0 | fault_noncritical:2 tc_mode_nominal:1 | NOMINAL>DEGRADED(c2) DEGRADED>NOMINAL(c1) -> **NOMINAL** | NOMINAL>DEGRADED(c2) -> **DEGRADED** |
| NOMINAL | 0 | fault_critical:2 tc_mode_nominal:1 | NOMINAL>SAFE(c2) SAFE>NOMINAL(c1) -> **NOMINAL** | NOMINAL>SAFE(c2) -> **SAFE** |
| NOMINAL | 0 | fault_critical:2 tc_mode_degraded:1 | NOMINAL>SAFE(c2) SAFE>DEGRADED(c1) -> **DEGRADED** | NOMINAL>SAFE(c2) discard:tc_mode_degraded -> **SAFE** |
| NOMINAL | 0 | fault_noncritical:2 fault_critical:2 tc_mode_nominal:1 | NOMINAL>DEGRADED(c2) DEGRADED>SAFE(c2) SAFE>NOMINAL(c1) -> **NOMINAL** | NOMINAL>SAFE(c2) -> **SAFE** |
| NOMINAL | 0 | fault_critical:2 tc_mode_nominal:1 fault_noncritical:2 | NOMINAL>SAFE(c2) SAFE>NOMINAL(c1) NOMINAL>DEGRADED(c2) -> **DEGRADED** | NOMINAL>SAFE(c2) -> **SAFE** |
| NOMINAL | 0 | fault_critical:2 fault_noncritical:2 tc_mode_nominal:1 | NOMINAL>SAFE(c2) SAFE>NOMINAL(c1) -> **NOMINAL** | NOMINAL>SAFE(c2) -> **SAFE** |
| ... | | 11 more in the CSV | | |

## intermediate transitions (hand)

| Start | Inhibit | Events in arrival order (event:cause) | Hand written | Generated |
|---|---|---|---|---|
| DEGRADED | 0 | tc_mode_nominal:1 tc_mode_safe:1 | DEGRADED>NOMINAL(c1) NOMINAL>SAFE(c1) -> **SAFE** | DEGRADED>SAFE(c1) discard:tc_mode_nominal -> **SAFE** |
| DEGRADED | 1 | tc_mode_nominal:1 tc_mode_safe:1 | DEGRADED>NOMINAL(c1) NOMINAL>SAFE(c1) -> **SAFE** | DEGRADED>SAFE(c1) discard:tc_mode_nominal -> **SAFE** |
| DEGRADED | 0 | tc_mode_nominal:1 fault_critical:2 | DEGRADED>NOMINAL(c1) NOMINAL>SAFE(c2) -> **SAFE** | DEGRADED>SAFE(c2) discard:tc_mode_nominal -> **SAFE** |
| DEGRADED | 0 | tc_mode_nominal:1 fault_noncritical:2 fault_critical:2 | DEGRADED>NOMINAL(c1) NOMINAL>DEGRADED(c2) DEGRADED>SAFE(c2) -> **SAFE** | DEGRADED>SAFE(c2) discard:tc_mode_nominal -> **SAFE** |
| DEGRADED | 0 | tc_mode_nominal:1 fault_critical:2 fault_noncritical:2 | DEGRADED>NOMINAL(c1) NOMINAL>SAFE(c2) -> **SAFE** | DEGRADED>SAFE(c2) discard:tc_mode_nominal -> **SAFE** |
| DEGRADED | 0 | fault_noncritical:2 tc_mode_nominal:1 fault_critical:2 | DEGRADED>NOMINAL(c1) NOMINAL>SAFE(c2) -> **SAFE** | DEGRADED>SAFE(c2) discard:tc_mode_nominal -> **SAFE** |
| ... | | 2 more in the CSV | | |

## same end state but different cause attribution

| Start | Inhibit | Events in arrival order (event:cause) | Hand written | Generated |
|---|---|---|---|---|
| NOMINAL | 0 | fault_noncritical:2 tc_mode_degraded:1 | NOMINAL>DEGRADED(c2) -> **DEGRADED** | NOMINAL>DEGRADED(c1) -> **DEGRADED** |
| NOMINAL | 1 | fault_noncritical:2 tc_mode_degraded:1 | refused(c2) NOMINAL>DEGRADED(c1) -> **DEGRADED** | NOMINAL>DEGRADED(c1) -> **DEGRADED** |
| DEGRADED | 1 | fault_noncritical:2 tc_mode_nominal:1 | DEGRADED>NOMINAL(c1) -> **NOMINAL** | DEGRADED>NOMINAL(c1) refused(c2) -> **NOMINAL** |
| DEGRADED | 0 | fault_critical:2 tc_mode_safe:1 | DEGRADED>SAFE(c2) -> **SAFE** | DEGRADED>SAFE(c1) -> **SAFE** |
| DEGRADED | 1 | fault_critical:2 tc_mode_safe:1 | refused(c2) DEGRADED>SAFE(c1) -> **SAFE** | DEGRADED>SAFE(c1) -> **SAFE** |
| DEGRADED | 0 | fault_noncritical:2 fault_critical:2 tc_mode_safe:1 | DEGRADED>SAFE(c2) -> **SAFE** | DEGRADED>SAFE(c1) -> **SAFE** |
| ... | | 5 more in the CSV | | |

## single-event mismatch

| Start | Inhibit | Events in arrival order (event:cause) | Hand written | Generated |
|---|---|---|---|---|
| NOMINAL | 0 | tc_mode_safe:1 | NOMINAL>SAFE(c1) -> **SAFE** | NOMINAL>SAFE(c1) -> **SAFE** |

## timing only (not a divergence)

| Start | Inhibit | Events in arrival order (event:cause) | Hand written | Generated |
|---|---|---|---|---|
| NOMINAL | 0 | tc_mode_degraded:1 fault_noncritical:2 | NOMINAL>DEGRADED(c1) -> **DEGRADED** | NOMINAL>DEGRADED(c1) -> **DEGRADED** |
| NOMINAL | 1 | tc_mode_degraded:1 fault_noncritical:2 | NOMINAL>DEGRADED(c1) -> **DEGRADED** | NOMINAL>DEGRADED(c1) -> **DEGRADED** |
| NOMINAL | 1 | tc_mode_degraded:1 fault_critical:2 | NOMINAL>DEGRADED(c1) refused(c2) -> **DEGRADED** | NOMINAL>DEGRADED(c1) refused(c2) -> **DEGRADED** |
| NOMINAL | 1 | fault_noncritical:2 fault_critical:2 | refused(c2) refused(c2) -> **NOMINAL** | refused(c2) refused(c2) -> **NOMINAL** |
| NOMINAL | 1 | fault_critical:2 tc_mode_degraded:1 | refused(c2) NOMINAL>DEGRADED(c1) -> **DEGRADED** | NOMINAL>DEGRADED(c1) refused(c2) -> **DEGRADED** |
| NOMINAL | 1 | fault_critical:2 fault_noncritical:2 | refused(c2) refused(c2) -> **NOMINAL** | refused(c2) refused(c2) -> **NOMINAL** |
| ... | | 32 more in the CSV | | |

