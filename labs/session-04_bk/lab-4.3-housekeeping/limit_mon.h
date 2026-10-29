/*
 * Lab 4.3 - Upper limit monitor with hysteresis.
 *
 * Reports a transition, not a level: the caller gets LIMIT_MON_WENT_HIGH
 * once when the value rises above `limit`, and nothing more until the
 * value has fallen below `limit - hysteresis`, which re-arms the monitor
 * and returns LIMIT_MON_WENT_NOMINAL once.
 *
 *            value
 *   limit  ----------------x-x---x------------------   trip above here
 *                        x  x x x  x
 *   limit - hyst --------------------x--------------   re-arm below here
 *                      x               x
 *   state    NOMINAL  | HIGH           | NOMINAL
 *   event             ^ once           ^ once
 *
 * Between the two thresholds the state does not change, so noise smaller
 * than the hysteresis band cannot produce events.
 *
 * Portable C99, integer only, no OS calls.
 */
#ifndef LIMIT_MON_H
#define LIMIT_MON_H

#include <stdint.h>

typedef enum {
    LIMIT_MON_NOMINAL = 0,
    LIMIT_MON_HIGH    = 1
} limit_mon_state_t;

typedef enum {
    LIMIT_MON_NO_CHANGE    = 0,
    LIMIT_MON_WENT_HIGH    = 1,
    LIMIT_MON_WENT_NOMINAL = 2
} limit_mon_transition_t;

typedef struct {
    int16_t limit;       /* trip when value > limit                   */
    int16_t hysteresis;  /* re-arm when value < limit - hysteresis    */
    limit_mon_state_t state;
} limit_mon_t;

void limit_mon_init(limit_mon_t *mon, int16_t limit, int16_t hysteresis);

/* Feed one sample. Returns the transition this sample caused, if any. */
limit_mon_transition_t limit_mon_update(limit_mon_t *mon, int16_t value);

#endif /* LIMIT_MON_H */
