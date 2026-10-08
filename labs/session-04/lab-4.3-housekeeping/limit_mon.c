#include "limit_mon.h"

void limit_mon_init(limit_mon_t *mon, int16_t limit, int16_t hysteresis)
{
    mon->limit      = limit;
    mon->hysteresis = hysteresis;
    mon->state      = LIMIT_MON_NOMINAL;
}

limit_mon_transition_t limit_mon_update(limit_mon_t *mon, int16_t value)
{
    /* int32 so limit - hysteresis cannot overflow int16. */
    int32_t rearm = (int32_t)mon->limit - (int32_t)mon->hysteresis;

    if (mon->state == LIMIT_MON_NOMINAL) {
        if (value > mon->limit) {
            mon->state = LIMIT_MON_HIGH;
            return LIMIT_MON_WENT_HIGH;
        }
    } else {
        if ((int32_t)value < rearm) {
            mon->state = LIMIT_MON_NOMINAL;
            return LIMIT_MON_WENT_NOMINAL;
        }
    }
    return LIMIT_MON_NO_CHANGE;
}
