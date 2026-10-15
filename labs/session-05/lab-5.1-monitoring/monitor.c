/* Lab 5.1 - Parameter monitoring. See monitor.h. */
#include "monitor.h"

#include <string.h>

void mon_define(monitor_entry_t *e, uint16_t param_id,
                int32_t limit_low, int32_t limit_low_clear,
                int32_t limit_high_clear, int32_t limit_high,
                uint16_t persistence, uint16_t event_id, uint8_t severity)
{
    memset(e, 0, sizeof(*e));
    e->param_id         = param_id;
    e->limit_low        = limit_low;
    e->limit_low_clear  = limit_low_clear;
    e->limit_high_clear = limit_high_clear;
    e->limit_high       = limit_high;
    e->persistence      = persistence;
    e->event_id         = event_id;
    e->severity         = severity;
    e->state            = MON_OK;
    e->side             = MON_SIDE_NONE;
    e->enabled          = 1u;
}

int mon_check_definition(const monitor_entry_t *e)
{
    if (e->persistence < 1u ||
        !(e->limit_low < e->limit_low_clear) ||
        !(e->limit_low_clear <= e->limit_high_clear) ||
        !(e->limit_high_clear < e->limit_high)) {
        return -1;
    }
    return 0;
}

static void reset_runtime(monitor_entry_t *e)
{
    e->state           = MON_OK;
    e->side            = MON_SIDE_NONE;
    e->violation_count = 0;
    e->clear_count     = 0;
}

mon_transition_t mon_update(monitor_entry_t *e, int32_t value)
{
    mon_side_t side;

    if (!e->enabled) {
        return MON_NO_CHANGE;
    }

    if (e->state == MON_ALARM) {
        /* Only a sample inside BOTH clear thresholds counts towards
         * leaving the alarm; anything else restarts the count. */
        if (value >= e->limit_low_clear && value <= e->limit_high_clear) {
            e->clear_count++;
            if (e->clear_count >= e->persistence) {
                reset_runtime(e);
                return MON_CLEARED;
            }
        } else {
            e->clear_count = 0;
        }
        return MON_NO_CHANGE;
    }

    /* OK or WARN: look for a violation of either limit. */
    if (value > e->limit_high) {
        side = MON_SIDE_HIGH;
    } else if (value < e->limit_low) {
        side = MON_SIDE_LOW;
    } else {
        side = MON_SIDE_NONE;
    }

    if (side == MON_SIDE_NONE) {
        /* Requirement 4: any in limit sample resets the counter. A run
         * of violations that ends here never became an alarm: count it,
         * so the rejection of transients is visible in telemetry. */
        if (e->violation_count > 0u) {
            e->filtered++;
        }
        e->violation_count = 0;
        e->state = MON_OK;
        e->side  = MON_SIDE_NONE;
        return MON_NO_CHANGE;
    }

    if (side != e->side && e->violation_count > 0u) {
        /* Jumped from one limit to the other: a new, separate run. */
        e->filtered++;
        e->violation_count = 0;
    }
    e->side = side;
    e->violation_count++;

    if (e->violation_count >= e->persistence) {
        e->state           = MON_ALARM;
        e->violation_count = 0;
        e->clear_count     = 0;
        return (side == MON_SIDE_HIGH) ? MON_RAISED_HIGH : MON_RAISED_LOW;
    }

    e->state = MON_WARN;
    return MON_NO_CHANGE;
}

void mon_set_enabled(monitor_entry_t *e, int enabled)
{
    reset_runtime(e);
    e->enabled = enabled ? 1u : 0u;
}

monitor_entry_t *mon_find(monitor_entry_t *table, size_t n, uint16_t param_id)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (table[i].param_id == param_id) {
            return &table[i];
        }
    }
    return NULL;
}

void mon_table_run(monitor_entry_t *table, size_t n, mon_get_fn get,
                   mon_transition_fn on_transition, void *ctx)
{
    size_t i;

    for (i = 0; i < n; i++) {
        int32_t value;
        mon_transition_t tr;

        if (!table[i].enabled) {
            continue;
        }
        value = get(table[i].param_id, ctx);
        tr = mon_update(&table[i], value);
        if (tr != MON_NO_CHANGE && on_transition != NULL) {
            on_transition(&table[i], tr, value, ctx);
        }
    }
}
