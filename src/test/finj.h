/*
 * Lab 5.4 - Fault injection interface. TEST BUILDS ONLY.
 *
 * The flight logic contains three kinds of injection point, written as
 * the macros below. In a flight build (FDIR_TEST_BUILD not defined) the
 * macros expand to the plain value or to nothing, this file's functions
 * are not compiled, and telecommand service 200 is not registered, so
 * nothing of the harness exists in the executable. `make check-flight`
 * in app/ verifies that by looking for finj_ symbols.
 *
 *   FINJ_PARAM(param_id, value)   replace a sampled parameter value
 *   FINJ_TASK_POINT(task_idx)     make a supervised task hang here
 *   FINJ_SUPERVISOR_POINT()       make the supervisor hang here
 *
 * A "hang" is a block on a semaphore nobody releases: the deadlock kind
 * of hang, which leaves the rest of the system running so the reaction
 * can be observed.
 *
 * Portable C99. The state is written by the telecommand task and read by
 * the injected tasks; every field is a single word.
 */
#ifndef FINJ_H
#define FINJ_H

#include <stdint.h>

#define FINJ_MAX_PARAMS 4u
#define FINJ_MAX_TASKS  8u

#ifdef FDIR_TEST_BUILD

void    finj_clear(void);
/* samples = 0: sustained until cleared. samples = 1: one sample spike. */
void    finj_param_override(uint16_t param_id, int32_t value, uint16_t samples);
/* The task hangs at its next injection point, `times` times in a row
 * (it hangs again after each restart, until the count is used up). */
void    finj_task_hang(uint8_t task_idx, uint8_t times);
void    finj_supervisor_hang(void);

int32_t finj_apply_param(uint16_t param_id, int32_t value);
int     finj_take_task_hang(uint8_t task_idx);
int     finj_take_supervisor_hang(void);

/* Provided by the application: block the calling task forever. */
void    finj_block_forever(void);

#define FINJ_PARAM(p, v)        finj_apply_param((p), (v))
#define FINJ_TASK_POINT(idx)    do { if (finj_take_task_hang(idx)) { finj_block_forever(); } } while (0)
#define FINJ_SUPERVISOR_POINT() do { if (finj_take_supervisor_hang()) { finj_block_forever(); } } while (0)

#else  /* flight build */

#define FINJ_PARAM(p, v)        (v)
#define FINJ_TASK_POINT(idx)    do { } while (0)
#define FINJ_SUPERVISOR_POINT() do { } while (0)

#endif /* FDIR_TEST_BUILD */

#endif /* FINJ_H */
