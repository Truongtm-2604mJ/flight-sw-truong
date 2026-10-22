# Coverage of the generated module mode_sm.c

Model sha256 `f82e7dfde38f18f4...`, measured with gcov over the whole Lab 6.3 suite (back-to-back sequences and requirement tests).

| Code | Statements | Branches taken |
|---|---|---|
| Flight code | 231/231 (100.0 %) | 52/55 (94.5 %) |
| Test-only names block (`MODE_SM_WITH_NAMES`, not in flight builds) | 8/15 | 6/11 |

## Uncovered items in flight code

| Line | Item | Model element | Classification |
|---|---|---|---|
| 231 | branch 3 | `select:T05` | unreachable by construction: T05 is tested only after T02 (same state NOMINAL, same event fault_critical, priority 2 < 5) has been found not enabled. With fault_critical pending, T02 not enabled means its guard `!fdir_inhibited` is false, so `fdir_inhibited` is true whenever this condition is evaluated (the inputs are a const copy taken at the start of step(), so they cannot change between the two evaluations): the false outcome of the guard term cannot occur. The guard is kept so each transition's condition reads as in the model and stays correct if priorities are reordered. |
| 233 | branch 3 | `select:T06` | unreachable by construction: T06 is tested only after T04 (same state NOMINAL, same event fault_noncritical, priority 4 < 6) has been found not enabled. With fault_noncritical pending, T04 not enabled means its guard `!fdir_inhibited` is false, so `fdir_inhibited` is true whenever this condition is evaluated (the inputs are a const copy taken at the start of step(), so they cannot change between the two evaluations): the false outcome of the guard term cannot occur. The guard is kept so each transition's condition reads as in the model and stays correct if priorities are reordered. |
| 250 | branch 3 | `select:T10` | unreachable by construction: T10 is tested only after T08 (same state DEGRADED, same event fault_critical, priority 2 < 4) has been found not enabled. With fault_critical pending, T08 not enabled means its guard `!fdir_inhibited` is false, so `fdir_inhibited` is true whenever this condition is evaluated (the inputs are a const copy taken at the start of step(), so they cannot change between the two evaluations): the false outcome of the guard term cannot occur. The guard is kept so each transition's condition reads as in the model and stays correct if priorities are reordered. |

Untested items: 0.

