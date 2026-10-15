/*
 * Lab 5.1 - Table driven parameter monitoring with persistence and
 * hysteresis.
 *
 * One monitor_entry_t per monitored parameter. The first nine fields are
 * the structure given in the session notes; the fields after them are
 * additions this implementation needs (see each comment).
 *
 * State model:
 *
 *      in limit                 out of limit, < persistence
 *   +----------+  violation  +-----------+
 *   |   OK     |------------>|   WARN    |  (no event: not confirmed)
 *   +----------+<------------+-----------+
 *        ^       in limit          |  persistence consecutive
 *        |   (counted as a         |  violations, same side
 *        |    filtered transient)  v
 *        |                    +-----------+
 *        +--------------------|  ALARM    |  event on entry
 *          persistence        +-----------+
 *          consecutive samples
 *          inside the CLEAR thresholds: event on exit
 *
 *   violation high :  value >  limit_high
 *   violation low  :  value <  limit_low
 *   clear          :  limit_low_clear <= value <= limit_high_clear
 *
 * Between a limit and its clear threshold nothing changes state, so a
 * value sitting on a limit cannot make the monitor oscillate, and
 * clearing needs the same persistence as raising.
 *
 * Portable C99, integer only, no OS calls, no dynamic memory.
 */
#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>
#include <stddef.h>

typedef enum { MON_OK = 0, MON_WARN = 1, MON_ALARM = 2 } mon_state_t;

typedef enum {
    MON_SIDE_NONE = 0,
    MON_SIDE_HIGH = 1,
    MON_SIDE_LOW  = 2
} mon_side_t;

typedef enum {
    MON_NO_CHANGE   = 0,
    MON_RAISED_HIGH = 1,  /* entered ALARM on the high limit */
    MON_RAISED_LOW  = 2,  /* entered ALARM on the low limit  */
    MON_CLEARED     = 3   /* left ALARM, back to OK          */
} mon_transition_t;

typedef struct {
    uint16_t    param_id;
    int32_t     limit_high;       /* alarm threshold                   */
    int32_t     limit_high_clear; /* hysteresis, lower than above      */
    int32_t     limit_low;
    int32_t     limit_low_clear;  /* hysteresis, higher than above     */
    uint16_t    persistence;      /* consecutive samples required      */
    uint16_t    violation_count;  /* runtime counter                   */
    mon_state_t state;
    uint16_t    event_id;         /* raised on state transition        */

    /* --- additions ---------------------------------------------------- */
    uint8_t     enabled;          /* requirement 5: per parameter on/off */
    uint8_t     severity;         /* TM(5,x) subtype of the alarm event  */
    mon_side_t  side;             /* limit being violated / in alarm     */
    uint16_t    clear_count;      /* consecutive clear samples in ALARM  */
    uint32_t    filtered;         /* violations rejected by persistence  */
} monitor_entry_t;

/* Fill in an entry. Runtime fields start at zero, state OK, enabled. */
void mon_define(monitor_entry_t *e, uint16_t param_id,
                int32_t limit_low, int32_t limit_low_clear,
                int32_t limit_high_clear, int32_t limit_high,
                uint16_t persistence, uint16_t event_id, uint8_t severity);

/* 0 if the definition is consistent (low < low_clear <= high_clear <
 * high, persistence >= 1), -1 otherwise. Checked once at start up. */
int mon_check_definition(const monitor_entry_t *e);

/* Feed one sample. Returns the transition this sample caused. */
mon_transition_t mon_update(monitor_entry_t *e, int32_t value);

/* Enable or disable. Disabling forgets all runtime state (back to OK,
 * counters cleared) and raises no event; enabling starts from OK. */
void mon_set_enabled(monitor_entry_t *e, int enabled);

/* Find an entry by parameter ID, or NULL. */
monitor_entry_t *mon_find(monitor_entry_t *table, size_t n, uint16_t param_id);

/* Run every entry of a table once.
 *   get(param_id, ctx)                   returns the current value
 *   on_transition(entry, tr, value, ctx) called for each transition  */
typedef int32_t (*mon_get_fn)(uint16_t param_id, void *ctx);
typedef void (*mon_transition_fn)(const monitor_entry_t *e,
                                  mon_transition_t tr, int32_t value,
                                  void *ctx);

void mon_table_run(monitor_entry_t *table, size_t n, mon_get_fn get,
                   mon_transition_fn on_transition, void *ctx);

#endif /* MONITOR_H */
