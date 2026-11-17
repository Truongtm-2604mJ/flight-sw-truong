/* Lab 8.1 - Rate limited rejection reporting. See sec_report.h. */
#include "sec_report.h"

#include <string.h>

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

/* Authentication, replay and window rejections can be an attack: medium
 * severity (TM(5,3)). Noise and state: low (TM(5,2)). */
static uint8_t severity(size_t c)
{
    return (c == (size_t)SEC_REJ_AUTH || c == (size_t)SEC_REJ_REPLAY ||
            c == (size_t)SEC_REJ_WINDOW) ? 3u : 2u;
}

void sec_report_init(sec_report_t *r)
{
    (void)memset(r, 0, sizeof(*r));
}

void sec_report_cycle(sec_report_t *r, const uint32_t totals[SEC_REJ_CLASSES],
                      const uint8_t last_code[SEC_REJ_CLASSES], uint32_t now_ms,
                      sec_emit_fn emit, void *arg)
{
    size_t c;
    uint8_t aux[14];

    for (c = 0; c < (size_t)SEC_REJ_CLASSES; c++) {
        uint32_t delta = totals[c] - r->reported[c];   /* wraps correctly */
        r->reported[c] = totals[c];

        if (delta > 0u && !r->open[c]) {
            /* First rejection after a quiet period: report it now. */
            put16(&aux[0], SEC_EVT_REJECT_FIRST);
            aux[2] = (uint8_t)c;
            aux[3] = last_code[c];
            put32(&aux[4], totals[c]);
            emit(severity(c), aux, 8u, arg);
            r->events_emitted++;
            r->open[c] = 1u;
            r->window_start[c] = now_ms;
            r->in_window[c] = delta - 1u;
        } else if (delta > 0u) {
            r->in_window[c] += delta;
        } else {
            /* nothing new for this class (MISRA 15.7) */
        }

        if (r->open[c] && now_ms - r->window_start[c] >= SEC_REPORT_WINDOW_MS) {
            if (r->in_window[c] > 0u) {
                put16(&aux[0], SEC_EVT_REJECT_SUMMARY);
                aux[2] = (uint8_t)c;
                aux[3] = last_code[c];
                put32(&aux[4], r->in_window[c]);
                put32(&aux[8], totals[c]);
                put16(&aux[12], (now_ms - r->window_start[c]) / 1000u);
                emit(severity(c), aux, 14u, arg);
                r->events_emitted++;
                r->in_window[c] = 0u;
                r->window_start[c] = now_ms;    /* still active: new window */
            } else {
                r->open[c] = 0u;                /* a quiet window: close   */
            }
        }
    }
}
