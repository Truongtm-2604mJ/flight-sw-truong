#include "sensors5.h"

void sensors5_init(sensors5_t *s)
{
    s->step = 0;
    s->rng  = 0x5EEDu;
}

static int32_t noise(sensors5_t *s, int32_t range)
{
    s->rng = s->rng * 1103515245u + 12345u;
    return (int32_t)((s->rng >> 16) % (uint32_t)(2 * range + 1)) - range;
}

void sensors5_sample(sensors5_t *s, sensor5_values_t *out)
{
    uint32_t pos = s->step % 200u;                  /* triangle, 200 samples */
    int32_t tri = (pos < 100u) ? (int32_t)pos : (int32_t)(200u - pos);

    out->vbus_mv   = 28000 + noise(s, 100);
    out->tbat_cdeg = 2500 + tri * 10 + noise(s, 50);
    out->tobc_cdeg = 4000 + noise(s, 100);

    s->step++;
    if (s->step % SENS_VBUS_GLITCH_EVERY == 0u) {
        out->vbus_mv = 25500;
    }
    if (s->step % SENS_TBAT_GLITCH_EVERY == 0u) {
        out->tbat_cdeg = 4600;
    }
}
