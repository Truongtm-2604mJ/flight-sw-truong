/* Lab 5.4 - Fault injection state. TEST BUILDS ONLY. See finj.h. */
#ifdef FDIR_TEST_BUILD

#include "finj.h"

#include <string.h>

typedef struct {
    volatile uint8_t  active;
    volatile uint16_t param_id;
    volatile int32_t  value;
    volatile uint16_t remaining;   /* 0 = sustained */
} finj_param_t;

static finj_param_t     params[FINJ_MAX_PARAMS];
static volatile uint8_t task_hang[FINJ_MAX_TASKS];
static volatile uint8_t supervisor_hang;

void finj_clear(void)
{
    size_t i;
    for (i = 0; i < FINJ_MAX_PARAMS; i++) {
        params[i].active = 0;
    }
    for (i = 0; i < FINJ_MAX_TASKS; i++) {
        task_hang[i] = 0;
    }
    supervisor_hang = 0;
}

void finj_param_override(uint16_t param_id, int32_t value, uint16_t samples)
{
    size_t i, slot = FINJ_MAX_PARAMS;
    for (i = 0; i < FINJ_MAX_PARAMS; i++) {
        if (params[i].active && params[i].param_id == param_id) {
            slot = i;
            break;
        }
        if (!params[i].active && slot == FINJ_MAX_PARAMS) {
            slot = i;
        }
    }
    if (slot == FINJ_MAX_PARAMS) {
        return;
    }
    params[slot].active    = 0;          /* publish the fields last */
    params[slot].param_id  = param_id;
    params[slot].value     = value;
    params[slot].remaining = samples;
    params[slot].active    = 1;
}

void finj_task_hang(uint8_t task_idx, uint8_t times)
{
    if (task_idx < FINJ_MAX_TASKS) {
        task_hang[task_idx] = times;
    }
}

void finj_supervisor_hang(void)
{
    supervisor_hang = 1;
}

int32_t finj_apply_param(uint16_t param_id, int32_t value)
{
    size_t i;
    for (i = 0; i < FINJ_MAX_PARAMS; i++) {
        if (params[i].active && params[i].param_id == param_id) {
            int32_t v = params[i].value;
            if (params[i].remaining > 0u) {
                params[i].remaining = (uint16_t)(params[i].remaining - 1u);
                if (params[i].remaining == 0u) {
                    params[i].active = 0;
                }
            }
            return v;
        }
    }
    return value;
}

int finj_take_task_hang(uint8_t task_idx)
{
    if (task_idx < FINJ_MAX_TASKS && task_hang[task_idx] > 0u) {
        task_hang[task_idx] = (uint8_t)(task_hang[task_idx] - 1u);
        return 1;
    }
    return 0;
}

int finj_take_supervisor_hang(void)
{
    if (supervisor_hang) {
        supervisor_hang = 0;
        return 1;
    }
    return 0;
}

#else
/* Flight build: nothing here. ISO C forbids an empty translation unit. */
typedef int finj_flight_build_is_empty;
#endif /* FDIR_TEST_BUILD */
