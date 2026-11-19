/*
 * Lab 8.1 - Rate limited, aggregated reporting of rejected telecommands.
 *
 * The telecommand path only counts (tc_auth_t.rej_total, one counter per
 * rejection class). This reporter runs in the housekeeping task, compares
 * the counters with what it has already reported, and emits events:
 *
 *   REJECT_FIRST    the first rejection of a class after a quiet period,
 *                   at once (the operators learn of an attack or a
 *                   misconfigured ground station within one HK cycle)
 *   REJECT_SUMMARY  at the end of each SEC_REPORT_WINDOW_MS window in which
 *                   more rejections of that class arrived: how many, and
 *                   the running total
 *
 * So per class, in any window, at most one FIRST and one SUMMARY: a burst
 * of any size costs at most 2 events per class per window, and a flood
 * lasting T seconds at most 1 + ceil(T / window) + 1 per class. The
 * downlink and the event store cannot be flooded by an attacker who does
 * not have the key, and no single rejection is answered on its own (no
 * oracle: the timing and content of events do not depend on what was
 * wrong with any one packet beyond its class).
 *
 * A class goes quiet again after one full window with no new rejection.
 */
#ifndef SEC_REPORT_H
#define SEC_REPORT_H

#include <stddef.h>
#include <stdint.h>

#include "tc_auth.h"

#define SEC_REPORT_WINDOW_MS 10000u

#define SEC_EVT_REJECT_FIRST   0x0601u
#define SEC_EVT_REJECT_SUMMARY 0x0602u

typedef struct {
    uint32_t reported[SEC_REJ_CLASSES];   /* totals already accounted for */
    uint32_t in_window[SEC_REJ_CLASSES];  /* after the FIRST, this window */
    uint32_t window_start[SEC_REJ_CLASSES];
    uint8_t  open[SEC_REJ_CLASSES];
    uint32_t events_emitted;
} sec_report_t;

/* severity (PUS 5 subtype 1..4), aux data (starts with the event ID) */
typedef void (*sec_emit_fn)(uint8_t severity, const uint8_t *aux, size_t len,
                            void *arg);

void sec_report_init(sec_report_t *r);

/* One reporter cycle. totals and last_code are a snapshot of tc_auth_t
 * taken under the tc lock; now_ms is a monotonic time. */
void sec_report_cycle(sec_report_t *r, const uint32_t totals[SEC_REJ_CLASSES],
                      const uint8_t last_code[SEC_REJ_CLASSES], uint32_t now_ms,
                      sec_emit_fn emit, void *arg);

#endif /* SEC_REPORT_H */
