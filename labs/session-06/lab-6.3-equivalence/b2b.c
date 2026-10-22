/*
 * Lab 6.3 - Back-to-back equivalence harness.
 *
 * Drives the hand written Lab 5.3 mode manager
 * (session-05/lab-5.3-safe-mode/modes.c) and the generated module
 * (lab-6.2-generation/generated/mode_sm.c) with the same input
 * sequences and compares, after every cycle:
 *
 *   - the mode;
 *   - the outputs the mode drives (payload, sun pointing, schedule,
 *     housekeeping rate, monitoring rate);
 *   - the reports: transitions with their cause, and refusals.
 *
 * The two implementations have different execution models, which the
 * comparison has to respect:
 *
 *   hand written  event driven: every mode_request() is evaluated at once,
 *                 in the order the requests arrive;
 *   generated     cyclic: the cycle's events are posted, then ONE step.
 *
 * With one event per cycle (suites A, SA) the two must match exactly,
 * cycle by cycle. With several events in one cycle (suites B, SB) the
 * generated machine may need further cycles to drain latched events, so
 * it is stepped until nothing is pending and the outcome (mode, outputs,
 * set of reports) is compared. Every difference is a divergence; each is
 * classified by rule below and written to divergences.csv.
 *
 * Host test code. Usage: b2b <results dir> <sequence file>...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modes.h"      /* hand written, Session 5 */
#include "mode_sm.h"    /* generated, Lab 6.2      */

#define MAX_EVENTS_PER_CYCLE 8
#define MAX_TRACE            32
#define MAX_DRAIN            8

/* ---- Observables ------------------------------------------------------------ */

typedef struct {
    int payload_on;
    int sun_pointing;
    int sched_enabled;
    int hk_rate;          /* 0 normal, 1 high, 2 minimal */
    int monitor_fast;
} outputs_t;

typedef struct {
    char kind;            /* 'T' transition, 'R' refusal, 'D' discard */
    int from, to, src;
} rec_t;

typedef struct {
    rec_t r[MAX_TRACE];
    int n;
} trace_t;

static void trace_add(trace_t *t, char kind, int from, int to, int src)
{
    if (t->n < MAX_TRACE) {
        t->r[t->n].kind = kind;
        t->r[t->n].from = from;
        t->r[t->n].to = to;
        t->r[t->n].src = src;
        t->n++;
    }
}

/* ---- Abstract events ---------------------------------------------------------- */

typedef struct {
    int ev;               /* mode_sm_event_t */
    int src;
} in_event_t;

static const char *STATE[] = { "NOMINAL", "DEGRADED", "SAFE" };

static int event_by_name(const char *name)
{
    int e;
    for (e = 0; e < (int)MODE_SM_EVENT_COUNT; e++) {
        if (strcmp(name, mode_sm_event_name((mode_sm_event_t)e)) == 0) {
            return e;
        }
    }
    return -1;
}

static int is_ground(int ev)
{
    return ev == MODE_SM_EV_TC_MODE_NOMINAL || ev == MODE_SM_EV_TC_MODE_DEGRADED ||
           ev == MODE_SM_EV_TC_MODE_SAFE;
}

/* ---- Hand written side ------------------------------------------------------------ */

static mode_mgr_t hand;
static outputs_t hand_out;
static trace_t hand_tr;
static int hand_inhibited;

static void hand_apply(void *arg, vmode_t mode)
{
    /* Exactly what Session 5 fdir.c mode_apply() and fdir_hk_cycle()
     * derive from the mode. */
    (void)arg;
    hand_out.payload_on   = (mode == MODE_NOMINAL);
    hand_out.sun_pointing = (mode == MODE_SAFE);
    if (mode == MODE_SAFE) {
        hand_out.sched_enabled = 0;
    }
    hand_out.hk_rate      = (int)mode;
    hand_out.monitor_fast = (mode != MODE_NOMINAL);
}

static void hand_transition(void *arg, vmode_t from, vmode_t to,
                            cause_src_t src, uint16_t detail)
{
    (void)arg; (void)detail;
    trace_add(&hand_tr, 'T', (int)from, (int)to, (int)src);
}

static void hand_inhibit(void *arg, int on, uint16_t d, inhibit_reason_t r)
{
    (void)arg; (void)on; (void)d; (void)r;
}

static const mode_ops_t hand_ops = { hand_apply, hand_transition, hand_inhibit };

static void hand_reset(vmode_t mode)
{
    memset(&hand_out, 0, sizeof(hand_out));
    hand_out.sched_enabled = 1;
    mode_init(&hand, mode, CAUSE_BOOT, 0, &hand_ops, NULL);
    hand_inhibited = 0;
}

/* Returns the mode_result_t of the request. */
static int hand_event(const in_event_t *e)
{
    vmode_t target;
    cause_src_t src = (cause_src_t)e->src;
    mode_result_t r;

    switch (e->ev) {
    case MODE_SM_EV_TC_MODE_NOMINAL:  target = MODE_NOMINAL;  src = CAUSE_GROUND; break;
    case MODE_SM_EV_TC_MODE_DEGRADED: target = MODE_DEGRADED; src = CAUSE_GROUND; break;
    case MODE_SM_EV_TC_MODE_SAFE:     target = MODE_SAFE;     src = CAUSE_GROUND; break;
    case MODE_SM_EV_FAULT_NONCRITICAL: target = MODE_DEGRADED; break;
    case MODE_SM_EV_FAULT_CRITICAL:   target = MODE_SAFE;     break;
    default:                          target = MODE_NOMINAL;  break; /* cleared */
    }
    r = mode_request(&hand, target, src, 0, 0);
    if (r == MODE_REFUSED_INHIBIT) {
        trace_add(&hand_tr, 'R', (int)hand.mode, (int)hand.mode, (int)src);
    }
    return (int)r;
}

static void hand_set_inhibit(int inh)
{
    if (inh != hand_inhibited) {
        (void)mode_set_inhibit(&hand, inh, inh ? 3600u : 0u, 0);
        hand_inhibited = inh;
    }
}

/* ---- Generated side (the hooks) ------------------------------------------------------ */

static mode_sm_t gen;
static outputs_t gen_out;
static trace_t gen_tr;

#define HOOK(name, body) \
    void mode_sm_action_##name(void *user, const mode_sm_payload_t *cause) \
    { (void)user; (void)cause; body; }

HOOK(payload_enable,      gen_out.payload_on = 1)
HOOK(payload_suspend,     gen_out.payload_on = 0)
HOOK(payload_off,         gen_out.payload_on = 0)
HOOK(hk_rate_normal,      gen_out.hk_rate = 0; gen_out.monitor_fast = 0)
HOOK(hk_rate_high,        gen_out.hk_rate = 1)
HOOK(hk_minimal,          gen_out.hk_rate = 2)
HOOK(monitors_tighten,    gen_out.monitor_fast = 1)
HOOK(schedule_suspend,    gen_out.sched_enabled = 0)
HOOK(sun_pointing,        gen_out.sun_pointing = 1)
HOOK(sun_pointing_release, gen_out.sun_pointing = 0)
HOOK(rx_guarantee,        (void)0)
HOOK(report_action_inhibited,
     trace_add(&gen_tr, 'R', (int)gen.state, (int)gen.state, (int)cause->src))

void mode_sm_on_transition(void *user, mode_sm_state_t from, mode_sm_state_t to,
                           mode_sm_event_t event, const mode_sm_payload_t *cause)
{
    (void)user; (void)event;
    trace_add(&gen_tr, 'T', (int)from, (int)to, (int)cause->src);
}

void mode_sm_on_discard(void *user, mode_sm_event_t event,
                        const mode_sm_payload_t *cause)
{
    (void)user;
    trace_add(&gen_tr, 'D', (int)event, (int)event, (int)cause->src);
}

void mode_sm_on_invalid_state(void *user, uint32_t raw)
{
    (void)user; (void)raw;
}

static int gen_inhibited;

static void gen_reset(mode_sm_state_t s)
{
    memset(&gen_out, 0, sizeof(gen_out));
    gen_out.sched_enabled = 1;
    (void)mode_sm_init(&gen, s, NULL);
    gen_inhibited = 0;
}

static void gen_post(const in_event_t *e)
{
    mode_sm_payload_t p;
    p.src = (uint8_t)(is_ground(e->ev) ? CAUSE_GROUND : e->src);
    p.detail = 0;
    (void)mode_sm_post(&gen, (mode_sm_event_t)e->ev, p);
}

static void gen_step(void)
{
    mode_sm_inputs_t in;
    in.fdir_inhibited = gen_inhibited != 0;
    (void)mode_sm_step(&gen, &in);
}

/* ---- Comparison ----------------------------------------------------------------------- */

static FILE *div_csv;
static int checks, divergences;
static int class_count[8];
static int max_drain;

enum {
    C_A_MISMATCH = 0, C_INTERMEDIATE, C_GROUND_OVERRIDE, C_FAULT_LOST,
    C_CONFLICTING_GROUND, C_REPORTS, C_UNCLASSIFIED, C_TIMING
};
static const char *CLASS[] = {
    "single-event mismatch", "intermediate transitions (hand)",
    "ground overrides same-cycle fault (hand)", "fault lost to order (hand)",
    "conflicting ground commands", "same end state but different cause attribution",
    "UNCLASSIFIED", "timing only (not a divergence)"
};

static int same_outputs(void)
{
    return memcmp(&hand_out, &gen_out, sizeof(outputs_t)) == 0;
}

static void fmt_trace(char *buf, size_t n, const trace_t *t, int with_discards)
{
    int i;
    size_t len = 0;
    buf[0] = '\0';
    for (i = 0; i < t->n && len + 32 < n; i++) {
        const rec_t *r = &t->r[i];
        if (r->kind == 'T') {
            len += (size_t)snprintf(buf + len, n - len, "%s%s>%s(c%d)", len ? " " : "",
                                    STATE[r->from], STATE[r->to], r->src);
        } else if (r->kind == 'R') {
            len += (size_t)snprintf(buf + len, n - len, "%srefused(c%d)", len ? " " : "", r->src);
        } else if (with_discards) {
            len += (size_t)snprintf(buf + len, n - len, "%sdiscard:%s", len ? " " : "",
                                    mode_sm_event_name((mode_sm_event_t)r->from));
        }
    }
    if (len == 0) {
        snprintf(buf, n, "-");
    }
}

static void fmt_outputs(char *buf, size_t n, const outputs_t *o)
{
    snprintf(buf, n, "pl%d sun%d sch%d hk%d mon%d", o->payload_on, o->sun_pointing,
             o->sched_enabled, o->hk_rate, o->monitor_fast);
}

static int count_kind(const trace_t *t, char k)
{
    int i, n = 0;
    for (i = 0; i < t->n; i++) {
        n += t->r[i].kind == k;
    }
    return n;
}

/* Same transitions and refusals, in any order (discards excluded). */
static int same_report_set(void)
{
    int used[MAX_TRACE] = { 0 };
    int i, j;
    if (count_kind(&hand_tr, 'T') != count_kind(&gen_tr, 'T') ||
        count_kind(&hand_tr, 'R') != count_kind(&gen_tr, 'R')) {
        return 0;
    }
    for (i = 0; i < hand_tr.n; i++) {
        const rec_t *a = &hand_tr.r[i];
        int found = 0;
        for (j = 0; j < gen_tr.n && !found; j++) {
            const rec_t *b = &gen_tr.r[j];
            /* A refusal is identified by its cause only: the mode it
             * was refused in depends on when it was evaluated. */
            if (!used[j] && b->kind == a->kind && b->src == a->src &&
                (a->kind == 'R' || (b->from == a->from && b->to == a->to))) {
                used[j] = 1;
                found = 1;
            }
        }
        if (!found) {
            return 0;
        }
    }
    return 1;
}

static int same_trace_exact(void)
{
    int i;
    if (hand_tr.n != gen_tr.n) {
        return 0;
    }
    for (i = 0; i < hand_tr.n; i++) {
        const rec_t *a = &hand_tr.r[i], *b = &gen_tr.r[i];
        if (a->kind != b->kind || a->from != b->from || a->to != b->to ||
            a->src != b->src) {
            return 0;
        }
    }
    return 1;
}

static void record(const char *suite, const char *cid, int cycle, int cls,
                   const in_event_t *ev, int n, int inh, vmode_t hand_start)
{
    char evs[160] = "", ht[400], gt[400], ho[64], go[64];
    size_t len = 0;
    int i;

    for (i = 0; i < n; i++) {
        len += (size_t)snprintf(evs + len, sizeof(evs) - len, "%s%s:%d", i ? " " : "",
                                mode_sm_event_name((mode_sm_event_t)ev[i].ev),
                                is_ground(ev[i].ev) ? (int)CAUSE_GROUND : ev[i].src);
    }
    fmt_trace(ht, sizeof(ht), &hand_tr, 0);
    fmt_trace(gt, sizeof(gt), &gen_tr, 1);
    fmt_outputs(ho, sizeof(ho), &hand_out);
    fmt_outputs(go, sizeof(go), &gen_out);
    class_count[cls]++;
    if (cls != C_TIMING) {
        divergences++;
    }
    fprintf(div_csv, "%s,%s,%d,\"%s\",%s,%d,%s,\"%s\",\"%s\",%s,%s,%s,%s\n", suite, cid, cycle,
            CLASS[cls], STATE[hand_start], inh, evs, ht, gt, STATE[hand.mode],
            STATE[gen.state], ho, go);
}

/* Classify a multi-event outcome. results[] are the hand mode_result_t. */
static int classify(const in_event_t *ev, const int *res, int n, int drained)
{
    int i, ground_changed = 0, fault_changed_before_last_ground = 0;
    int fault_nochange_before_ground = 0, last_ground = -1, seen_fault_change = 0;
    int seen_fault_nochange = 0;

    if ((int)hand.mode == (int)gen.state && same_outputs()) {
        if (same_report_set()) {
            return drained > 1 ? C_TIMING : -1;
        }
        return count_kind(&hand_tr, 'T') > count_kind(&gen_tr, 'T')
               ? C_INTERMEDIATE : C_REPORTS;
    }
    for (i = 0; i < n; i++) {
        if (is_ground(ev[i].ev)) {
            if (res[i] == MODE_CHANGED) {
                ground_changed++;
                last_ground = i;
                fault_changed_before_last_ground |= seen_fault_change;
                fault_nochange_before_ground |= seen_fault_nochange;
            }
        } else if (ev[i].ev != MODE_SM_EV_FAULT_CLEARED) {
            if (res[i] == MODE_CHANGED) {
                seen_fault_change = 1;
            } else if (res[i] == MODE_NO_CHANGE) {
                seen_fault_nochange = 1;
            }
        }
    }
    (void)last_ground;
    if (ground_changed >= 2) {
        return C_CONFLICTING_GROUND;
    }
    if ((int)hand.mode < (int)gen.state) {
        if (fault_changed_before_last_ground) {
            return C_GROUND_OVERRIDE;
        }
        if (fault_nochange_before_ground) {
            return C_FAULT_LOST;
        }
    }
    return C_UNCLASSIFIED;
}

/* ---- Sequence runner -------------------------------------------------------------------- */

static int parse_events(char *list, in_event_t *ev)
{
    int n = 0;
    char *tok, *save = NULL;
    if (strcmp(list, "-") == 0) {
        return 0;
    }
    for (tok = strtok_r(list, ",", &save); tok && n < MAX_EVENTS_PER_CYCLE;
         tok = strtok_r(NULL, ",", &save)) {
        char *colon = strchr(tok, ':');
        int src = CAUSE_MONITOR;
        if (colon) {
            *colon = '\0';
            src = atoi(colon + 1);
        }
        ev[n].ev = event_by_name(tok);
        if (ev[n].ev < 0) {
            fprintf(stderr, "b2b: unknown event %s\n", tok);
            exit(2);
        }
        ev[n].src = src;
        n++;
    }
    return n;
}

static void run_cycle(const char *suite, const char *cid, int cycle, int inh,
                      in_event_t *ev, int n)
{
    int res[MAX_EVENTS_PER_CYCLE];
    int i, drained = 0;
    vmode_t start = hand.mode;

    hand_tr.n = 0;
    gen_tr.n = 0;
    hand_set_inhibit(inh);
    gen_inhibited = inh;

    for (i = 0; i < n; i++) {
        res[i] = hand_event(&ev[i]);
        gen_post(&ev[i]);
    }
    do {
        gen_step();
        drained++;
    } while (n > 1 && gen.pending != 0u && drained < MAX_DRAIN);

    checks++;
    if (n > 1 && drained > max_drain) {
        max_drain = drained;
    }
    if (n <= 1) {
        if ((int)hand.mode != (int)gen.state || !same_outputs() || !same_trace_exact()) {
            record(suite, cid, cycle, C_A_MISMATCH, ev, n, inh, start);
            hand_reset((vmode_t)gen.state);           /* resync */
            hand_out = gen_out;
            hand_set_inhibit(inh);
        }
        return;
    }
    {
        int cls = classify(ev, res, n, drained);
        if (cls >= 0) {
            record(suite, cid, cycle, cls, ev, n, inh, start);
            if (cls != C_TIMING) {
                hand_reset((vmode_t)gen.state);        /* resync */
                hand_out = gen_out;
                hand_set_inhibit(inh);
            }
        }
    }
}

static int run_file(const char *path, FILE *summary)
{
    FILE *f = fopen(path, "r");
    char line[8192], cid[64] = "", suite[8] = "";
    int cycle = 0, cases = 0, cycles = 0, events = 0;
    int before = divergences;

    if (f == NULL) {
        fprintf(stderr, "b2b: cannot open %s\n", path);
        return -1;
    }
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        if (strncmp(line, "CASE ", 5) == 0) {
            sscanf(line + 5, "%63s %7s", cid, suite);
            hand_reset(MODE_NOMINAL);
            gen_reset(MODE_SM_NOMINAL);
            cycle = 0;
            cases++;
        } else if (strncmp(line, "CYCLE ", 6) == 0) {
            int inh;
            char list[8000];
            in_event_t ev[MAX_EVENTS_PER_CYCLE];
            int n;
            if (sscanf(line + 6, "%d %7999s", &inh, list) != 2) {
                fprintf(stderr, "b2b: bad line: %s\n", line);
                exit(2);
            }
            n = parse_events(list, ev);
            run_cycle(suite, cid, ++cycle, inh, ev, n);
            cycles++;
            events += n;
        }
    }
    fclose(f);
    fprintf(summary, "%-40s %5d cases %6d cycles %6d events %5d divergences\n",
            strrchr(path, '/') ? strrchr(path, '/') + 1 : path, cases, cycles,
            events, divergences - before);
    return 0;
}

int main(int argc, char **argv)
{
    char path[512];
    FILE *summary;
    int i;

    if (argc < 3) {
        fprintf(stderr, "usage: b2b <results dir> <sequence file>...\n");
        return 2;
    }
    snprintf(path, sizeof(path), "%s/divergences.csv", argv[1]);
    div_csv = fopen(path, "w");
    snprintf(path, sizeof(path), "%s/b2b-summary.txt", argv[1]);
    summary = fopen(path, "w");
    if (div_csv == NULL || summary == NULL) {
        fprintf(stderr, "b2b: cannot write to %s\n", argv[1]);
        return 2;
    }
    fprintf(div_csv, "suite,case,cycle,class,hand_start,inhibit,events,hand_reports,"
                     "generated_reports,hand_final,generated_final,hand_outputs,"
                     "generated_outputs\n");
    fprintf(summary, "Back-to-back: hand written modes.c vs generated mode_sm.c\n\n");
    for (i = 2; i < argc; i++) {
        if (run_file(argv[i], summary) != 0) {
            return 2;
        }
    }
    fprintf(summary, "\n%d cycles compared, %d divergences\n", checks, divergences);
    fprintf(summary, "Max drain: %d generated steps for one multi-event cycle\n\nBy class:\n",
            max_drain);
    for (i = 0; i < 8; i++) {
        if (class_count[i] > 0) {
            fprintf(summary, "  %-45s %5d\n", CLASS[i], class_count[i]);
        }
    }
    fclose(summary);
    fclose(div_csv);
    snprintf(path, sizeof(path), "%s/b2b-summary.txt", argv[1]);
    summary = fopen(path, "r");
    while (summary && fgets(path, sizeof(path), summary)) {
        fputs(path, stdout);
    }
    if (summary) {
        fclose(summary);
    }
    return class_count[C_UNCLASSIFIED] > 0 ? 1 : 0;
}
