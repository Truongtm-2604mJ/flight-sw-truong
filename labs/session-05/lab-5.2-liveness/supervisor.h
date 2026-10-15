/*
 * Lab 5.2 - Task liveness supervision with bounded restart escalation.
 *
 * Each supervised task increments its own heartbeat counter once per
 * period. The supervisor runs at a longer period and compares each
 * counter with the value it saw at its previous check.
 *
 * Why a counter and not the "set a flag, supervisor clears it" scheme:
 * with a flag, both the task (set) and the supervisor (clear) write the
 * same word, and a heartbeat that lands between the supervisor's read
 * and its clear is lost, which is a false "missed heartbeat". With a
 * counter, each word has exactly one writer and there is nothing to
 * race on, on one CPU or several.
 *
 * Escalation policy (per task, all numbers set at init):
 *
 *   heartbeat missed  ->  event HEARTBEAT_MISSED
 *     restarts of this task in the last `window_ms` < `max_restarts`
 *         ->  level 1: restart the task, event TASK_RESTARTED
 *     otherwise
 *         ->  no further restart. Event TASK_ESCALATED, the task is
 *             marked FAILED (no longer supervised, so no event every
 *             cycle), and its escalation action is requested.
 *   FDIR actions inhibited by ground
 *         ->  detection and one event per episode, no action. If the
 *             inhibit is lifted while the task is still silent, the
 *             normal action is taken at the next check.
 *
 * Portable C99, no OS calls: restarting, escalating and reporting are
 * done through the callbacks in sup_ops_t.
 */
#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include <stdint.h>
#include <stddef.h>

#define SUP_MAX_TASKS     8u
#define SUP_MAX_RESTARTS  8u   /* upper bound on max_restarts */

/* Events (event IDs reported through sup_ops_t.event). */
#define SUP_EVT_HEARTBEAT_MISSED  0x0301u
#define SUP_EVT_TASK_RESTARTED    0x0302u
#define SUP_EVT_TASK_ESCALATED    0x0303u
#define SUP_EVT_ACTION_INHIBITED  0x0304u
#define SUP_EVT_TASK_RECOVERED    0x0305u  /* heartbeat back after an
                                              inhibited episode */

typedef enum {
    SUP_TASK_OK      = 0,
    SUP_TASK_MISSING = 1,  /* missed, no action taken (FDIR inhibited) */
    SUP_TASK_FAILED  = 2   /* escalated; no longer supervised          */
} sup_task_status_t;

typedef struct {
    const char       *name;
    uint32_t          period_ms;
    uint8_t           escalation;      /* passed to ops->escalate        */
    uint32_t          os_id;           /* for ops->restart (RTEMS id)    */
    volatile uint32_t heartbeat;       /* written by the task only       */
    uint32_t          last_seen;       /* written by the supervisor only */
    sup_task_status_t status;
    uint32_t          restart_ms[SUP_MAX_RESTARTS]; /* ring of restart times */
    uint8_t           restart_next;
    uint8_t           restarts_total;  /* saturating, for telemetry      */
    uint32_t          misses_total;
} sup_task_t;

typedef struct {
    /* Called in the supervisor's context. */
    int  (*restart)(void *arg, size_t idx, uint32_t os_id);
    void (*escalate)(void *arg, size_t idx, uint8_t escalation);
    void (*event)(void *arg, uint16_t event_id, size_t idx, uint32_t value);
    int  (*actions_inhibited)(void *arg);
} sup_ops_t;

typedef struct {
    sup_task_t      tasks[SUP_MAX_TASKS];
    size_t          count;
    uint32_t        check_period_ms;
    uint32_t        window_ms;
    uint8_t         max_restarts;
    const sup_ops_t *ops;
    void           *arg;
} supervisor_t;

/* Returns 0, or -1 if max_restarts is 0 or above SUP_MAX_RESTARTS. */
int sup_init(supervisor_t *s, uint32_t check_period_ms, uint32_t window_ms,
             uint8_t max_restarts, const sup_ops_t *ops, void *arg);

/* Register a task. Returns its index, or -1 if the table is full or the
 * task's period is not shorter than the check period (it could then
 * legitimately miss a whole check interval). */
int sup_register(supervisor_t *s, const char *name, uint32_t period_ms,
                 uint32_t os_id, uint8_t escalation);

/* Called by the supervised task once per period. */
static inline void sup_heartbeat(supervisor_t *s, size_t idx)
{
    s->tasks[idx].heartbeat = s->tasks[idx].heartbeat + 1u;
}

/* One supervision cycle. now_ms is a monotonic time in milliseconds
 * (wraps after 49 days; all comparisons are wrap safe). */
void sup_check(supervisor_t *s, uint32_t now_ms);

/* Restarts of task idx inside the window ending at now_ms. */
uint8_t sup_restarts_in_window(const supervisor_t *s, size_t idx,
                               uint32_t now_ms);

#endif /* SUPERVISOR_H */
