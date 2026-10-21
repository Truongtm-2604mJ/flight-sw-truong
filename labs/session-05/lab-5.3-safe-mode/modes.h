/*
 * Lab 5.3 - Vehicle mode management and the FDIR inhibit.
 *
 *                      ground (any time)
 *        +-------------------------------------------------+
 *        |                                                 v
 *   +---------+  non critical fault  +----------+  critical fault,   +------+
 *   | NOMINAL |--------------------->| DEGRADED |------------------->| SAFE |
 *   +---------+  (autonomous)        +----------+  escalation,       +------+
 *     ^  ^  |                           |          ground            |  |
 *     |  |  +---------------------------+--------------------------->+  |
 *     |  |         critical fault / escalation / ground                  |
 *     |  +---------------- ground "NOMINAL" (only way out of SAFE) -------+
 *     +------------------- ground "NOMINAL" from DEGRADED
 *
 * Rules, enforced here and nowhere else:
 *   - Only CAUSE_GROUND is a privileged (ground) request. Every other
 *     cause value, known or not, is treated as autonomous: fail closed,
 *     so a new or corrupted cause can never de-escalate the vehicle or
 *     bypass the inhibit.
 *   - Autonomous requests only ever move towards SAFE:
 *     NOMINAL -> DEGRADED, NOMINAL/DEGRADED -> SAFE. Never back.
 *   - Only the ground leaves SAFE or DEGRADED. No timeout, no automatic
 *     recovery.
 *   - While the FDIR inhibit is set, autonomous requests are refused
 *     (the caller reports that). Ground requests are never inhibited.
 *   - The inhibit expires on its own after the duration given by the
 *     ground: an inhibit forgotten after loss of signal would leave the
 *     vehicle without autonomous protection until the next pass.
 *   - Every transition is reported with its cause.
 *
 * Not thread safe by itself: the application calls it under one mutex.
 * Portable C99.
 */
#ifndef MODES_H
#define MODES_H

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
    MODE_CHANGED        = 0,
    MODE_NO_CHANGE      = 1,  /* already there, or autonomous request
                                 that would not be more severe       */
    MODE_REFUSED_INHIBIT = 2  /* autonomous request while inhibited  */
} mode_result_t;

#define MODE_INHIBIT_MAX_S  3600u

typedef enum {
    INHIBIT_BY_GROUND  = 1,
    INHIBIT_BY_TIMEOUT = 2
} inhibit_reason_t;

typedef struct {
    /* Entry actions for the new mode (payload, schedule, telemetry). */
    void (*apply)(void *arg, vmode_t mode);
    /* Transition report. */
    void (*transition)(void *arg, vmode_t from, vmode_t to,
                       cause_src_t src, uint16_t detail);
    /* Inhibit set or cleared. */
    void (*inhibit)(void *arg, int inhibited, uint16_t duration_s,
                    inhibit_reason_t reason);
} mode_ops_t;

typedef struct {
    volatile vmode_t  mode;
    cause_src_t       last_src;
    uint16_t          last_detail;
    uint32_t          last_change_ms;
    uint16_t          transitions;
    volatile uint8_t  inhibited;
    uint32_t          inhibit_until_ms;
    const mode_ops_t *ops;
    void             *arg;
} mode_mgr_t;

/* Start in `mode` (NOMINAL on a cold boot, SAFE after an unplanned
 * reset). Calls apply() once; no transition event. */
void mode_init(mode_mgr_t *m, vmode_t mode, cause_src_t src, uint16_t detail,
               const mode_ops_t *ops, void *arg);

/* Request a mode. Only src == CAUSE_GROUND is a ground request; any
 * other value is autonomous. A target that is not a mode is ignored. */
mode_result_t mode_request(mode_mgr_t *m, vmode_t target, cause_src_t src,
                           uint16_t detail, uint32_t now_ms);

/* Ground sets the inhibit for duration_s (1..MODE_INHIBIT_MAX_S), or
 * clears it. Returns 0, or -1 for a duration out of range. */
int mode_set_inhibit(mode_mgr_t *m, int inhibit, uint16_t duration_s,
                     uint32_t now_ms);

/* Periodic: expires the inhibit. */
void mode_tick(mode_mgr_t *m, uint32_t now_ms);

/* Seconds of inhibit left (0 if not inhibited), for telemetry. */
uint16_t mode_inhibit_remaining_s(const mode_mgr_t *m, uint32_t now_ms);

int mode_is_ground(cause_src_t src);      /* src == CAUSE_GROUND only */
int mode_is_autonomous(cause_src_t src);  /* everything else           */

#endif /* MODES_H */
