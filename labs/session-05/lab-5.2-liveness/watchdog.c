/* Lab 5.2 - Watchdog model. See watchdog.h. */
#include "watchdog.h"

void wdg_init(wdg_t *w, uint32_t timeout_ms, uint32_t grace_ms)
{
    w->timeout_ms       = timeout_ms;
    w->grace_ms         = grace_ms;
    w->since_kick_ms    = 0;
    w->since_warning_ms = 0;
    w->state            = WDG_RUNNING;
}

void wdg_kick(wdg_t *w)
{
    /* A single aligned store. The tick runs in interrupt context and
     * cannot be preempted by this task, so no lock is needed. */
    if (w->state == WDG_RUNNING) {
        w->since_kick_ms = 0;
    }
}

wdg_action_t wdg_tick(wdg_t *w, uint32_t elapsed_ms)
{
    switch (w->state) {
    case WDG_RUNNING:
        w->since_kick_ms = w->since_kick_ms + elapsed_ms;
        if (w->since_kick_ms >= w->timeout_ms) {
            w->state = WDG_WARNED;
            w->since_warning_ms = 0;
            return WDG_EARLY_WARNING;
        }
        return WDG_NONE;

    case WDG_WARNED:
        w->since_warning_ms += elapsed_ms;
        if (w->since_warning_ms >= w->grace_ms) {
            w->state = WDG_EXPIRED;
            return WDG_RESET;
        }
        return WDG_NONE;

    case WDG_EXPIRED:
    default:
        return WDG_NONE;
    }
}
