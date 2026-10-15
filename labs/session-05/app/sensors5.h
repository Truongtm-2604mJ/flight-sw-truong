/*
 * Session 5 - Simulated sensors for a HEALTHY vehicle.
 *
 * Unlike the Lab 4.3 model (built to cross its limit), everything here
 * stays inside its limits, with two exceptions on purpose: natural
 * single sample glitches, the kind EMI or an ADC hiccup produces.
 *
 *   V_BUS   28.000 V +/- 0.100 V; every 137th sample one glitch to 25.5 V
 *           (below the 26.0 V low limit)
 *   T_BAT   triangle 25.00 .. 35.00 C, period 200 samples, +/- 0.50 C
 *           noise; every 211th sample one glitch to 46.00 C (above 45 C)
 *   T_OBC   40.00 C +/- 1.00 C
 *
 * The glitches are what makes the false positive measurement mean
 * something: they must be counted as filtered transients, never alarm.
 *
 * Deterministic (fixed seed), integer only. Portable C99.
 */
#ifndef SENSORS5_H
#define SENSORS5_H

#include <stdint.h>

#define SENS_VBUS_GLITCH_EVERY  137u
#define SENS_TBAT_GLITCH_EVERY  211u

typedef struct {
    uint32_t step;
    uint32_t rng;
} sensors5_t;

typedef struct {
    int32_t vbus_mv;
    int32_t tbat_cdeg;
    int32_t tobc_cdeg;
} sensor5_values_t;

void sensors5_init(sensors5_t *s);
void sensors5_sample(sensors5_t *s, sensor5_values_t *out);

#endif /* SENSORS5_H */
