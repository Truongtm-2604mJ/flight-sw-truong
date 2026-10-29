/*
 * Integration suite: the telecommand path, from reception to verification
 * and event reporting, with the real flight modules linked together.
 *
 *   tmtc_dispatch() (TC_RX entry, tc lock)          tmtc5.c
 *     -> tc_handle(): packet checks, acceptance      tc_handler.c, pus.c, sp.c, crc16.c
 *     -> service check / exec                        services.c
 *     -> FDIR: mode requests, inhibit, schedule      fdir6.c, schedule.c, supervisor.c
 *     -> generated mode machine, one step per cycle  mode_sm.c
 *     -> TM(1,x), TM(17,2), TM(5,x) at the TM sink   (captured here)
 *
 * What is NOT in scope here, and where it is covered instead: the console
 * line framing (TC_RX process_line), the TM queue and the TM_TX task, and
 * the periodic tasks. Those are exercised by every validation test, which
 * goes through the real UART path (tests/validation).
 *
 * The TM sink is intercepted with the linker (--wrap=tmtc_tm_sink), so the
 * flight sources are linked unmodified. The program runs the same way on
 * the host simulation of the RTEMS API and on RTEMS 6 / LEON3 under SIS,
 * prints one line per test case and a termination marker:
 *
 *     @@ INTEGRATION RESULT: PASS 13/13 @@
 *
 * and shuts down. tests/support/run_image.py turns the marker into an exit
 * code and enforces a timeout, so no human has to read the console.
 */
#include <rtems.h>
#include <stdio.h>
#include <string.h>

#include "crc16.h"
#include "fdir.h"
#include "fdir_app.h"
#include "mode_sm.h"
#include "pus.h"
#include "services.h"
#include "supervisor.h"
#include "tc_handler.h"
#include "tmtc5.h"
#include "schedule.h"

/* ---- RTEMS configuration --------------------------------------------------------- */
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER
#define CONFIGURE_MAXIMUM_TASKS            5
#define CONFIGURE_MAXIMUM_SEMAPHORES       8
#define CONFIGURE_MAXIMUM_TIMERS           1
#define CONFIGURE_MAXIMUM_MESSAGE_QUEUES   1
#define CONFIGURE_MESSAGE_BUFFER_MEMORY \
    CONFIGURE_MESSAGE_BUFFERS_FOR_QUEUE(TM_QUEUE_DEPTH, PUS_MAX_PACKET)
#define CONFIGURE_EXTRA_TASK_STACKS        (2u * 8u * 1024u)
#define CONFIGURE_MICROSECONDS_PER_TICK    10000
#define CONFIGURE_INIT_TASK_STACK_SIZE     (16 * 1024)
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT
#include <rtems/confdefs.h>

/* ---- TM capture at the sink -------------------------------------------------------- */

#define CAP_MAX 600u

typedef struct {
    uint16_t apid, seq;
    uint8_t svc, sub;
    uint16_t dest;
    uint8_t data[PUS_MAX_PACKET];
    size_t dlen;
    int crc_ok;
} tm_t;

static tm_t cap[CAP_MAX];
static volatile size_t cap_n;
static volatile size_t cap_lost;
static rtems_id cap_lock;

int __real_tmtc_tm_sink(const uint8_t *packet, size_t len, void *arg);
int __wrap_tmtc_tm_sink(const uint8_t *packet, size_t len, void *arg);

int __wrap_tmtc_tm_sink(const uint8_t *p, size_t len, void *arg)
{
    tm_t *t;
    (void)arg;
    (void)rtems_semaphore_obtain(cap_lock, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
    if (cap_n >= CAP_MAX || len < PUS_TM_MIN_LEN) {
        cap_lost++;
        (void)rtems_semaphore_release(cap_lock);
        return 0;
    }
    t = &cap[cap_n];
    t->apid = (uint16_t)(((p[0] & 0x07u) << 8) | p[1]);
    t->seq  = (uint16_t)(((p[2] & 0x3Fu) << 8) | p[3]);
    t->svc  = p[7];
    t->sub  = p[8];
    t->dest = pus_get_u16(&p[11]);
    t->dlen = len - PUS_TM_MIN_LEN;
    memcpy(t->data, &p[6 + PUS_TM_SEC_HDR_LEN], t->dlen);
    t->crc_ok = crc16_ccitt(p, len - 2u) == pus_get_u16(&p[len - 2u]);
    cap_n++;
    (void)rtems_semaphore_release(cap_lock);
    return 0;       /* captured: not queued, TM_TX is not running here */
}

static void cap_reset(void)
{
    cap_n = 0;
    cap_lost = 0;
}

/* Index of the n-th (0 based) captured packet of service/subtype, or -1. */
static int find(uint8_t svc, uint8_t sub, int nth)
{
    size_t i;
    for (i = 0; i < cap_n; i++) {
        if (cap[i].svc == svc && cap[i].sub == sub && nth-- == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Index of the first event report TM(5,x) with this event id, or -1. */
static int find_event(uint16_t id)
{
    size_t i;
    for (i = 0; i < cap_n; i++) {
        if (cap[i].svc == 5u && cap[i].dlen >= 2u && pus_get_u16(cap[i].data) == id) {
            return (int)i;
        }
    }
    return -1;
}

/* ---- Telecommands ---------------------------------------------------------------------- */

#define ACK_ACC_COMPL (PUS_ACK_ACCEPTANCE | PUS_ACK_COMPLETION)
#define GROUND_ID 0x0042u

static uint16_t tc_seq;
static uint8_t last_req[PUS_REQUEST_ID_LEN];

static size_t build_tc(uint8_t *out, uint16_t apid, uint8_t svc, uint8_t sub,
                       const uint8_t *data, size_t len)
{
    int n = pus_tc_build(apid, tc_seq, ACK_ACC_COMPL, svc, sub, GROUND_ID,
                         data, len, out, PUS_MAX_PACKET);
    tc_seq = (uint16_t)((tc_seq + 1u) & 0x3FFFu);
    memcpy(last_req, out, PUS_REQUEST_ID_LEN);
    return n > 0 ? (size_t)n : 0u;
}

static void tc_send(uint8_t svc, uint8_t sub, const uint8_t *data, size_t len)
{
    uint8_t f[PUS_MAX_PACKET];
    size_t n = build_tc(f, APID_CMD, svc, sub, data, len);
    tmtc_dispatch(f, n);
}

static void set_mode(uint8_t mode)
{
    uint8_t d[2];
    d[0] = FUNC_SET_MODE;
    d[1] = mode;
    tc_send(SVC_FUNCTION, 1, d, 2);
}

/* ---- Test bookkeeping ---------------------------------------------------------------- */

static int failures_in_case;
static int cases_run;
static int cases_failed;

#define EXPECT(c) do { if (!(c)) { failures_in_case++; \
    printf("    expectation failed, line %d: %s\n", __LINE__, #c); } } while (0)

static void run_case(const char *id, const char *title, void (*fn)(void))
{
    failures_in_case = 0;
    cap_reset();
    fn();
    cases_run++;
    if (failures_in_case != 0) {
        cases_failed++;
    }
    printf("%-6s %-62s %s\n", id, title, failures_in_case ? "FAIL" : "PASS");
    fflush(stdout);
}

/* Verification report i: right subtype, request ID of the last TC, APID,
 * addressed to the TC's source. */
static int is_report(int i, uint8_t sub)
{
    return i >= 0 && cap[i].apid == APID_CMD && cap[i].svc == 1u && cap[i].sub == sub &&
           cap[i].dlen >= PUS_REQUEST_ID_LEN &&
           memcmp(cap[i].data, last_req, PUS_REQUEST_ID_LEN) == 0 &&
           cap[i].dest == GROUND_ID;
}

/* Acceptance failure for a TC rejected at packet level (checks 1 to 7):
 * by design (Session 4 design note, tc_handler.c) its source ID is not
 * trusted, so the report goes to destination 0. First run: these cases
 * expected the source ID and failed; the test was wrong, not the code. */
static int is_packet_rejection(int i, pus_fail_t code)
{
    return i >= 0 && cap[i].apid == APID_CMD && cap[i].svc == 1u && cap[i].sub == 2u &&
           cap[i].dlen == PUS_REQUEST_ID_LEN + 1u &&
           memcmp(cap[i].data, last_req, PUS_REQUEST_ID_LEN) == 0 &&
           cap[i].data[4] == (uint8_t)code && cap[i].dest == 0u;
}

/* ---- Test cases --------------------------------------------------------------------------- */

static void it01_ping(void)
{
    size_t i;
    tc_send(17, 1, NULL, 0);
    EXPECT(cap_n == 3u);
    EXPECT(is_report(0, 1));                       /* TM(1,1) acceptance */
    EXPECT(cap_n > 1u && cap[1].svc == 17u && cap[1].sub == 2u);
    EXPECT(is_report(2, 7));                       /* TM(1,7) completion */
    for (i = 0; i < cap_n; i++) {
        EXPECT(cap[i].crc_ok);
        if (i > 0u) {
            EXPECT(cap[i].seq == (uint16_t)((cap[i - 1u].seq + 1u) & 0x3FFFu));
        }
    }
}

static void it02_bad_crc(void)
{
    uint8_t f[PUS_MAX_PACKET];
    size_t n = build_tc(f, APID_CMD, 17, 1, NULL, 0);
    uint32_t rej = tmtc_tc_ctx()->rejected;
    f[n - 1u] ^= 0x01u;
    tmtc_dispatch(f, n);
    EXPECT(cap_n == 1u);
    EXPECT(is_packet_rejection(0, PUS_FAIL_CHECKSUM));
    EXPECT(find(17, 2, 0) < 0);                    /* not executed */
    EXPECT(tmtc_tc_ctx()->rejected == rej + 1u);
}

static void it03_unknown_service(void)
{
    tc_send(99, 1, NULL, 0);
    EXPECT(cap_n == 1u && is_report(0, 2) && cap[0].data[4] == PUS_FAIL_SERVICE);
}

static void it04_wrong_apid(void)
{
    uint8_t f[PUS_MAX_PACKET];
    size_t n = build_tc(f, 0x055u, 17, 1, NULL, 0);
    tmtc_dispatch(f, n);
    EXPECT(cap_n == 1u && is_packet_rejection(0, PUS_FAIL_APID));
}

static void it05_bad_mode_value(void)
{
    vmode_t before = fdir_mode();
    set_mode(3);
    fdir_supervisor_cycle();
    EXPECT(is_report(0, 2) && cap[0].data[4] == PUS_FAIL_APP_DATA);
    EXPECT(find(1, 7, 0) < 0);
    EXPECT(fdir_mode() == before);
}

static void it06_ground_safe(void)
{
    int ev;
    EXPECT(fdir_mode() == MODE_NOMINAL);
    set_mode((uint8_t)MODE_SAFE);
    EXPECT(is_report(0, 1) && is_report(1, 7));
    EXPECT(find_event(EVT_MODE_CHANGED) < 0);      /* not before the cycle */
    fdir_supervisor_cycle();                       /* the model's step */
    ev = find_event(EVT_MODE_CHANGED);
    EXPECT(ev >= 0 && cap[ev].apid == APID_EVENT && cap[ev].crc_ok);
    EXPECT(ev >= 0 && cap[ev].data[2] == MODE_SM_NOMINAL && cap[ev].data[3] == MODE_SM_SAFE &&
           cap[ev].data[4] == (uint8_t)CAUSE_GROUND);
    EXPECT(find_event(EVT_SCHED_SUSPENDED) >= 0);
    EXPECT(fdir_mode() == MODE_SAFE);
    EXPECT(!fdir_payload_on());
}

static void it07_conflicting_ground_commands(void)
{
    int ev, dis;
    set_mode((uint8_t)MODE_DEGRADED);
    set_mode((uint8_t)MODE_NOMINAL);
    EXPECT(find(1, 7, 0) >= 0 && find(1, 7, 1) >= 0);  /* both accepted */
    fdir_supervisor_cycle();
    ev = find_event(EVT_MODE_CHANGED);
    dis = find_event(0x0404u);                      /* EVT_MODE_CMD_DISCARDED */
    EXPECT(ev >= 0 && cap[ev].data[3] == MODE_SM_DEGRADED);
    EXPECT(dis >= 0 && cap[dis].data[2] == (uint8_t)MODE_SM_EV_TC_MODE_NOMINAL);
    EXPECT(fdir_mode() == MODE_DEGRADED);
}

static void it08_inhibit_blocks_autonomous_only(void)
{
    uint8_t d[2];
    pus_put_u16(d, 60);
    tc_send(SVC_EVENT_ACTION, 9, d, 2);                /* inhibit 60 s */
    /* Order: TM(1,1), the inhibit event emitted during execution, TM(1,7).
     * First run expected TM(1,7) at index 1; the test was wrong. */
    EXPECT(is_report(0, 1) && is_report(2, 7));
    EXPECT(find_event(EVT_INHIBIT_CHANGED) >= 0);
    (void)fdir_mode_request(MODE_SAFE, CAUSE_MONITOR, 2);   /* critical fault */
    fdir_supervisor_cycle();
    EXPECT(find_event(SUP_EVT_ACTION_INHIBITED) >= 0);
    EXPECT(fdir_mode() == MODE_DEGRADED);
    cap_reset();
    set_mode((uint8_t)MODE_NOMINAL);                /* ground: never inhibited */
    fdir_supervisor_cycle();
    EXPECT(fdir_mode() == MODE_NOMINAL);
    tc_send(SVC_EVENT_ACTION, 8, NULL, 0);             /* lift the inhibit */
    EXPECT(find(1, 7, 1) >= 0);
    EXPECT(find_event(EVT_INHIBIT_CHANGED) >= 0);
}

static void it09_scheduled_command(void)
{
    uint8_t inner[PUS_MAX_PACKET];
    uint8_t d[1 + 4 + PUS_MAX_PACKET];
    uint8_t inner_req[PUS_REQUEST_ID_LEN];
    size_t n = build_tc(inner, APID_CMD, 17, 1, NULL, 0);
    memcpy(inner_req, last_req, sizeof(inner_req));
    d[0] = 1u;
    pus_put_u32(&d[1], 0u);                         /* due at once */
    memcpy(&d[5], inner, n);
    tc_send(SVC_SCHEDULE, 4, d, 5u + n);
    EXPECT(is_report(1, 7));
    tc_send(SVC_SCHEDULE, 1, NULL, 0);                 /* enable (suspended in SAFE) */
    cap_reset();
    fdir_sched_cycle();                             /* SCHED task body */
    memcpy(last_req, inner_req, sizeof(last_req));
    EXPECT(cap_n == 3u);
    EXPECT(is_report(0, 1) && is_report(2, 7));
    EXPECT(find(17, 2, 0) == 1);
}

/* MM-130 through the real path: a monitor fault and a ground de-escalation
 * in the same evaluation cycle. The ground cannot choose the cycle a
 * command lands in, so this is tested here rather than in validation. */
static void it10_fault_not_lost_to_ground_recovery(void)
{
    int k, back = -1;
    set_mode((uint8_t)MODE_DEGRADED);
    fdir_supervisor_cycle();
    EXPECT(fdir_mode() == MODE_DEGRADED);
    cap_reset();
    (void)fdir_mode_request(MODE_DEGRADED, CAUSE_MONITOR, 2);   /* alarm */
    set_mode((uint8_t)MODE_NOMINAL);                            /* ground */
    fdir_supervisor_cycle();
    EXPECT(fdir_mode() == MODE_NOMINAL);
    fdir_supervisor_cycle();
    EXPECT(fdir_mode() == MODE_DEGRADED);
    for (k = 0; k < (int)cap_n; k++) {
        if (cap[k].svc == 5u && pus_get_u16(cap[k].data) == EVT_MODE_CHANGED &&
            cap[k].data[3] == MODE_SM_DEGRADED) {
            back = k;
        }
    }
    EXPECT(back >= 0 && cap[back].data[4] == (uint8_t)CAUSE_MONITOR);
}

/* Concurrency: the ground path (TC_RX) and the onboard schedule path both
 * dispatch telecommands; the tc lock must serialise them completely. */
#define CONC_N 40
static rtems_id done_sem;

static rtems_task conc_rx(rtems_task_argument a)
{
    int k;
    uint8_t f[PUS_MAX_PACKET];
    (void)a;
    for (k = 0; k < CONC_N; k++) {
        int n = pus_tc_build(APID_CMD, (uint16_t)(0x1000 + k), ACK_ACC_COMPL, 17, 1,
                             GROUND_ID, NULL, 0, f, sizeof(f));
        tmtc_dispatch(f, (size_t)n);
        (void)rtems_task_wake_after(1);
    }
    (void)rtems_semaphore_release(done_sem);
    rtems_task_exit();
}

static rtems_task conc_sched(rtems_task_argument a)
{
    int k;
    uint8_t f[PUS_MAX_PACKET];
    (void)a;
    for (k = 0; k < CONC_N; k++) {
        int n = pus_tc_build(APID_CMD, (uint16_t)(0x2000 + k), ACK_ACC_COMPL, 17, 1,
                             GROUND_ID, NULL, 0, f, sizeof(f));
        tmtc_lock();
        tmtc_dispatch_locked(f, (size_t)n);
        tmtc_unlock();
        (void)rtems_task_wake_after(1);
    }
    (void)rtems_semaphore_release(done_sem);
    rtems_task_exit();
}

static void it11_concurrent_dispatch(void)
{
    rtems_id t1, t2;
    size_t i;
    uint32_t acc = tmtc_tc_ctx()->accepted;
    int order_ok = 1;
    EXPECT(rtems_task_create(rtems_build_name('C', 'R', 'X', ' '), 10, 8u * 1024u,
                             RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES, &t1)
           == RTEMS_SUCCESSFUL);
    EXPECT(rtems_task_create(rtems_build_name('C', 'S', 'C', 'H'), 10, 8u * 1024u,
                             RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES, &t2)
           == RTEMS_SUCCESSFUL);
    (void)rtems_task_start(t1, conc_rx, 0);
    (void)rtems_task_start(t2, conc_sched, 0);
    EXPECT(rtems_semaphore_obtain(done_sem, RTEMS_WAIT, 3000) == RTEMS_SUCCESSFUL);
    EXPECT(rtems_semaphore_obtain(done_sem, RTEMS_WAIT, 3000) == RTEMS_SUCCESSFUL);
    EXPECT(cap_lost == 0u);
    EXPECT(cap_n == 6u * CONC_N);
    EXPECT(tmtc_tc_ctx()->accepted == acc + 2u * CONC_N);
    /* Every TC's three reports are adjacent and in order, and the APID's
     * sequence count never repeats or skips: no interleaving inside tc_handle. */
    for (i = 0; i + 2u < cap_n; i += 3u) {
        if (!(cap[i].svc == 1u && cap[i].sub == 1u && cap[i + 1u].svc == 17u &&
              cap[i + 2u].svc == 1u && cap[i + 2u].sub == 7u &&
              memcmp(cap[i].data, cap[i + 2u].data, PUS_REQUEST_ID_LEN) == 0)) {
            order_ok = 0;
        }
    }
    EXPECT(order_ok);
    for (i = 1; i < cap_n; i++) {
        EXPECT(cap[i].seq == (uint16_t)((cap[i - 1u].seq + 1u) & 0x3FFFu));
    }
}

/* Added after the Session 7 coverage analysis (coverage.md, C-02): every
 * acceptance check of the application services, through the real path.
 * Each malformed TC must be answered with TM(1,2) and the right code, and
 * not executed. */
typedef struct {
    uint8_t svc, sub;
    uint8_t data[8];
    size_t len;
    pus_fail_t code;
} bad_tc_t;

static void it12_service_acceptance_checks(void)
{
    static const bad_tc_t bad[] = {
        { SVC_FUNCTION, 2, { 0 }, 0, PUS_FAIL_SUBTYPE },                   /* TC(8,2)   */
        { SVC_SCHEDULE, 4, { 1, 0, 0, 0, 0 }, 5, PUS_FAIL_APP_DATA },      /* no TC     */
        { SVC_SCHEDULE, 9, { 0 }, 0, PUS_FAIL_SUBTYPE },                   /* TC(11,9)  */
        { SVC_MONITORING, 3, { 1, 0, 2 }, 3, PUS_FAIL_SUBTYPE },          /* TC(12,3)  */
        { SVC_MONITORING, 1, { 1 }, 1, PUS_FAIL_APP_DATA },               /* too short */
        { SVC_MONITORING, 1, { 2, 0, 2 }, 3, PUS_FAIL_APP_DATA },         /* N mismatch*/
        { SVC_MONITORING, 1, { 1, 0, 99 }, 3, PUS_FAIL_APP_DATA },        /* unknown   */
        { SVC_EVENT_ACTION, 8, { 0 }, 1, PUS_FAIL_APP_DATA },             /* data      */
        { SVC_EVENT_ACTION, 1, { 0 }, 0, PUS_FAIL_SUBTYPE },              /* TC(19,1)  */
        { SVC_EVENT_ACTION, 9, { 0 }, 1, PUS_FAIL_APP_DATA },             /* 1 octet   */
        { SVC_FAULT_INJECT, 1, { 0, 2, 0 }, 3, PUS_FAIL_APP_DATA },       /* short     */
        { SVC_FAULT_INJECT, 2, { 9, 1 }, 2, PUS_FAIL_APP_DATA },          /* task 9    */
        { SVC_FAULT_INJECT, 9, { 0 }, 0, PUS_FAIL_SUBTYPE },              /* TC(200,9) */
    };
    size_t k;
    for (k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
        cap_reset();
        tc_send(bad[k].svc, bad[k].sub, bad[k].len ? bad[k].data : NULL, bad[k].len);
        if (!(cap_n == 1u && is_report(0, 2) && cap[0].data[4] == (uint8_t)bad[k].code)) {
            printf("    TC(%u,%u) case %u: %u packet(s), code %u\n", bad[k].svc, bad[k].sub,
                   (unsigned)k, (unsigned)cap_n, cap_n ? cap[0].data[4] : 0u);
            failures_in_case++;
        }
    }
    /* Schedule disable and reset execute: TM(1,7), and the schedule is empty. */
    cap_reset();
    tc_send(SVC_SCHEDULE, 2, NULL, 0);
    tc_send(SVC_SCHEDULE, 3, NULL, 0);
    EXPECT(find(1, 7, 1) >= 0);
    EXPECT(sched_count(fdir_schedule()) == 0u);
    tc_send(SVC_SCHEDULE, 1, NULL, 0);             /* leave it enabled */
}

/* Added after the coverage analysis (coverage.md, C-03): a supervised task
 * that misses its heartbeat while FDIR actions are inhibited is reported
 * and not restarted. */
static void it13_restart_refused_under_inhibit(void)
{
    rtems_id dormant;
    uint8_t d[2];
    int ev;
    EXPECT(rtems_task_create(rtems_build_name('D', 'O', 'R', 'M'), 100, 8u * 1024u,
                             RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES, &dormant)
           == RTEMS_SUCCESSFUL);
    EXPECT(fdir_supervise("IT_TASK", 100, dormant, MODE_DEGRADED) >= 0);
    pus_put_u16(d, 60);
    tc_send(SVC_EVENT_ACTION, 9, d, 2);              /* inhibit 60 s */
    cap_reset();
    fdir_supervisor_cycle();                        /* no heartbeat: a miss */
    ev = find_event(SUP_EVT_ACTION_INHIBITED);
    EXPECT(find_event(SUP_EVT_HEARTBEAT_MISSED) >= 0);
    EXPECT(ev >= 0 && cap[ev].data[2] == 1u);       /* kind: restart */
    EXPECT(find_event(SUP_EVT_TASK_RESTARTED) < 0);
}

/* ---- Init --------------------------------------------------------------------------------- */

#ifdef INT_COVERAGE
extern void __gcov_dump(void);
#endif

rtems_task Init(rtems_task_argument arg)
{
    tc_ctx_t *ctx;
    (void)arg;

    printf("\nIntegration suite: telecommand path (%s)\n",
#ifdef __rtems__
           "RTEMS"
#else
           "host simulation"
#endif
           );
    if (rtems_semaphore_create(rtems_build_name('C', 'A', 'P', 'L'), 1,
                               RTEMS_BINARY_SEMAPHORE | RTEMS_PRIORITY |
                               RTEMS_INHERIT_PRIORITY, 0, &cap_lock) != RTEMS_SUCCESSFUL ||
        rtems_semaphore_create(rtems_build_name('D', 'O', 'N', 'E'), 0,
                               RTEMS_COUNTING_SEMAPHORE, 0, &done_sem) != RTEMS_SUCCESSFUL ||
        tmtc_init() != RTEMS_SUCCESSFUL || fdir_boot() != RTEMS_SUCCESSFUL) {
        printf("@@ INTEGRATION RESULT: FAIL 0/0 (set up) @@\n");
        rtems_shutdown_executive(1);
    }
    ctx = tmtc_tc_ctx();
    if (services_register(ctx) != 0) {
        printf("@@ INTEGRATION RESULT: FAIL 0/0 (services) @@\n");
        rtems_shutdown_executive(1);
    }
    ctx->sink = __wrap_tmtc_tm_sink;   /* tc_init() took the unwrapped address */

    run_case("IT-01", "TC(17,1): TM(1,1), TM(17,2), TM(1,7), CRC, sequence", it01_ping);
    run_case("IT-02", "corrupted CRC: TM(1,2) code 3, not executed, counted", it02_bad_crc);
    run_case("IT-03", "unknown service: TM(1,2) code 7", it03_unknown_service);
    run_case("IT-04", "wrong APID: TM(1,2) code 5", it04_wrong_apid);
    run_case("IT-05", "TC(8,1) invalid mode: TM(1,2) code 9, no change", it05_bad_mode_value);
    run_case("IT-06", "TC(8,1) SAFE: reports, mode event at next cycle", it06_ground_safe);
    run_case("IT-07", "two conflicting TC(8,1): one applied, other reported", it07_conflicting_ground_commands);
    run_case("IT-08", "TC(19,9): autonomous refused + reported, ground not", it08_inhibit_blocks_autonomous_only);
    run_case("IT-09", "TC(11,4): scheduled TC released through the same path", it09_scheduled_command);
    run_case("IT-10", "fault + ground NOMINAL in one cycle: fault kept (MM-130)", it10_fault_not_lost_to_ground_recovery);
    run_case("IT-11", "TC_RX and SCHED paths concurrently: serialised", it11_concurrent_dispatch);
    run_case("IT-12", "service acceptance checks: 13 malformed TCs, codes 8/9", it12_service_acceptance_checks);
    run_case("IT-13", "missed heartbeat under inhibit: reported, no restart", it13_restart_refused_under_inhibit);

#ifdef INT_COVERAGE
    __gcov_dump();    /* before the marker: the runner stops the image on it */
#endif
    printf("@@ INTEGRATION RESULT: %s %d/%d @@\n", cases_failed ? "FAIL" : "PASS",
           cases_run - cases_failed, cases_run);
    fflush(stdout);
    rtems_shutdown_executive(cases_failed ? 1u : 0u);
}
