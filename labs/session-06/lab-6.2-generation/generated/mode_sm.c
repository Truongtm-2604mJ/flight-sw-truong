/*
 * GENERATED FILE - DO NOT EDIT.
 *
 * State machine implementation.
 * Model     : mode_machine.yaml ("vehicle_mode", version 3)
 * Model hash: sha256:46d8922ca14ac2917bc4bf7ba7d5c85a3dc0ad984d941a39f8f728d665cebf66
 * Generator : modelgen.py 1.1, sha256:358817262c6f4d3c3635a50f045af71b94bbf802d79d4401d16e733e0dddbb3f
 *
 * Change the model or the generator and regenerate. `make check-generated`
 * fails if this file differs from a fresh generation.
 */
#include <stdint.h>

#include "mode_sm.h"

#define BIT(e) ((uint32_t)1U << (uint32_t)(e))

/* Event classes (semantics: unconsumed_one_shot, unconsumed_latched). */
static const uint32_t ONE_SHOT = BIT(MODE_SM_EV_TC_MODE_NOMINAL) | BIT(MODE_SM_EV_TC_MODE_DEGRADED) | BIT(MODE_SM_EV_TC_MODE_SAFE) | BIT(MODE_SM_EV_FAULT_CLEARED);
static const uint32_t LATCHED = BIT(MODE_SM_EV_FAULT_NONCRITICAL) | BIT(MODE_SM_EV_FAULT_CRITICAL);
/* Ground commands (semantics: ignored_ground_command). */
static const uint32_t GROUND = BIT(MODE_SM_EV_TC_MODE_NOMINAL) | BIT(MODE_SM_EV_TC_MODE_DEGRADED) | BIT(MODE_SM_EV_TC_MODE_SAFE);

/* Events explicitly ignored, per state (completeness). */
static const uint32_t IGNORED[MODE_SM_STATE_COUNT] = {
    BIT(MODE_SM_EV_TC_MODE_NOMINAL) | BIT(MODE_SM_EV_FAULT_CLEARED),   /* NOMINAL */
    BIT(MODE_SM_EV_TC_MODE_DEGRADED) | BIT(MODE_SM_EV_FAULT_NONCRITICAL) | BIT(MODE_SM_EV_FAULT_CLEARED),   /* DEGRADED */
    BIT(MODE_SM_EV_TC_MODE_SAFE) | BIT(MODE_SM_EV_FAULT_CRITICAL) | BIT(MODE_SM_EV_FAULT_NONCRITICAL) | BIT(MODE_SM_EV_FAULT_CLEARED),   /* SAFE */
};

/* NOMINAL on_entry */
static void enter_NOMINAL(mode_sm_t *sm, const mode_sm_payload_t *cause)
{
    sm->state = MODE_SM_NOMINAL;
    mode_sm_action_payload_enable(sm->user, cause);
    mode_sm_action_hk_rate_normal(sm->user, cause);
}

/* DEGRADED on_entry */
static void enter_DEGRADED(mode_sm_t *sm, const mode_sm_payload_t *cause)
{
    sm->state = MODE_SM_DEGRADED;
    mode_sm_action_payload_suspend(sm->user, cause);
    mode_sm_action_hk_rate_high(sm->user, cause);
    mode_sm_action_monitors_tighten(sm->user, cause);
}

/* SAFE on_entry */
static void enter_SAFE(mode_sm_t *sm, const mode_sm_payload_t *cause)
{
    sm->state = MODE_SM_SAFE;
    mode_sm_action_payload_off(sm->user, cause);
    mode_sm_action_schedule_suspend(sm->user, cause);
    mode_sm_action_hk_minimal(sm->user, cause);
    mode_sm_action_monitors_tighten(sm->user, cause);
    mode_sm_action_sun_pointing(sm->user, cause);
    mode_sm_action_rx_guarantee(sm->user, cause);
}

/* SAFE on_exit */
static void exit_SAFE(mode_sm_t *sm, const mode_sm_payload_t *cause)
{
    mode_sm_action_sun_pointing_release(sm->user, cause);
}

/* T01: NOMINAL -> SAFE on tc_mode_safe [true], priority 1  MM-070 */
static void fire_T01(mode_sm_t *sm, mode_sm_result_t *r)
{
    const mode_sm_payload_t *cause = &sm->payload[MODE_SM_EV_TC_MODE_SAFE];
    enter_SAFE(sm, cause);
    mode_sm_on_transition(sm->user, MODE_SM_NOMINAL, MODE_SM_SAFE, MODE_SM_EV_TC_MODE_SAFE, cause);
    r->kind = MODE_SM_STEP_TRANSITION;
    r->transition = 1U;
    r->from = MODE_SM_NOMINAL;
    r->to = MODE_SM_SAFE;
    r->event = MODE_SM_EV_TC_MODE_SAFE;
}

/* T02: NOMINAL -> SAFE on fault_critical [!fdir_inhibited], priority 2  MM-040 */
static void fire_T02(mode_sm_t *sm, mode_sm_result_t *r)
{
    const mode_sm_payload_t *cause = &sm->payload[MODE_SM_EV_FAULT_CRITICAL];
    enter_SAFE(sm, cause);
    mode_sm_on_transition(sm->user, MODE_SM_NOMINAL, MODE_SM_SAFE, MODE_SM_EV_FAULT_CRITICAL, cause);
    r->kind = MODE_SM_STEP_TRANSITION;
    r->transition = 2U;
    r->from = MODE_SM_NOMINAL;
    r->to = MODE_SM_SAFE;
    r->event = MODE_SM_EV_FAULT_CRITICAL;
}

/* T03: NOMINAL -> DEGRADED on tc_mode_degraded [true], priority 3  MM-070 */
static void fire_T03(mode_sm_t *sm, mode_sm_result_t *r)
{
    const mode_sm_payload_t *cause = &sm->payload[MODE_SM_EV_TC_MODE_DEGRADED];
    enter_DEGRADED(sm, cause);
    mode_sm_on_transition(sm->user, MODE_SM_NOMINAL, MODE_SM_DEGRADED, MODE_SM_EV_TC_MODE_DEGRADED, cause);
    r->kind = MODE_SM_STEP_TRANSITION;
    r->transition = 3U;
    r->from = MODE_SM_NOMINAL;
    r->to = MODE_SM_DEGRADED;
    r->event = MODE_SM_EV_TC_MODE_DEGRADED;
}

/* T04: NOMINAL -> DEGRADED on fault_noncritical [!fdir_inhibited], priority 4  MM-040 */
static void fire_T04(mode_sm_t *sm, mode_sm_result_t *r)
{
    const mode_sm_payload_t *cause = &sm->payload[MODE_SM_EV_FAULT_NONCRITICAL];
    enter_DEGRADED(sm, cause);
    mode_sm_on_transition(sm->user, MODE_SM_NOMINAL, MODE_SM_DEGRADED, MODE_SM_EV_FAULT_NONCRITICAL, cause);
    r->kind = MODE_SM_STEP_TRANSITION;
    r->transition = 4U;
    r->from = MODE_SM_NOMINAL;
    r->to = MODE_SM_DEGRADED;
    r->event = MODE_SM_EV_FAULT_NONCRITICAL;
}

/* T05: NOMINAL (internal) on fault_critical [fdir_inhibited], priority 5  MM-060 */
static void fire_T05(mode_sm_t *sm, mode_sm_result_t *r)
{
    const mode_sm_payload_t *cause = &sm->payload[MODE_SM_EV_FAULT_CRITICAL];
    mode_sm_action_report_action_inhibited(sm->user, cause);
    r->kind = MODE_SM_STEP_INTERNAL;
    r->transition = 5U;
    r->from = MODE_SM_NOMINAL;
    r->to = MODE_SM_NOMINAL;
    r->event = MODE_SM_EV_FAULT_CRITICAL;
}

/* T06: NOMINAL (internal) on fault_noncritical [fdir_inhibited], priority 6  MM-060 */
static void fire_T06(mode_sm_t *sm, mode_sm_result_t *r)
{
    const mode_sm_payload_t *cause = &sm->payload[MODE_SM_EV_FAULT_NONCRITICAL];
    mode_sm_action_report_action_inhibited(sm->user, cause);
    r->kind = MODE_SM_STEP_INTERNAL;
    r->transition = 6U;
    r->from = MODE_SM_NOMINAL;
    r->to = MODE_SM_NOMINAL;
    r->event = MODE_SM_EV_FAULT_NONCRITICAL;
}

/* T07: DEGRADED -> SAFE on tc_mode_safe [true], priority 1  MM-070 */
static void fire_T07(mode_sm_t *sm, mode_sm_result_t *r)
{
    const mode_sm_payload_t *cause = &sm->payload[MODE_SM_EV_TC_MODE_SAFE];
    enter_SAFE(sm, cause);
    mode_sm_on_transition(sm->user, MODE_SM_DEGRADED, MODE_SM_SAFE, MODE_SM_EV_TC_MODE_SAFE, cause);
    r->kind = MODE_SM_STEP_TRANSITION;
    r->transition = 7U;
    r->from = MODE_SM_DEGRADED;
    r->to = MODE_SM_SAFE;
    r->event = MODE_SM_EV_TC_MODE_SAFE;
}

/* T08: DEGRADED -> SAFE on fault_critical [!fdir_inhibited], priority 2  MM-040 */
static void fire_T08(mode_sm_t *sm, mode_sm_result_t *r)
{
    const mode_sm_payload_t *cause = &sm->payload[MODE_SM_EV_FAULT_CRITICAL];
    enter_SAFE(sm, cause);
    mode_sm_on_transition(sm->user, MODE_SM_DEGRADED, MODE_SM_SAFE, MODE_SM_EV_FAULT_CRITICAL, cause);
    r->kind = MODE_SM_STEP_TRANSITION;
    r->transition = 8U;
    r->from = MODE_SM_DEGRADED;
    r->to = MODE_SM_SAFE;
    r->event = MODE_SM_EV_FAULT_CRITICAL;
}

/* T09: DEGRADED -> NOMINAL on tc_mode_nominal [true], priority 3  MM-010 */
static void fire_T09(mode_sm_t *sm, mode_sm_result_t *r)
{
    const mode_sm_payload_t *cause = &sm->payload[MODE_SM_EV_TC_MODE_NOMINAL];
    enter_NOMINAL(sm, cause);
    mode_sm_on_transition(sm->user, MODE_SM_DEGRADED, MODE_SM_NOMINAL, MODE_SM_EV_TC_MODE_NOMINAL, cause);
    r->kind = MODE_SM_STEP_TRANSITION;
    r->transition = 9U;
    r->from = MODE_SM_DEGRADED;
    r->to = MODE_SM_NOMINAL;
    r->event = MODE_SM_EV_TC_MODE_NOMINAL;
}

/* T10: DEGRADED (internal) on fault_critical [fdir_inhibited], priority 4  MM-060 */
static void fire_T10(mode_sm_t *sm, mode_sm_result_t *r)
{
    const mode_sm_payload_t *cause = &sm->payload[MODE_SM_EV_FAULT_CRITICAL];
    mode_sm_action_report_action_inhibited(sm->user, cause);
    r->kind = MODE_SM_STEP_INTERNAL;
    r->transition = 10U;
    r->from = MODE_SM_DEGRADED;
    r->to = MODE_SM_DEGRADED;
    r->event = MODE_SM_EV_FAULT_CRITICAL;
}

/* T11: SAFE -> DEGRADED on tc_mode_degraded [true], priority 1  MM-010 */
static void fire_T11(mode_sm_t *sm, mode_sm_result_t *r)
{
    const mode_sm_payload_t *cause = &sm->payload[MODE_SM_EV_TC_MODE_DEGRADED];
    exit_SAFE(sm, cause);
    enter_DEGRADED(sm, cause);
    mode_sm_on_transition(sm->user, MODE_SM_SAFE, MODE_SM_DEGRADED, MODE_SM_EV_TC_MODE_DEGRADED, cause);
    r->kind = MODE_SM_STEP_TRANSITION;
    r->transition = 11U;
    r->from = MODE_SM_SAFE;
    r->to = MODE_SM_DEGRADED;
    r->event = MODE_SM_EV_TC_MODE_DEGRADED;
}

/* T12: SAFE -> NOMINAL on tc_mode_nominal [true], priority 2  MM-010 */
static void fire_T12(mode_sm_t *sm, mode_sm_result_t *r)
{
    const mode_sm_payload_t *cause = &sm->payload[MODE_SM_EV_TC_MODE_NOMINAL];
    exit_SAFE(sm, cause);
    enter_NOMINAL(sm, cause);
    mode_sm_on_transition(sm->user, MODE_SM_SAFE, MODE_SM_NOMINAL, MODE_SM_EV_TC_MODE_NOMINAL, cause);
    r->kind = MODE_SM_STEP_TRANSITION;
    r->transition = 12U;
    r->from = MODE_SM_SAFE;
    r->to = MODE_SM_NOMINAL;
    r->event = MODE_SM_EV_TC_MODE_NOMINAL;
}

/* NOMINAL: outgoing transitions in priority order. */
static void select_NOMINAL(mode_sm_t *sm, uint32_t pending,
                        const mode_sm_inputs_t *in, mode_sm_result_t *r)
{
    if ((pending & BIT(MODE_SM_EV_TC_MODE_SAFE)) != 0U) {   /* T01 */
        fire_T01(sm, r);
    } else if ((pending & BIT(MODE_SM_EV_FAULT_CRITICAL)) != 0U && (!in->fdir_inhibited)) {   /* T02 */
        fire_T02(sm, r);
    } else if ((pending & BIT(MODE_SM_EV_TC_MODE_DEGRADED)) != 0U) {   /* T03 */
        fire_T03(sm, r);
    } else if ((pending & BIT(MODE_SM_EV_FAULT_NONCRITICAL)) != 0U && (!in->fdir_inhibited)) {   /* T04 */
        fire_T04(sm, r);
    } else if ((pending & BIT(MODE_SM_EV_FAULT_CRITICAL)) != 0U && in->fdir_inhibited) {   /* T05 */
        fire_T05(sm, r);
    } else if ((pending & BIT(MODE_SM_EV_FAULT_NONCRITICAL)) != 0U && in->fdir_inhibited) {   /* T06 */
        fire_T06(sm, r);
    } else {
        /* nothing enabled */
    }
}

/* DEGRADED: outgoing transitions in priority order. */
static void select_DEGRADED(mode_sm_t *sm, uint32_t pending,
                        const mode_sm_inputs_t *in, mode_sm_result_t *r)
{
    if ((pending & BIT(MODE_SM_EV_TC_MODE_SAFE)) != 0U) {   /* T07 */
        fire_T07(sm, r);
    } else if ((pending & BIT(MODE_SM_EV_FAULT_CRITICAL)) != 0U && (!in->fdir_inhibited)) {   /* T08 */
        fire_T08(sm, r);
    } else if ((pending & BIT(MODE_SM_EV_TC_MODE_NOMINAL)) != 0U) {   /* T09 */
        fire_T09(sm, r);
    } else if ((pending & BIT(MODE_SM_EV_FAULT_CRITICAL)) != 0U && in->fdir_inhibited) {   /* T10 */
        fire_T10(sm, r);
    } else {
        /* nothing enabled */
    }
}

/* SAFE: outgoing transitions in priority order. */
static void select_SAFE(mode_sm_t *sm, uint32_t pending,
                        const mode_sm_inputs_t *in, mode_sm_result_t *r)
{
    (void)in;
    if ((pending & BIT(MODE_SM_EV_TC_MODE_DEGRADED)) != 0U) {   /* T11 */
        fire_T11(sm, r);
    } else if ((pending & BIT(MODE_SM_EV_TC_MODE_NOMINAL)) != 0U) {   /* T12 */
        fire_T12(sm, r);
    } else {
        /* nothing enabled */
    }
}

int mode_sm_init(mode_sm_t *sm, mode_sm_state_t boot, void *user)
{
    static const mode_sm_payload_t none = { 0U, 0U };
    uint32_t i;

    sm->pending = 0U;
    sm->user = user;
    for (i = 0U; i < (uint32_t)MODE_SM_EVENT_COUNT; i++) {
        sm->payload[i] = none;
    }
    switch ((uint32_t)boot) {
    case (uint32_t)MODE_SM_NOMINAL:
        enter_NOMINAL(sm, &none);
        return 0;
    case (uint32_t)MODE_SM_SAFE:
        enter_SAFE(sm, &none);
        return 0;
    default:
        enter_SAFE(sm, &none);
        return -1;
    }
}

int mode_sm_post(mode_sm_t *sm, mode_sm_event_t event, mode_sm_payload_t cause)
{
    if ((uint32_t)event >= (uint32_t)MODE_SM_EVENT_COUNT) {
        return -1;
    }
    sm->payload[event] = cause;
    sm->pending |= BIT(event);
    return 0;
}

mode_sm_state_t mode_sm_state(const mode_sm_t *sm)
{
    return sm->state;
}

mode_sm_result_t mode_sm_step(mode_sm_t *sm, const mode_sm_inputs_t *in_now)
{
    static const mode_sm_payload_t recovery = { 0U, 0U };
    mode_sm_result_t r = { MODE_SM_STEP_IDLE, 0U, MODE_SM_NOMINAL, MODE_SM_NOMINAL, MODE_SM_EV_TC_MODE_NOMINAL };
    /* semantics: input_sampling = start_of_cycle */
    const uint32_t pending = sm->pending;
    const mode_sm_inputs_t in = *in_now;
    const uint32_t start = (uint32_t)sm->state;
    uint32_t consumed = 0U;
    uint32_t lost;
    uint32_t e;

    r.from = sm->state;
    r.to = sm->state;

    /* semantics: one transition per cycle, lowest priority number wins */
    switch (start) {
    case (uint32_t)MODE_SM_NOMINAL:
        select_NOMINAL(sm, pending, &in, &r);
        break;
    case (uint32_t)MODE_SM_DEGRADED:
        select_DEGRADED(sm, pending, &in, &r);
        break;
    case (uint32_t)MODE_SM_SAFE:
        select_SAFE(sm, pending, &in, &r);
        break;
    default:
        /* State variable outside the model (memory corruption): recover
         * to SAFE. Latched events are kept, one shot events dropped. */
        mode_sm_on_invalid_state(sm->user, start);
        sm->pending &= LATCHED;
        enter_SAFE(sm, &recovery);
        r.kind = MODE_SM_STEP_RECOVERED;
        r.to = MODE_SM_SAFE;
        return r;
    }

    if (r.kind != MODE_SM_STEP_IDLE) {
        consumed = BIT(r.event);
    }
    /* One shot events are consumed every cycle. Those neither taken
     * nor ignored in the start state were lost to a higher priority
     * transition: reported (semantics: unconsumed_one_shot). */
    lost = pending & ONE_SHOT & ~consumed & ~IGNORED[start];
    /* semantics: ignored_ground_command = report_if_state_changed. A
     * ground command for the start mode is already satisfied, unless an
     * external transition left that mode in this cycle (D-05). */
    if (r.kind == MODE_SM_STEP_TRANSITION) {
        lost |= pending & GROUND & ~consumed;
    }
    consumed |= pending & ONE_SHOT;
    /* Latched events ignored in the start state are dropped only if
     * nothing fired; otherwise they are re-evaluated in the new state. */
    if (r.kind == MODE_SM_STEP_IDLE) {
        consumed |= pending & LATCHED & IGNORED[start];
    }
    sm->pending &= ~consumed;

    for (e = 0U; e < (uint32_t)MODE_SM_EVENT_COUNT; e++) {
        if ((lost & BIT(e)) != 0U) {
            mode_sm_on_discard(sm->user, (mode_sm_event_t)e, &sm->payload[e]);
        }
    }
    return r;
}

#ifdef MODE_SM_WITH_NAMES
const char *mode_sm_state_name(mode_sm_state_t s)
{
    switch ((uint32_t)s) {
    case (uint32_t)MODE_SM_NOMINAL: return "NOMINAL";
    case (uint32_t)MODE_SM_DEGRADED: return "DEGRADED";
    case (uint32_t)MODE_SM_SAFE: return "SAFE";
    default: return "?";
    }
}

const char *mode_sm_event_name(mode_sm_event_t e)
{
    switch ((uint32_t)e) {
    case (uint32_t)MODE_SM_EV_TC_MODE_NOMINAL: return "tc_mode_nominal";
    case (uint32_t)MODE_SM_EV_TC_MODE_DEGRADED: return "tc_mode_degraded";
    case (uint32_t)MODE_SM_EV_TC_MODE_SAFE: return "tc_mode_safe";
    case (uint32_t)MODE_SM_EV_FAULT_NONCRITICAL: return "fault_noncritical";
    case (uint32_t)MODE_SM_EV_FAULT_CRITICAL: return "fault_critical";
    case (uint32_t)MODE_SM_EV_FAULT_CLEARED: return "fault_cleared";
    default: return "?";
    }
}
#endif
