/* Lab 5.3 - Onboard time based schedule. See schedule.h. */
#include "schedule.h"

#include <string.h>

void sched_init(sched_t *s)
{
    memset(s, 0, sizeof(*s));
    s->enabled = 1u;
}

int sched_insert(sched_t *s, uint32_t release_s, const uint8_t *tc,
                 size_t len)
{
    size_t i;
    if (tc == NULL || len == 0u || len > SCHED_MAX_TC) {
        return -2;
    }
    for (i = 0; i < SCHED_MAX_ENTRIES; i++) {
        if (!s->e[i].used) {
            s->e[i].used = 1u;
            s->e[i].release_s = release_s;
            s->e[i].len = (uint16_t)len;
            memcpy(s->e[i].tc, tc, len);
            return 0;
        }
    }
    return -1;
}

void sched_reset(sched_t *s)
{
    size_t i;
    for (i = 0; i < SCHED_MAX_ENTRIES; i++) {
        s->e[i].used = 0u;
    }
}

void sched_set_enabled(sched_t *s, int enabled)
{
    s->enabled = enabled ? 1u : 0u;
}

size_t sched_count(const sched_t *s)
{
    size_t i, n = 0;
    for (i = 0; i < SCHED_MAX_ENTRIES; i++) {
        n += s->e[i].used ? 1u : 0u;
    }
    return n;
}

size_t sched_release_due(sched_t *s, uint32_t now_s, sched_release_fn fn,
                         void *arg)
{
    size_t released = 0;

    while (s->enabled) {
        size_t i, best = SCHED_MAX_ENTRIES;
        for (i = 0; i < SCHED_MAX_ENTRIES; i++) {
            if (s->e[i].used && s->e[i].release_s <= now_s &&
                (best == SCHED_MAX_ENTRIES ||
                 s->e[i].release_s < s->e[best].release_s)) {
                best = i;
            }
        }
        if (best == SCHED_MAX_ENTRIES) {
            break;
        }
        /* Remove before releasing: a released command may itself touch
         * the schedule (disable it, insert a new activity). */
        {
            sched_entry_t e = s->e[best];
            s->e[best].used = 0u;
            fn(e.tc, e.len, arg);
        }
        released++;
    }
    return released;
}
