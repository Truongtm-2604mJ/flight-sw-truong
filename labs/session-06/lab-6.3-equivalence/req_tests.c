/*
 * Lab 6.3 / 6.4 - Requirement based tests of the generated mode module.
 *
 * Each test case has an identifier (TC-...) and the requirements it
 * verifies (MM-...). The program prints one line per test case,
 *     <TC id> <MM ids> PASS|FAIL
 * which ../lab-6.4-traceability/build_matrix.py reads to fill the Result
 * column of the traceability matrix.
 *
 * Unlike the back-to-back harness, these tests state the expected
 * behaviour explicitly, so they would catch a defect that the hand
 * written and generated versions shared.
 */
#include <stdio.h>
#include <string.h>

#include "mode_sm.h"

/* ---- Recording hooks ---------------------------------------------------------- */

typedef struct {
    int payload_on, sun, sched, hk_rate, mon_fast, rx;
    int transitions, refusals, discards, invalid;
    mode_sm_state_t last_from, last_to;
    mode_sm_event_t last_event;
    uint8_t last_src;
    uint16_t last_detail;
} obs_t;

static obs_t o;
static mode_sm_t sm;

#define HOOK(name, body) \
    void mode_sm_action_##name(void *user, const mode_sm_payload_t *cause) \
    { (void)user; (void)cause; body; }

HOOK(payload_enable,      o.payload_on = 1)
HOOK(payload_suspend,     o.payload_on = 0)
HOOK(payload_off,         o.payload_on = 0)
HOOK(hk_rate_normal,      o.hk_rate = 0; o.mon_fast = 0)
HOOK(hk_rate_high,        o.hk_rate = 1)
HOOK(hk_minimal,          o.hk_rate = 2)
HOOK(monitors_tighten,    o.mon_fast = 1)
HOOK(schedule_suspend,    o.sched = 0)
HOOK(sun_pointing,        o.sun = 1)
HOOK(sun_pointing_release, o.sun = 0)
HOOK(rx_guarantee,        o.rx = 1)
HOOK(report_action_inhibited, o.refusals++; o.last_src = cause->src)

void mode_sm_on_transition(void *user, mode_sm_state_t from, mode_sm_state_t to,
                           mode_sm_event_t event, const mode_sm_payload_t *cause)
{
    (void)user;
    o.transitions++;
    o.last_from = from;
    o.last_to = to;
    o.last_event = event;
    o.last_src = cause->src;
    o.last_detail = cause->detail;
}

void mode_sm_on_discard(void *user, mode_sm_event_t event, const mode_sm_payload_t *c)
{
    (void)user; (void)event; (void)c;
    o.discards++;
}

void mode_sm_on_invalid_state(void *user, uint32_t raw)
{
    (void)user; (void)raw;
    o.invalid++;
}

/* ---- Helpers ------------------------------------------------------------------- */

static int fails;

#define EXPECT(c) do { if (!(c)) { fails++; \
    printf("    expectation failed, line %d: %s\n", __LINE__, #c); } } while (0)

static void boot(mode_sm_state_t s)
{
    memset(&o, 0, sizeof(o));
    o.sched = 1;
    EXPECT(mode_sm_init(&sm, s, NULL) == 0);
}

static void post(mode_sm_event_t e, uint8_t src, uint16_t detail)
{
    mode_sm_payload_t p;
    p.src = src;
    p.detail = detail;
    EXPECT(mode_sm_post(&sm, e, p) == 0);
}

static mode_sm_result_t step(int inhibited)
{
    mode_sm_inputs_t in;
    in.fdir_inhibited = inhibited != 0;
    return mode_sm_step(&sm, &in);
}

/* Reach a state from a cold boot through the ground path. */
static void reach(mode_sm_state_t s)
{
    boot(MODE_SM_NOMINAL);
    if (s == MODE_SM_DEGRADED) {
        post(MODE_SM_EV_TC_MODE_DEGRADED, 1, 0);
        (void)step(0);
    } else if (s == MODE_SM_SAFE) {
        post(MODE_SM_EV_TC_MODE_SAFE, 1, 0);
        (void)step(0);
    }
    EXPECT(mode_sm_state(&sm) == s);
    o.transitions = 0;
    o.refusals = 0;
    o.discards = 0;
}

static const mode_sm_event_t AUTONOMOUS[] = {
    MODE_SM_EV_FAULT_NONCRITICAL, MODE_SM_EV_FAULT_CRITICAL, MODE_SM_EV_FAULT_CLEARED
};
static const uint8_t CAUSES[] = { 0, 2, 3, 4, 5, 99, 255 };

/* ---- Test cases ------------------------------------------------------------------- */

/* MM-010: SAFE is left only by a ground command. */
static void tc_safe_exit_01(void)
{
    size_t e, c;
    int k, inh;
    reach(MODE_SM_SAFE);
    for (k = 0; k < 100; k++) {
        for (inh = 0; inh < 2; inh++) {
            for (e = 0; e < 3; e++) {
                for (c = 0; c < sizeof(CAUSES); c++) {
                    post(AUTONOMOUS[e], CAUSES[c], 0);
                    (void)step(inh);
                }
            }
            post(MODE_SM_EV_TC_MODE_SAFE, 1, 0);
            (void)step(inh);
            (void)step(inh);           /* and plain time passing */
        }
    }
    EXPECT(mode_sm_state(&sm) == MODE_SM_SAFE);
    EXPECT(o.transitions == 0);
}

static void tc_safe_exit_02(void)
{
    reach(MODE_SM_SAFE);
    post(MODE_SM_EV_TC_MODE_NOMINAL, 1, 0);
    EXPECT(step(0).transition == 12u);
    EXPECT(mode_sm_state(&sm) == MODE_SM_NOMINAL);
    reach(MODE_SM_SAFE);
    post(MODE_SM_EV_TC_MODE_NOMINAL, 1, 0);
    (void)step(1);                       /* ground is never inhibited */
    EXPECT(mode_sm_state(&sm) == MODE_SM_NOMINAL);
}

static void tc_safe_exit_03(void)
{
    reach(MODE_SM_SAFE);
    post(MODE_SM_EV_TC_MODE_DEGRADED, 1, 0);
    EXPECT(step(0).transition == 11u);
    EXPECT(mode_sm_state(&sm) == MODE_SM_DEGRADED);
    post(MODE_SM_EV_TC_MODE_NOMINAL, 1, 0);
    EXPECT(step(0).transition == 9u);
    EXPECT(mode_sm_state(&sm) == MODE_SM_NOMINAL);
}

/* MM-020: SAFE entry suspends the schedule; leaving SAFE does not resume it. */
static void tc_safe_entry_02(void)
{
    reach(MODE_SM_NOMINAL);
    EXPECT(o.sched == 1);
    post(MODE_SM_EV_FAULT_CRITICAL, 2, 2);
    (void)step(0);
    EXPECT(mode_sm_state(&sm) == MODE_SM_SAFE && o.sched == 0);
    reach(MODE_SM_DEGRADED);
    o.sched = 1;
    post(MODE_SM_EV_TC_MODE_SAFE, 1, 0);
    (void)step(0);
    EXPECT(o.sched == 0);
    post(MODE_SM_EV_TC_MODE_NOMINAL, 1, 0);
    (void)step(0);
    EXPECT(mode_sm_state(&sm) == MODE_SM_NOMINAL && o.sched == 0);
}

/* MM-030: SAFE entry configures the survival configuration. */
static void tc_safe_entry_01(void)
{
    reach(MODE_SM_NOMINAL);
    EXPECT(o.payload_on == 1 && o.sun == 0);
    post(MODE_SM_EV_TC_MODE_SAFE, 1, 0);
    (void)step(0);
    EXPECT(o.payload_on == 0 && o.hk_rate == 2 && o.sun == 1 && o.rx == 1 &&
           o.mon_fast == 1);
    post(MODE_SM_EV_TC_MODE_NOMINAL, 1, 0);
    (void)step(0);
    EXPECT(o.sun == 0 && o.payload_on == 1 && o.hk_rate == 0 && o.mon_fast == 0);
}

/* MM-040: confirmed faults escalate when not inhibited. */
static void tc_fault_01(void)
{
    reach(MODE_SM_NOMINAL);
    post(MODE_SM_EV_FAULT_NONCRITICAL, 2, 2);
    EXPECT(step(0).transition == 4u);
    EXPECT(mode_sm_state(&sm) == MODE_SM_DEGRADED && o.payload_on == 0 &&
           o.mon_fast == 1 && o.hk_rate == 1);
    post(MODE_SM_EV_FAULT_CRITICAL, 3, 0);
    EXPECT(step(0).transition == 8u);
    EXPECT(mode_sm_state(&sm) == MODE_SM_SAFE);
    reach(MODE_SM_NOMINAL);
    post(MODE_SM_EV_FAULT_CRITICAL, 3, 0);
    EXPECT(step(0).transition == 2u);
    EXPECT(mode_sm_state(&sm) == MODE_SM_SAFE);
}

/* MM-050: no autonomous event ever lowers the mode, from any state, with
 * any input, any cause. */
static void tc_norec_01(void)
{
    mode_sm_state_t s;
    size_t e, c;
    int inh;
    for (s = MODE_SM_NOMINAL; s < MODE_SM_STATE_COUNT; s++) {
        for (e = 0; e < 3; e++) {
            for (c = 0; c < sizeof(CAUSES); c++) {
                for (inh = 0; inh < 2; inh++) {
                    reach(s);
                    post(AUTONOMOUS[e], CAUSES[c], 0);
                    (void)step(inh);
                    (void)step(inh);
                    EXPECT(mode_sm_state(&sm) >= s);
                }
            }
        }
    }
}

/* MM-060: while inhibited, autonomous changes are refused and reported. */
static void tc_inh_01(void)
{
    reach(MODE_SM_NOMINAL);
    post(MODE_SM_EV_FAULT_CRITICAL, 3, 0);
    EXPECT(step(1).transition == 5u);
    post(MODE_SM_EV_FAULT_NONCRITICAL, 2, 2);
    EXPECT(step(1).transition == 6u);
    EXPECT(mode_sm_state(&sm) == MODE_SM_NOMINAL && o.refusals == 2 &&
           o.transitions == 0);
    reach(MODE_SM_DEGRADED);
    post(MODE_SM_EV_FAULT_CRITICAL, 3, 0);
    EXPECT(step(1).transition == 10u);
    EXPECT(mode_sm_state(&sm) == MODE_SM_DEGRADED && o.refusals == 1);
    (void)step(1);
    EXPECT(o.refusals == 1);            /* reported once, then consumed */
}

/* MM-070: ground commands SAFE / DEGRADED, also while inhibited. */
static void tc_gnd_01(void)
{
    int inh;
    for (inh = 0; inh < 2; inh++) {
        reach(MODE_SM_NOMINAL);
        post(MODE_SM_EV_TC_MODE_SAFE, 1, 0);
        EXPECT(step(inh).transition == 1u);
        reach(MODE_SM_NOMINAL);
        post(MODE_SM_EV_TC_MODE_DEGRADED, 1, 0);
        EXPECT(step(inh).transition == 3u);
        post(MODE_SM_EV_TC_MODE_SAFE, 1, 0);
        EXPECT(step(inh).transition == 7u);
        EXPECT(mode_sm_state(&sm) == MODE_SM_SAFE);
    }
}

/* MM-080: every transition reported once, with the triggering cause. */
static void tc_rep_01(void)
{
    reach(MODE_SM_NOMINAL);
    post(MODE_SM_EV_FAULT_NONCRITICAL, 2, 0x0220);
    (void)step(0);
    EXPECT(o.transitions == 1 && o.last_from == MODE_SM_NOMINAL &&
           o.last_to == MODE_SM_DEGRADED &&
           o.last_event == MODE_SM_EV_FAULT_NONCRITICAL && o.last_src == 2 &&
           o.last_detail == 0x0220);
    post(MODE_SM_EV_FAULT_CRITICAL, 3, 1);
    (void)step(0);
    EXPECT(o.transitions == 2 && o.last_to == MODE_SM_SAFE && o.last_src == 3 &&
           o.last_detail == 1);
    (void)step(0);
    EXPECT(o.transitions == 2);         /* nothing without a new event */
}

/* MM-090: boot states. */
static void tc_boot_01(void)
{
    boot(MODE_SM_NOMINAL);
    EXPECT(mode_sm_state(&sm) == MODE_SM_NOMINAL && o.payload_on == 1 &&
           o.transitions == 0);
    boot(MODE_SM_SAFE);
    EXPECT(mode_sm_state(&sm) == MODE_SM_SAFE && o.sched == 0 && o.sun == 1 &&
           o.transitions == 0);
    memset(&o, 0, sizeof(o));
    EXPECT(mode_sm_init(&sm, MODE_SM_DEGRADED, NULL) == -1);   /* not a boot state */
    EXPECT(mode_sm_state(&sm) == MODE_SM_SAFE);
    EXPECT(mode_sm_init(&sm, (mode_sm_state_t)7, NULL) == -1);
    EXPECT(mode_sm_state(&sm) == MODE_SM_SAFE);
}

/* MM-110: one transition per step, the highest priority one. */
static void tc_det_01(void)
{
    mode_sm_result_t r;
    reach(MODE_SM_NOMINAL);
    post(MODE_SM_EV_FAULT_NONCRITICAL, 2, 0);
    post(MODE_SM_EV_TC_MODE_DEGRADED, 1, 0);
    post(MODE_SM_EV_FAULT_CRITICAL, 2, 0);
    post(MODE_SM_EV_TC_MODE_SAFE, 1, 0);
    r = step(0);
    EXPECT(r.transition == 1u && o.transitions == 1);      /* T01 wins */
    EXPECT(o.discards == 1);            /* tc_mode_degraded lost, reported */
    /* Two ground commands in SAFE: the less de-escalating one. */
    reach(MODE_SM_SAFE);
    post(MODE_SM_EV_TC_MODE_NOMINAL, 1, 0);
    post(MODE_SM_EV_TC_MODE_DEGRADED, 1, 0);
    EXPECT(step(0).transition == 11u);
    EXPECT(o.discards == 1);
    (void)step(0);
    EXPECT(mode_sm_state(&sm) == MODE_SM_DEGRADED);         /* not retained */
}

/* MM-120: only ground commands are privileged; inputs outside the model
 * are rejected or recovered from, never trusted. */
static void tc_fail_01(void)
{
    mode_sm_payload_t p = { 1, 0 };
    reach(MODE_SM_SAFE);
    EXPECT(mode_sm_post(&sm, (mode_sm_event_t)MODE_SM_EVENT_COUNT, p) == -1);
    EXPECT(mode_sm_post(&sm, (mode_sm_event_t)31, p) == -1);
    EXPECT(sm.pending == 0u);
    (void)step(0);
    EXPECT(mode_sm_state(&sm) == MODE_SM_SAFE);
    /* A fault event carrying the ground cause value is still a fault. */
    post(MODE_SM_EV_FAULT_CLEARED, 1, 0);
    (void)step(0);
    EXPECT(mode_sm_state(&sm) == MODE_SM_SAFE);
}

static void tc_fail_02(void)
{
    mode_sm_result_t r;
    reach(MODE_SM_NOMINAL);
    post(MODE_SM_EV_FAULT_CRITICAL, 2, 0);      /* latched, kept      */
    post(MODE_SM_EV_TC_MODE_NOMINAL, 1, 0);     /* one shot, dropped  */
    sm.state = (mode_sm_state_t)0x55;           /* simulated upset    */
    r = step(0);
    EXPECT(r.kind == MODE_SM_STEP_RECOVERED && o.invalid == 1);
    EXPECT(mode_sm_state(&sm) == MODE_SM_SAFE && o.sched == 0);
    EXPECT(sm.pending == (1u << MODE_SM_EV_FAULT_CRITICAL));
    (void)step(0);                              /* ignored in SAFE: dropped */
    EXPECT(sm.pending == 0u);
}

/* MM-130: a fault in the same cycle as a ground recovery is not lost. */
static void tc_latch_01(void)
{
    reach(MODE_SM_DEGRADED);
    post(MODE_SM_EV_FAULT_NONCRITICAL, 2, 2);
    post(MODE_SM_EV_TC_MODE_NOMINAL, 1, 0);
    EXPECT(step(0).transition == 9u);
    EXPECT(step(0).transition == 4u);
    EXPECT(mode_sm_state(&sm) == MODE_SM_DEGRADED);
    reach(MODE_SM_SAFE);
    post(MODE_SM_EV_FAULT_CRITICAL, 3, 0);
    post(MODE_SM_EV_TC_MODE_NOMINAL, 1, 0);
    EXPECT(step(0).transition == 12u);
    EXPECT(step(0).transition == 2u);
    EXPECT(mode_sm_state(&sm) == MODE_SM_SAFE);
    /* ...and refused, not silently lost, if FDIR is inhibited. */
    reach(MODE_SM_SAFE);
    post(MODE_SM_EV_FAULT_CRITICAL, 3, 0);
    post(MODE_SM_EV_TC_MODE_NOMINAL, 1, 0);
    (void)step(1);
    EXPECT(step(1).transition == 5u && o.refusals == 1);
    EXPECT(mode_sm_state(&sm) == MODE_SM_NOMINAL);
}

typedef struct {
    const char *id;
    const char *reqs;
    void (*fn)(void);
} tc_t;

static const tc_t TESTS[] = {
    { "TC-SAFE-EXIT-01",  "MM-010",        tc_safe_exit_01 },
    { "TC-SAFE-EXIT-02",  "MM-010,MM-070", tc_safe_exit_02 },
    { "TC-SAFE-EXIT-03",  "MM-010",        tc_safe_exit_03 },
    { "TC-SAFE-ENTRY-01", "MM-030",        tc_safe_entry_01 },
    { "TC-SAFE-ENTRY-02", "MM-020",        tc_safe_entry_02 },
    { "TC-FAULT-01",      "MM-040",        tc_fault_01 },
    { "TC-NOREC-01",      "MM-050",        tc_norec_01 },
    { "TC-INH-01",        "MM-060",        tc_inh_01 },
    { "TC-GND-01",        "MM-070",        tc_gnd_01 },
    { "TC-REP-01",        "MM-080",        tc_rep_01 },
    { "TC-BOOT-01",       "MM-090",        tc_boot_01 },
    { "TC-DET-01",        "MM-110",        tc_det_01 },
    { "TC-FAIL-01",       "MM-120",        tc_fail_01 },
    { "TC-FAIL-02",       "MM-120",        tc_fail_02 },
    { "TC-LATCH-01",      "MM-130",        tc_latch_01 },
};

int main(void)
{
    size_t i;
    int failed = 0;
    for (i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); i++) {
        int before = fails;
        TESTS[i].fn();
        printf("%-18s %-15s %s\n", TESTS[i].id, TESTS[i].reqs,
               fails == before ? "PASS" : "FAIL");
        failed += fails != before;
    }
    printf("\n%u test cases, %d failed\n", (unsigned)(sizeof(TESTS) / sizeof(TESTS[0])),
           failed);
    return failed ? 1 : 0;
}
