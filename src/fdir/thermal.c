/* Thermal recovery with bounded escalation. See thermal.h. */
#include "thermal.h"

#include <string.h>

void thermal_init(thermal_t *t)
{
    (void)memset(t, 0, sizeof(*t));
}

therm_action_t thermal_step(thermal_t *t, int alarm, int degraded, uint32_t now_ms)
{
    if (!degraded) {
        t->spent = 0u;           /* a new DEGRADED episode may try again */
    }
    if (!t->active) {
        if (alarm && degraded && !t->spent) {
            t->active = 1u;
            t->attempts = 1u;
            t->next_ms = now_ms + THERM_PERIOD_MS;
            t->sequences++;
            return THERM_ATTEMPT;
        }
        return THERM_NONE;
    }
    if (!degraded) {
        t->active = 0u;          /* the mode changed under us: stand down */
        return THERM_NONE;
    }
    if (!alarm) {
        t->active = 0u;
        return THERM_RECOVERED;
    }
    if ((int32_t)(now_ms - t->next_ms) < 0) {
        return THERM_NONE;       /* wait for the next attempt time */
    }
    if (t->attempts < THERM_MAX_ATTEMPTS) {
        t->attempts++;
        t->next_ms = now_ms + THERM_PERIOD_MS;
        return THERM_ATTEMPT;
    }
    t->active = 0u;
    t->spent = 1u;
    t->exhausted++;
    return THERM_EXHAUSTED;
}
