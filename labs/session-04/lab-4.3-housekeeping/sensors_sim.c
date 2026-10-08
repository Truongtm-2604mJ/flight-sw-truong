#include "sensors_sim.h"

#define TBAT_MIN_CDEG    4200
#define TBAT_SPAN_CDEG   600    /* 42.00 .. 48.00 C */
#define TBAT_STEP_CDEG   20     /* 0.20 C per sample */
#define TBAT_START_CDEG  4350
#define TBAT_NOISE_CDEG  80     /* +/- 0.80 C */

void sensors_sim_init(sensors_sim_t *sim)
{
    sim->step  = 0;
    sim->noise = 2024u;
}

/* Linear congruential generator, returns a value in [-range, +range]. */
static int32_t noise(sensors_sim_t *sim, int32_t range)
{
    sim->noise = sim->noise * 1103515245u + 12345u;
    return (int32_t)((sim->noise >> 16) % (uint32_t)(2 * range + 1)) - range;
}

void sensors_sim_sample(sensors_sim_t *sim, sensor_values_t *out)
{
    /* Triangle wave: position runs 0..2*SPAN-1, folded at SPAN. */
    uint32_t period = 2u * TBAT_SPAN_CDEG;
    uint32_t pos = ((uint32_t)(TBAT_START_CDEG - TBAT_MIN_CDEG) +
                    sim->step * TBAT_STEP_CDEG) % period;
    int32_t tri = (pos < TBAT_SPAN_CDEG) ? (int32_t)pos
                                         : (int32_t)(period - pos);

    out->t_bat_cdeg = (int16_t)(TBAT_MIN_CDEG + tri +
                                noise(sim, TBAT_NOISE_CDEG));
    out->t_obc_cdeg = (int16_t)(2500 + (int32_t)((sim->step % 100u) * 10u));
    out->vbus_mv    = (uint16_t)(28000 + noise(sim, 150));

    sim->step++;
}
