/*
 * Vehicle mode types shared by the FDIR glue, the services and the tests.
 *
 * Capstone integration: these types used to live in the hand written mode
 * manager (labs/session-05/lab-5.3-safe-mode/modes.h). The integrated
 * application flies the GENERATED mode machine (mode_sm.c, from the
 * Session 6 model), so modes.c is not part of src/; only its types remain.
 */
#ifndef MODE_TYPES_H
#define MODE_TYPES_H

#include <stdint.h>

typedef enum {
    MODE_NOMINAL  = 0,
    MODE_DEGRADED = 1,
    MODE_SAFE     = 2
} vmode_t;

/* Who or what caused a transition. detail: parameter ID for a monitor,
 * task index for liveness, 0 otherwise. */
typedef enum {
    CAUSE_BOOT           = 0,
    CAUSE_GROUND         = 1,
    CAUSE_MONITOR        = 2,
    CAUSE_LIVENESS       = 3,
    CAUSE_WATCHDOG_RESET = 4
} cause_src_t;

typedef enum {
    MODE_CHANGED         = 0,
    MODE_NO_CHANGE       = 1,
    MODE_REFUSED_INHIBIT = 2
} mode_result_t;

#define MODE_INHIBIT_MAX_S  3600u

typedef enum {
    INHIBIT_BY_GROUND  = 1,
    INHIBIT_BY_TIMEOUT = 2
} inhibit_reason_t;

#endif /* MODE_TYPES_H */
