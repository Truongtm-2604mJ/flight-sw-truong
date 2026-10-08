/*
 * Lab 4.3 - Simulated sensors.
 *
 * Deterministic, integer only stand-ins for the acquisition hardware.
 * The battery temperature is built to exercise the limit monitor: a
 * slow triangle wave with noise larger than its per-sample slope, so it
 * crosses the limit several times in a row on the way up and down.
 *
 *   battery temperature  triangle 42.00 .. 48.00 C, 0.20 C per sample
 *                        (period 60 samples = 30 s at 2 Hz), starting
 *                        at 43.50 C rising, noise +/- 0.80 C
 *   OBC temperature      sawtooth 25.00 .. 34.90 C, 0.10 C per sample
 *   bus voltage          28000 mV +/- 150 mV ripple
 */
#ifndef SENSORS_SIM_H
#define SENSORS_SIM_H

#include <stdint.h>

typedef struct {
    uint32_t step;      /* number of samples taken so far */
    uint32_t noise;     /* pseudo random generator state  */
} sensors_sim_t;

typedef struct {
    uint16_t vbus_mv;       /* bus voltage, mV                 */
    int16_t  t_bat_cdeg;    /* battery temperature, 0.01 deg C */
    int16_t  t_obc_cdeg;    /* OBC temperature, 0.01 deg C     */
} sensor_values_t;

void sensors_sim_init(sensors_sim_t *sim);
void sensors_sim_sample(sensors_sim_t *sim, sensor_values_t *out);

#endif /* SENSORS_SIM_H */
