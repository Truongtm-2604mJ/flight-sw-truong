/*
 * GENERATED FILE - DO NOT EDIT.
 *
 * Public interface of the state machine.
 * Model     : mode_machine.yaml ("vehicle_mode", version 2)
 * Model hash: sha256:f82e7dfde38f18f475df59611e585b20e3c4466247d1732833060af4f2dd56bb
 * Generator : modelgen.py 1.0, sha256:4e01934c54195d8ecf8660e6aafeb2bd7c644bfadfa0e7237c5db0c2597c9594
 *
 * Change the model or the generator and regenerate. `make check-generated`
 * fails if this file differs from a fresh generation.
 */
#ifndef MODE_SM_H
#define MODE_SM_H

#include <stdbool.h>
#include <stdint.h>

#define MODE_SM_MODEL_NAME    "vehicle_mode"
#define MODE_SM_MODEL_VERSION 2
#define MODE_SM_MODEL_SHA256  "f82e7dfde38f18f475df59611e585b20e3c4466247d1732833060af4f2dd56bb"

typedef enum {
    MODE_SM_NOMINAL = 0,
    MODE_SM_DEGRADED = 1,
    MODE_SM_SAFE = 2,
    MODE_SM_STATE_COUNT = 3
} mode_sm_state_t;

typedef enum {
    MODE_SM_EV_TC_MODE_NOMINAL = 0,   /* ground, one_shot */
    MODE_SM_EV_TC_MODE_DEGRADED = 1,   /* ground, one_shot */
    MODE_SM_EV_TC_MODE_SAFE = 2,   /* ground, one_shot */
    MODE_SM_EV_FAULT_NONCRITICAL = 3,   /* autonomous, latched */
    MODE_SM_EV_FAULT_CRITICAL = 4,   /* autonomous, latched */
    MODE_SM_EV_FAULT_CLEARED = 5,   /* autonomous, one_shot */
    MODE_SM_EVENT_COUNT = 6
} mode_sm_event_t;

/* Inputs, sampled once at the start of every step. */
typedef struct {
    bool fdir_inhibited;
} mode_sm_inputs_t;

/* Cause carried by an event; the last post of an event wins. */
typedef struct {
    uint8_t  src;
    uint16_t detail;
} mode_sm_payload_t;

/* The machine. Allocated by the caller; no dynamic memory. */
typedef struct {
    mode_sm_state_t  state;
    uint32_t pending;              /* one bit per event */
    mode_sm_payload_t payload[MODE_SM_EVENT_COUNT];
    void *user;                    /* passed to every hook */
} mode_sm_t;

typedef enum {
    MODE_SM_STEP_IDLE = 0,       /* nothing enabled              */
    MODE_SM_STEP_TRANSITION = 1, /* external transition taken     */
    MODE_SM_STEP_INTERNAL = 2,   /* internal transition taken     */
    MODE_SM_STEP_RECOVERED = 3   /* invalid state, recovered      */
} mode_sm_step_kind_t;

typedef struct {
    mode_sm_step_kind_t kind;
    uint8_t transition;            /* 1-based transition number, 0 = none */
    mode_sm_state_t from;
    mode_sm_state_t to;
    mode_sm_event_t event;
} mode_sm_result_t;

/* Enter a boot state (NOMINAL, SAFE): runs its entry
 * actions, no transition report. Returns 0, or -1 if `boot` is not a
 * boot state (the machine is then started in SAFE). */
int mode_sm_init(mode_sm_t *sm, mode_sm_state_t boot, void *user);

/* Mark an event pending for the next step. Returns 0, or -1 for an
 * event value outside the model (ignored: fail closed). */
int mode_sm_post(mode_sm_t *sm, mode_sm_event_t event, mode_sm_payload_t cause);

/* One evaluation cycle: at most one transition. */
mode_sm_result_t mode_sm_step(mode_sm_t *sm, const mode_sm_inputs_t *in);

mode_sm_state_t mode_sm_state(const mode_sm_t *sm);

/* ---- Hooks implemented by the application -------------------------- */

void mode_sm_action_hk_minimal(void *user, const mode_sm_payload_t *cause);
void mode_sm_action_hk_rate_high(void *user, const mode_sm_payload_t *cause);
void mode_sm_action_hk_rate_normal(void *user, const mode_sm_payload_t *cause);
void mode_sm_action_monitors_tighten(void *user, const mode_sm_payload_t *cause);
void mode_sm_action_payload_enable(void *user, const mode_sm_payload_t *cause);
void mode_sm_action_payload_off(void *user, const mode_sm_payload_t *cause);
void mode_sm_action_payload_suspend(void *user, const mode_sm_payload_t *cause);
void mode_sm_action_report_action_inhibited(void *user, const mode_sm_payload_t *cause);
void mode_sm_action_rx_guarantee(void *user, const mode_sm_payload_t *cause);
void mode_sm_action_schedule_suspend(void *user, const mode_sm_payload_t *cause);
void mode_sm_action_sun_pointing(void *user, const mode_sm_payload_t *cause);
void mode_sm_action_sun_pointing_release(void *user, const mode_sm_payload_t *cause);

void mode_sm_on_transition(void *user, mode_sm_state_t from, mode_sm_state_t to,
        mode_sm_event_t event, const mode_sm_payload_t *cause);
void mode_sm_on_discard(void *user, mode_sm_event_t event, const mode_sm_payload_t *cause);
void mode_sm_on_invalid_state(void *user, uint32_t raw_state);

#ifdef MODE_SM_WITH_NAMES
const char *mode_sm_state_name(mode_sm_state_t s);
const char *mode_sm_event_name(mode_sm_event_t e);
#endif

#endif /* MODE_SM_H */
