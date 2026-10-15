/* Lab 5.3 - Vehicle mode management. See modes.h. */
#include "modes.h"

#include <string.h>

int mode_is_autonomous(cause_src_t src)
{
    return src == CAUSE_MONITOR || src == CAUSE_LIVENESS;
}

void mode_init(mode_mgr_t *m, vmode_t mode, cause_src_t src, uint16_t detail,
               const mode_ops_t *ops, void *arg)
{
    memset(m, 0, sizeof(*m));
    m->mode        = mode;
    m->last_src    = src;
    m->last_detail = detail;
    m->ops         = ops;
    m->arg         = arg;
    if (ops->apply != NULL) {
        ops->apply(arg, mode);
    }
}

mode_result_t mode_request(mode_mgr_t *m, vmode_t target, cause_src_t src,
                           uint16_t detail, uint32_t now_ms)
{
    vmode_t from = m->mode;

    if (mode_is_autonomous(src)) {
        if (target <= from) {
            return MODE_NO_CHANGE;       /* never back, never sideways */
        }
        if (m->inhibited) {
            return MODE_REFUSED_INHIBIT;
        }
    } else if (target == from) {
        return MODE_NO_CHANGE;
    }

    m->mode           = target;
    m->last_src       = src;
    m->last_detail    = detail;
    m->last_change_ms = now_ms;
    m->transitions++;

    /* Entry actions first, then the report, so the report describes a
     * configuration that is already in place. */
    if (m->ops->apply != NULL) {
        m->ops->apply(m->arg, target);
    }
    if (m->ops->transition != NULL) {
        m->ops->transition(m->arg, from, target, src, detail);
    }
    return MODE_CHANGED;
}

int mode_set_inhibit(mode_mgr_t *m, int inhibit, uint16_t duration_s,
                     uint32_t now_ms)
{
    if (inhibit) {
        if (duration_s == 0u || duration_s > MODE_INHIBIT_MAX_S) {
            return -1;
        }
        m->inhibit_until_ms = now_ms + (uint32_t)duration_s * 1000u;
        m->inhibited = 1u;
    } else {
        m->inhibited = 0u;
        duration_s = 0;
    }
    if (m->ops->inhibit != NULL) {
        m->ops->inhibit(m->arg, inhibit, duration_s, INHIBIT_BY_GROUND);
    }
    return 0;
}

void mode_tick(mode_mgr_t *m, uint32_t now_ms)
{
    /* Wrap safe "now >= until". */
    if (m->inhibited && (int32_t)(now_ms - m->inhibit_until_ms) >= 0) {
        m->inhibited = 0u;
        if (m->ops->inhibit != NULL) {
            m->ops->inhibit(m->arg, 0, 0, INHIBIT_BY_TIMEOUT);
        }
    }
}

uint16_t mode_inhibit_remaining_s(const mode_mgr_t *m, uint32_t now_ms)
{
    int32_t left;
    if (!m->inhibited) {
        return 0;
    }
    left = (int32_t)(m->inhibit_until_ms - now_ms);
    if (left <= 0) {
        return 0;
    }
    return (uint16_t)(((uint32_t)left + 999u) / 1000u);
}
