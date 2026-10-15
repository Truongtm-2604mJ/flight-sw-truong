/*
 * Lab 5.2 - Watchdog model with an early warning stage.
 *
 * Behaves like a hardware watchdog with an early warning interrupt (the
 * STM32 window watchdog's EWI is the same idea): if it is not kicked for
 * `timeout_ms`, it first signals WDG_EARLY_WARNING, which gives the
 * system `grace_ms` to record the reset cause in non volatile memory,
 * and then WDG_RESET. Once the warning has fired the reset is committed:
 * a late kick does not cancel it, so a half working system cannot talk
 * its way out.
 *
 * wdg_tick() must be driven from a time source that does not depend on
 * any task being scheduled (on the target: a timer service routine in
 * clock tick interrupt context). Only the supervisor calls wdg_kick().
 *
 * Portable C99.
 */
#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

typedef enum {
    WDG_RUNNING = 0,
    WDG_WARNED  = 1,
    WDG_EXPIRED = 2
} wdg_state_t;

typedef enum {
    WDG_NONE          = 0,
    WDG_EARLY_WARNING = 1,
    WDG_RESET         = 2
} wdg_action_t;

typedef struct {
    uint32_t             timeout_ms;
    uint32_t             grace_ms;
    volatile uint32_t    since_kick_ms;
    uint32_t             since_warning_ms;
    volatile wdg_state_t state;
} wdg_t;

void wdg_init(wdg_t *w, uint32_t timeout_ms, uint32_t grace_ms);
void wdg_kick(wdg_t *w);
wdg_action_t wdg_tick(wdg_t *w, uint32_t elapsed_ms);

#endif /* WATCHDOG_H */
