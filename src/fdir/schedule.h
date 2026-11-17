/*
 * Lab 5.3 - Minimal onboard time based command schedule (the idea of
 * PUS service 11): telecommands stored onboard with a release time.
 *
 * It exists here for one reason: SAFE must be able to suspend it, so a
 * command queued while the vehicle was healthy cannot fire into a
 * vehicle that no longer is. Suspension disables release; the stored
 * activities are kept. Leaving SAFE does NOT re-enable the schedule:
 * the ground re-enables it explicitly (or resets it) once it has looked
 * at what is queued. Activities that became due while suspended are
 * released when the schedule is re-enabled, oldest first.
 *
 * Portable C99. Not thread safe by itself.
 */
#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <stdint.h>
#include <stddef.h>

#define SCHED_MAX_ENTRIES  8u
#define SCHED_MAX_TC       120u

typedef struct {
    uint32_t release_s;
    uint16_t len;
    uint8_t  used;
    uint8_t  tc[SCHED_MAX_TC];
} sched_entry_t;

typedef struct {
    sched_entry_t     e[SCHED_MAX_ENTRIES];
    volatile uint8_t  enabled;
} sched_t;

typedef void (*sched_release_fn)(const uint8_t *tc, size_t len, void *arg);

void   sched_init(sched_t *s);           /* empty, enabled            */
int    sched_insert(sched_t *s, uint32_t release_s,
                    const uint8_t *tc, size_t len);   /* 0, -1 full, -2 size */
void   sched_reset(sched_t *s);          /* delete every activity     */
void   sched_set_enabled(sched_t *s, int enabled);
size_t sched_count(const sched_t *s);

/* Release every activity due at now_s (only if enabled), oldest first.
 * Returns the number released. */
size_t sched_release_due(sched_t *s, uint32_t now_s, sched_release_fn fn,
                         void *arg);

#endif /* SCHEDULE_H */
