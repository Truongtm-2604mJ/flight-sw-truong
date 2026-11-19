/* Lab 5.2 - Task liveness supervision. See supervisor.h. */
#include "supervisor.h"

#include <string.h>

int sup_init(supervisor_t *s, uint32_t check_period_ms, uint32_t window_ms,
             uint8_t max_restarts, const sup_ops_t *ops, void *arg)
{
    if (max_restarts == 0u || max_restarts > SUP_MAX_RESTARTS) {
        return -1;
    }
    memset(s, 0, sizeof(*s));
    s->check_period_ms = check_period_ms;
    s->window_ms       = window_ms;
    s->max_restarts    = max_restarts;
    s->ops             = ops;
    s->arg             = arg;
    return 0;
}

int sup_register(supervisor_t *s, const char *name, uint32_t period_ms,
                 uint32_t os_id, uint8_t escalation)
{
    sup_task_t *t;

    if (s->count >= SUP_MAX_TASKS || period_ms >= s->check_period_ms) {
        return -1;
    }
    t = &s->tasks[s->count];
    memset(t, 0, sizeof(*t));
    t->name       = name;
    t->period_ms  = period_ms;
    t->os_id      = os_id;
    t->escalation = escalation;
    t->status     = SUP_TASK_OK;
    return (int)s->count++;
}

uint8_t sup_restarts_in_window(const supervisor_t *s, size_t idx,
                               uint32_t now_ms)
{
    const sup_task_t *t = &s->tasks[idx];
    uint8_t n = 0;
    uint8_t i;
    uint8_t recorded = (t->restarts_total < s->max_restarts)
                       ? t->restarts_total : s->max_restarts;

    for (i = 0; i < recorded; i++) {
        /* Unsigned subtraction: correct across the 32 bit wrap. */
        if ((uint32_t)(now_ms - t->restart_ms[i]) < s->window_ms) {
            n++;
        }
    }
    return n;
}

static void event(supervisor_t *s, uint16_t id, size_t idx, uint32_t value)
{
    if (s->ops->event != NULL) {
        s->ops->event(s->arg, id, idx, value);
    }
}

void sup_check(supervisor_t *s, uint32_t now_ms)
{
    size_t i;

    for (i = 0; i < s->count; i++) {
        sup_task_t *t = &s->tasks[i];
        uint32_t hb = t->heartbeat;   /* one read of the task's word */
        uint8_t recent;
        int inhibited;

        if (t->status == SUP_TASK_FAILED) {
            continue;
        }

        if (hb != t->last_seen) {
            t->last_seen = hb;
            if (t->status == SUP_TASK_MISSING) {
                t->status = SUP_TASK_OK;
                event(s, SUP_EVT_TASK_RECOVERED, i, hb);
            }
            continue;
        }

        /* No heartbeat during a whole check interval. */
        inhibited = (s->ops->actions_inhibited != NULL &&
                     s->ops->actions_inhibited(s->arg));

        if (t->status == SUP_TASK_MISSING) {
            if (inhibited) {
                continue;   /* already reported once for this episode */
            }
            /* Inhibit lifted while the task is still silent: act now,
             * without reporting the same miss a second time. */
        } else {
            t->misses_total++;
            event(s, SUP_EVT_HEARTBEAT_MISSED, i, t->misses_total);
            if (inhibited) {
                t->status = SUP_TASK_MISSING;
                event(s, SUP_EVT_ACTION_INHIBITED, i, 1u);
                continue;
            }
        }
        t->status = SUP_TASK_OK;

        recent = sup_restarts_in_window(s, i, now_ms);
        if (recent >= s->max_restarts) {
            /* Bounded: do not restart again, escalate instead. */
            t->status = SUP_TASK_FAILED;
            event(s, SUP_EVT_TASK_ESCALATED, i, recent);
            if (s->ops->escalate != NULL) {
                s->ops->escalate(s->arg, i, t->escalation);
            }
            continue;
        }

        t->restart_ms[t->restart_next] = now_ms;
        t->restart_next = (uint8_t)((t->restart_next + 1u) % s->max_restarts);
        if (t->restarts_total < 255u) {
            t->restarts_total++;
        }
        if (s->ops->restart != NULL) {
            (void)s->ops->restart(s->arg, i, t->os_id);
        }
        event(s, SUP_EVT_TASK_RESTARTED, i, (uint32_t)recent + 1u);
    }
}
