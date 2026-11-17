/*
 * Thermal recovery with bounded escalation (capstone, requirement FDIR-T1..T3).
 *
 * A temperature monitor in ALARM takes the vehicle to DEGRADED (the monitor
 * table's event action, Session 5). In DEGRADED this module then tries to
 * recover, at most THERM_MAX_ATTEMPTS times, THERM_PERIOD_MS apart:
 *
 *   alarm, DEGRADED, idle   -> attempt 1 now               THERM_ATTEMPT
 *   alarm, period elapsed   -> next attempt, or, after the
 *                              last one, give up           THERM_EXHAUSTED
 *   alarm gone              -> stop                        THERM_RECOVERED
 *   no longer DEGRADED      -> stop silently (the ground or another fault
 *                              changed the mode)
 *   after THERM_EXHAUSTED   -> nothing more until the vehicle has left
 *                              DEGRADED (the SAFE request takes effect at
 *                              the next mode cycle, up to 2 s later)
 *
 * On THERM_EXHAUSTED the caller requests SAFE (a critical autonomous fault
 * in the mode model, transition T08). A recovered vehicle stays DEGRADED:
 * only the ground leaves DEGRADED, as for every other fault.
 *
 * What a recovery attempt does on a real spacecraft (heater controller
 * reset, switch to the redundant thermistor, payload duty reduction) is
 * not modelled here: the attempt is an event and a counter, and the
 * simulated temperature does not respond to it. Portable C99, no RTEMS.
 */
#ifndef THERMAL_H
#define THERMAL_H

#include <stdint.h>

#define THERM_MAX_ATTEMPTS 3u
#define THERM_PERIOD_MS    10000u

typedef enum {
    THERM_NONE      = 0,
    THERM_ATTEMPT   = 1,
    THERM_RECOVERED = 2,
    THERM_EXHAUSTED = 3
} therm_action_t;

typedef struct {
    uint8_t  active;     /* a recovery sequence is running */
    uint8_t  attempts;   /* attempts made in this sequence */
    uint8_t  spent;      /* exhausted: no new sequence until the vehicle
                            leaves DEGRADED (SAFE is applied at the next
                            2 s mode cycle, not at once)  */
    uint32_t next_ms;    /* time of the next attempt       */
    uint32_t sequences;  /* sequences started (telemetry)  */
    uint32_t exhausted;  /* sequences that ended in SAFE   */
} thermal_t;

void thermal_init(thermal_t *t);

/* One step, after each monitoring cycle. alarm: a temperature monitor is
 * in ALARM; degraded: the vehicle is in DEGRADED. */
therm_action_t thermal_step(thermal_t *t, int alarm, int degraded, uint32_t now_ms);

#endif /* THERMAL_H */
