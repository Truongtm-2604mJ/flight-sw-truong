/*
 * Lab 4.3 - host tests for the limit monitor, the housekeeping report
 * and the service 3 telecommands. Build and run: make test
 */
#include "../hk.h"
#include "../../lab-4.2-pus-service17/crc16.h"
#include "../../lab-4.1-space-packet/test_util.h"

/* ---- Capture sink ------------------------------------------------------- */

#define CAP_MAX 512
static uint8_t cap_pkt[CAP_MAX][PUS_MAX_PACKET];
static size_t  cap_len[CAP_MAX];
static int     cap_count;

static int capture_sink(const uint8_t *packet, size_t len, void *arg)
{
    (void)arg;
    if (cap_count < CAP_MAX) {
        memcpy(cap_pkt[cap_count], packet, len);
        cap_len[cap_count] = len;
    }
    cap_count++;
    return 0;
}

static uint64_t fake_ns;
static obt_t fake_now(void) { return obt_from_ns(fake_ns); }
static uint32_t fake_dropped(void) { return 3u; }

static unsigned tm_apid(int i)    { return ((cap_pkt[i][0] & 0x07u) << 8) | cap_pkt[i][1]; }
static unsigned tm_seq(int i)     { return ((cap_pkt[i][2] & 0x3Fu) << 8) | cap_pkt[i][3]; }
static unsigned tm_service(int i) { return cap_pkt[i][7]; }
static unsigned tm_subtype(int i) { return cap_pkt[i][8]; }
static const uint8_t *tm_data(int i) { return &cap_pkt[i][19]; }

static int count_tm(unsigned service, unsigned subtype)
{
    int i, n = 0;
    for (i = 0; i < cap_count && i < CAP_MAX; i++) {
        if (tm_service(i) == service && tm_subtype(i) == subtype) { n++; }
    }
    return n;
}

static tc_ctx_t tc;
static hk_ctx_t hk;

static void setup(void)
{
    cap_count = 0;
    fake_ns = 0;
    tc_init(&tc, 0x010, capture_sink, NULL, fake_now);
    hk_init(&hk, capture_sink, NULL, fake_now, &tc, fake_dropped);
    hk_register_service(&hk, &tc);
}

/* Run n housekeeping cycles, 500 ms of onboard time apart. */
static void run_cycles(int n)
{
    int i;
    for (i = 0; i < n; i++) {
        hk_cycle(&hk);
        fake_ns += 500000000ull;
    }
}

/* ---- Limit monitor -------------------------------------------------------- */

static void test_monitor_single_crossing(void)
{
    limit_mon_t m;
    limit_mon_init(&m, 4500, 200);

    CHECK_EQ(limit_mon_update(&m, 4400), LIMIT_MON_NO_CHANGE);
    CHECK_EQ(limit_mon_update(&m, 4500), LIMIT_MON_NO_CHANGE); /* not above */
    CHECK_EQ(limit_mon_update(&m, 4501), LIMIT_MON_WENT_HIGH);
    CHECK_EQ(limit_mon_update(&m, 4800), LIMIT_MON_NO_CHANGE); /* stays out */
    CHECK_EQ(limit_mon_update(&m, 4800), LIMIT_MON_NO_CHANGE);
    CHECK_EQ(m.state, LIMIT_MON_HIGH);
}

static void test_monitor_hysteresis_band(void)
{
    limit_mon_t m;
    int i;
    limit_mon_init(&m, 4500, 200);

    CHECK_EQ(limit_mon_update(&m, 4510), LIMIT_MON_WENT_HIGH);

    /* Chatter around the limit, inside the band: no events at all. */
    for (i = 0; i < 50; i++) {
        CHECK_EQ(limit_mon_update(&m, 4490), LIMIT_MON_NO_CHANGE);
        CHECK_EQ(limit_mon_update(&m, 4510), LIMIT_MON_NO_CHANGE);
        CHECK_EQ(limit_mon_update(&m, 4301), LIMIT_MON_NO_CHANGE);
    }
    CHECK_EQ(limit_mon_update(&m, 4300), LIMIT_MON_NO_CHANGE); /* not below */

    /* Below limit - hysteresis: one recovery, monitor re-armed. */
    CHECK_EQ(limit_mon_update(&m, 4299), LIMIT_MON_WENT_NOMINAL);
    CHECK_EQ(limit_mon_update(&m, 4299), LIMIT_MON_NO_CHANGE);
    CHECK_EQ(limit_mon_update(&m, 4499), LIMIT_MON_NO_CHANGE);

    /* A second, separate crossing gives a second event. */
    CHECK_EQ(limit_mon_update(&m, 4501), LIMIT_MON_WENT_HIGH);
}

static void test_monitor_extreme_values_do_not_overflow(void)
{
    limit_mon_t m;
    limit_mon_init(&m, -32768, 32767);
    CHECK_EQ(limit_mon_update(&m, -32767), LIMIT_MON_WENT_HIGH);
    CHECK_EQ(limit_mon_update(&m, -32768), LIMIT_MON_NO_CHANGE);
}

/*
 * The design trap, measured. Feed one full period of the simulated
 * battery temperature to three detectors and count the events each one
 * would have sent.
 */
static void test_event_flooding_naive_vs_hysteresis(void)
{
    sensors_sim_t sim;
    limit_mon_t m;
    int level_events = 0;     /* naive: event every cycle above the limit */
    int edge_events = 0;      /* edge detection, no hysteresis            */
    int high = 0, nominal = 0;
    int prev_above = 0;
    int i;

    sensors_sim_init(&sim);
    limit_mon_init(&m, TBAT_LIMIT_CDEG, TBAT_HYSTERESIS_CDEG);

    for (i = 0; i < 60; i++) { /* 60 samples = one triangle period, 30 s */
        sensor_values_t v;
        int above;

        sensors_sim_sample(&sim, &v);
        above = v.t_bat_cdeg > TBAT_LIMIT_CDEG;

        if (above) { level_events++; }
        if (above && !prev_above) { edge_events++; }
        prev_above = above;

        switch (limit_mon_update(&m, v.t_bat_cdeg)) {
        case LIMIT_MON_WENT_HIGH:    high++;    break;
        case LIMIT_MON_WENT_NOMINAL: nominal++; break;
        default: break;
        }
    }

    printf("    one 30 s temperature cycle: level detection %d events, "
           "edge only %d, edge + hysteresis %d\n",
           level_events, edge_events, high);

    CHECK(level_events >= 20);  /* a flood                               */
    CHECK(edge_events >= 2);    /* still repeats: noise re-crosses limit */
    CHECK_EQ(high, 1);          /* exactly once per crossing             */
    CHECK_EQ(nominal, 1);
}

/* ---- Housekeeping report ---------------------------------------------------- */

static void test_hk_pack_known_octets(void)
{
    /*
     *  01          SID 1
     *  6D 60       V_BUS 28000 mV
     *  11 94       T_BAT 45.00 C = 4500
     *  FF 9C       T_OBC -1.00 C = -100 (two's complement)
     *  03          MODE: enabled | T_BAT high
     *  00 02       TC_REJECTED 2
     *  01 00       TC_ACCEPTED 256
     *  00 03       TM_DROPPED 3
     *  00 01 51 80 UPTIME 86400 s
     *  04 D2       HK_EXEC 1234 us
     */
    static const uint8_t expected[HK_REPORT_LEN] = {
        0x01, 0x6D, 0x60, 0x11, 0x94, 0xFF, 0x9C, 0x03, 0x00, 0x02,
        0x01, 0x00, 0x00, 0x03, 0x00, 0x01, 0x51, 0x80, 0x04, 0xD2
    };
    hk_sample_t s;
    uint8_t out[HK_REPORT_LEN];

    memset(&s, 0, sizeof(s));
    s.vbus_mv = 28000; s.t_bat_cdeg = 4500; s.t_obc_cdeg = -100;
    s.mode = 0x03; s.tc_rejected = 2; s.tc_accepted = 256; s.tm_dropped = 3;
    s.uptime_s = 86400; s.hk_exec_us = 1234;

    hk_pack(&s, out);
    CHECK_MEM(out, expected, sizeof(expected));
}

static void test_hk_report_packet(void)
{
    setup();
    fake_ns = 2500000000ull; /* acquisition at 2.5 s */
    hk.last_exec_us = 777;
    hk_cycle(&hk);

    CHECK_EQ(cap_count, 1);
    CHECK_EQ(tm_apid(0), APID_HK);
    CHECK_EQ(tm_service(0), 3);
    CHECK_EQ(tm_subtype(0), 25);
    CHECK_EQ(cap_len[0], 6u + 13u + HK_REPORT_LEN + 2u);
    CHECK_EQ((cap_pkt[0][4] << 8) | cap_pkt[0][5], 13 + HK_REPORT_LEN + 2 - 1);
    CHECK_EQ(crc16_ccitt(cap_pkt[0], cap_len[0]), 0); /* CRC residue */

    /* Packet time is the acquisition time: 00 00 00 02 80 00. */
    {
        static const uint8_t t[6] = { 0x00, 0x00, 0x00, 0x02, 0x80, 0x00 };
        CHECK_MEM(&cap_pkt[0][13], t, 6);
    }
    CHECK_EQ(tm_data(0)[0], HK_SID_PLATFORM);
    CHECK_EQ(tm_data(0)[7] & HK_MODE_ENABLED, HK_MODE_ENABLED);
    CHECK_EQ(pus_get_u16(&tm_data(0)[12]), 3);   /* TM_DROPPED */
    CHECK_EQ(tm_data(0)[17], 2);                 /* UPTIME low octet */
    CHECK_EQ(pus_get_u16(&tm_data(0)[18]), 777); /* HK_EXEC */
}

/* ---- Enable / disable telecommands ------------------------------------------ */

static int send_tc(uint16_t seq, uint8_t service, uint8_t subtype,
                   const uint8_t *app, size_t app_len)
{
    uint8_t pkt[PUS_MAX_PACKET];
    int before = cap_count;
    int n = pus_tc_build(0x010, seq, 0x9, service, subtype, 1, app, app_len,
                         pkt, sizeof(pkt));
    tc_handle(&tc, pkt, (size_t)n);
    return cap_count - before;
}

static void test_enable_disable_controls_generation(void)
{
    static const uint8_t sid1[2] = { 1, HK_SID_PLATFORM };
    int before;

    setup();
    run_cycles(4);
    CHECK_EQ(count_tm(3, 25), 4);                    /* enabled at boot */

    CHECK_EQ(send_tc(0, 3, 6, sid1, 2), 2);          /* TM(1,1), TM(1,7) */
    CHECK_EQ(tm_subtype(cap_count - 2), 1);
    CHECK_EQ(tm_subtype(cap_count - 1), 7);
    CHECK_EQ(hk.enabled, 0);

    before = count_tm(3, 25);
    run_cycles(6);
    CHECK_EQ(count_tm(3, 25), before);               /* silent */

    CHECK_EQ(send_tc(1, 3, 5, sid1, 2), 2);
    CHECK_EQ(hk.enabled, 1);
    run_cycles(3);
    CHECK_EQ(count_tm(3, 25), before + 3);           /* resumed */
}

static void test_hk_sequence_has_no_gap_across_disable(void)
{
    static const uint8_t sid1[2] = { 1, HK_SID_PLATFORM };
    unsigned expected = 0;
    int i;

    setup();
    run_cycles(3);
    send_tc(0, 3, 6, sid1, 2);
    run_cycles(5);
    send_tc(1, 3, 5, sid1, 2);
    run_cycles(3);

    /* Packets that were never generated do not consume counts, so the
     * ground does not mistake a commanded pause for packet loss. */
    for (i = 0; i < cap_count; i++) {
        if (tm_apid(i) == APID_HK) {
            CHECK_EQ(tm_seq(i), expected);
            expected++;
        }
    }
    CHECK_EQ(expected, 6);
}

static void test_service3_rejects_bad_requests(void)
{
    static const uint8_t two_sids[3]  = { 2, 1, 2 };
    static const uint8_t wrong_sid[2] = { 1, 9 };
    static const uint8_t sid1[2]      = { 1, HK_SID_PLATFORM };

    setup();
    CHECK_EQ(send_tc(0, 3, 6, wrong_sid, 2), 1);
    CHECK_EQ(tm_subtype(cap_count - 1), 2);
    CHECK_EQ(tm_data(cap_count - 1)[4], PUS_FAIL_APP_DATA);

    CHECK_EQ(send_tc(1, 3, 6, two_sids, 3), 1);
    CHECK_EQ(tm_data(cap_count - 1)[4], PUS_FAIL_APP_DATA);

    CHECK_EQ(send_tc(2, 3, 6, NULL, 0), 1);
    CHECK_EQ(tm_data(cap_count - 1)[4], PUS_FAIL_APP_DATA);

    CHECK_EQ(send_tc(3, 3, 1, sid1, 2), 1);          /* TC(3,1) not offered */
    CHECK_EQ(tm_data(cap_count - 1)[4], PUS_FAIL_SUBTYPE);

    CHECK_EQ(hk.enabled, 1);                         /* nothing was executed */
}

/* ---- Events through the full cycle -------------------------------------------- */

static void test_one_event_per_crossing_over_three_periods(void)
{
    int i;
    int above = 0;

    setup();
    run_cycles(180); /* 3 temperature periods = 90 s */

    for (i = 0; i < cap_count && i < CAP_MAX; i++) {
        if (tm_apid(i) == APID_HK &&
            (int16_t)pus_get_u16(&tm_data(i)[3]) > TBAT_LIMIT_CDEG) {
            above++;
        }
    }

    CHECK_EQ(count_tm(3, 25), 180);
    CHECK(above >= 60);                 /* many cycles out of limits...  */
    CHECK_EQ(count_tm(5, 2), 3);        /* ...three crossings, 3 events  */
    CHECK_EQ(count_tm(5, 1), 3);
}

static void test_event_packet_contents(void)
{
    int i, ev = -1, hk_same_cycle = -1;

    setup();
    run_cycles(30);
    for (i = 0; i < cap_count; i++) {
        if (tm_service(i) == 5 && ev < 0) { ev = i; hk_same_cycle = i + 1; }
    }

    CHECK(ev >= 0);
    CHECK_EQ(tm_apid(ev), APID_EVENT);
    CHECK_EQ(tm_seq(ev), 0);
    CHECK_EQ(tm_subtype(ev), PUS_TM_5_LOW_SEVERITY);
    CHECK_EQ(cap_len[ev], 6u + 13u + EVT_AUX_LEN + 2u);
    CHECK_EQ(pus_get_u16(&tm_data(ev)[0]), EVT_TEMP_BAT_HIGH);
    CHECK_EQ(tm_data(ev)[2], PARAM_ID_T_BAT);
    CHECK((int16_t)pus_get_u16(&tm_data(ev)[3]) > TBAT_LIMIT_CDEG);
    CHECK_EQ(pus_get_u16(&tm_data(ev)[5]), TBAT_LIMIT_CDEG);
    CHECK_EQ(crc16_ccitt(cap_pkt[ev], cap_len[ev]), 0);

    /* The event and the HK report of the same cycle describe the same
     * sample: same timestamp, same value, and the mode bit is set. */
    CHECK_EQ(tm_service(hk_same_cycle), 3);
    CHECK_MEM(&cap_pkt[ev][13], &cap_pkt[hk_same_cycle][13], 6);
    CHECK_MEM(&tm_data(ev)[3], &tm_data(hk_same_cycle)[3], 2);
    CHECK_EQ(tm_data(hk_same_cycle)[7] & HK_MODE_TBAT_HIGH, HK_MODE_TBAT_HIGH);
}

static void test_monitoring_continues_while_hk_is_disabled(void)
{
    static const uint8_t sid1[2] = { 1, HK_SID_PLATFORM };

    setup();
    send_tc(0, 3, 6, sid1, 2);
    run_cycles(60);

    CHECK_EQ(count_tm(3, 25), 0);  /* no housekeeping */
    CHECK_EQ(count_tm(5, 2), 1);   /* but the anomaly is still reported */
    CHECK_EQ(count_tm(5, 1), 1);
}

int main(void)
{
    printf("Lab 4.3 - housekeeping and event reporting tests\n\n");
    RUN(test_monitor_single_crossing);
    RUN(test_monitor_hysteresis_band);
    RUN(test_monitor_extreme_values_do_not_overflow);
    RUN(test_event_flooding_naive_vs_hysteresis);
    RUN(test_hk_pack_known_octets);
    RUN(test_hk_report_packet);
    RUN(test_enable_disable_controls_generation);
    RUN(test_hk_sequence_has_no_gap_across_disable);
    RUN(test_service3_rejects_bad_requests);
    RUN(test_one_event_per_crossing_over_three_periods);
    RUN(test_event_packet_contents);
    RUN(test_monitoring_continues_while_hk_is_disabled);
    return TEST_SUMMARY();
}
