/* Lab 8.2 - Service 210, key management. See sec_service.h. */
#include "sec_service.h"

#include <string.h>

#define LOAD_LEN (1u + KS_NONCE_LEN + KS_WRAPPED_LEN)

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void sec_service_init(sec_service_t *s, tc_auth_t *auth)
{
    (void)memset(s, 0, sizeof(*s));
    s->auth = auth;
}

void sec_service_queue(sec_service_t *s, uint8_t severity, const uint8_t *aux,
                       size_t len)
{
    unsigned i;

    if (s->ev_count >= SEC_EVQ_DEPTH || len > SEC_EV_MAX) {
        s->ev_dropped++;
        return;
    }
    i = (s->ev_head + s->ev_count) % SEC_EVQ_DEPTH;
    (void)memcpy(s->ev[i], aux, len);
    s->ev_len[i] = (uint8_t)len;
    s->ev_sev[i] = severity;
    s->ev_count++;
}

size_t sec_service_take(sec_service_t *s, uint8_t *severity,
                        uint8_t out[SEC_EV_MAX])
{
    size_t len;

    if (s->ev_count == 0u) {
        return 0u;
    }
    len = s->ev_len[s->ev_head];
    (void)memcpy(out, s->ev[s->ev_head], len);
    *severity = s->ev_sev[s->ev_head];
    s->ev_head = (s->ev_head + 1u) % SEC_EVQ_DEPTH;
    s->ev_count--;
    return len;
}

static void event(sec_service_t *s, uint8_t sev, uint16_t id,
                  const uint8_t *data, size_t n)
{
    uint8_t aux[SEC_EV_MAX];
    aux[0] = (uint8_t)(id >> 8);
    aux[1] = (uint8_t)id;
    (void)memcpy(&aux[2], data, n);
    sec_service_queue(s, sev, aux, n + 2u);
}

static int op_slot(uint8_t slot)
{
    return slot == 1u || slot == 2u;
}

/* Acceptance: subtype, length, and the key store state the command needs.
 * Reads the key store, changes nothing. */
pus_fail_t sec_service_check(const pus_tc_t *tc, void *arg)
{
    const sec_service_t *s = (const sec_service_t *)arg;
    const keystore_t *ks = s->auth->ks;
    uint8_t spi = tc_auth_current_spi(s->auth);
    uint8_t slot;

    if (tc->app_len < 1u) {
        return (tc->subtype == SEC_TC_LOAD_KEY || tc->subtype == SEC_TC_ACTIVATE ||
                tc->subtype == SEC_TC_ERASE || tc->subtype == SEC_TC_RECOVERY_COUNTER ||
                tc->subtype == SEC_TC_RECOVERY_ACTIVATE) ? PUS_FAIL_APP_DATA
                                                         : PUS_FAIL_SUBTYPE;
    }
    slot = tc->app_data[0];

    switch (tc->subtype) {
    case SEC_TC_LOAD_KEY:
        if (tc->app_len != LOAD_LEN || !op_slot(slot) || slot == ks->active) {
            return PUS_FAIL_APP_DATA;
        }
        return PUS_OK;
    case SEC_TC_ACTIVATE:
        if (tc->app_len != 1u || !op_slot(slot) || slot == ks->active ||
            ks->slot[slot].state != (uint8_t)KS_LOADED) {
            return PUS_FAIL_APP_DATA;
        }
        return PUS_OK;
    case SEC_TC_ERASE:
        if (tc->app_len != 1u || !op_slot(slot) || slot == ks->active) {
            return PUS_FAIL_APP_DATA;
        }
        return PUS_OK;
    case SEC_TC_RECOVERY_COUNTER:
        if (spi != KS_RECOVERY) {
            return PUS_FAIL_SEC_STATE;     /* e.g. released from the schedule */
        }
        if (tc->app_len != 5u || !op_slot(slot)) {
            return PUS_FAIL_APP_DATA;
        }
        return PUS_OK;
    case SEC_TC_RECOVERY_ACTIVATE:
        if (spi != KS_RECOVERY) {
            return PUS_FAIL_SEC_STATE;
        }
        if (tc->app_len != 1u || !op_slot(slot) || slot == ks->active ||
            ks->slot[slot].state != (uint8_t)KS_LOADED) {
            return PUS_FAIL_APP_DATA;
        }
        return PUS_OK;
    default:
        return PUS_FAIL_SUBTYPE;
    }
}

static void persist(const sec_service_t *s)
{
    tc_auth_t *a = s->auth;
    if (a->persist != NULL) {
        a->persist(a->ks, a->persist_arg);
    }
}

static pus_fail_t do_activate(sec_service_t *s, uint8_t slot, uint8_t recovery)
{
    keystore_t *ks = s->auth->ks;
    uint8_t d[7];
    uint8_t prev = ks->active;

    if (ks_activate(ks, slot) != 0) {
        return PUS_FAIL_EXECUTION;
    }
    d[0] = slot;
    d[1] = prev;
    put32(&d[2], ks->slot[slot].kcv[0]);
    d[6] = recovery;
    event(s, 2u, SEC_EVT_KEY_ACTIVATED, d, sizeof(d));
    return PUS_OK;
}

pus_fail_t sec_service_exec(const pus_tc_t *tc, struct tc_ctx *ctx, void *arg)
{
    sec_service_t *s = (sec_service_t *)arg;
    keystore_t *ks = s->auth->ks;
    uint8_t slot = tc->app_data[0];
    uint8_t d[9];
    pus_fail_t r = PUS_OK;
    uint32_t old;

    (void)ctx;
    switch (tc->subtype) {
    case SEC_TC_LOAD_KEY:
        if (ks_load(ks, slot, &tc->app_data[1], &tc->app_data[1u + KS_NONCE_LEN])
            != 0) {
            d[0] = slot;
            event(s, 3u, SEC_EVT_KEY_LOAD_FAILED, d, 1u);
            r = PUS_FAIL_EXECUTION;
        } else {
            d[0] = slot;
            put32(&d[1], ks->slot[slot].kcv[0]);
            event(s, 1u, SEC_EVT_KEY_LOADED, d, 5u);
        }
        break;
    case SEC_TC_ACTIVATE:
        r = do_activate(s, slot, 0u);
        break;
    case SEC_TC_ERASE:
        if (ks_erase(ks, slot) != 0) {
            r = PUS_FAIL_EXECUTION;
        } else {
            d[0] = slot;
            event(s, 1u, SEC_EVT_KEY_ERASED, d, 1u);
        }
        break;
    case SEC_TC_RECOVERY_COUNTER:
    {
        uint32_t floor_value = get32(&tc->app_data[1]);
        uint32_t used;

        (void)ks_challenge(ks, &used);  /* commit() already moved it on */
        d[0] = SEC_TC_RECOVERY_COUNTER;
        d[1] = slot;
        put32(&d[2], used - 1u);        /* the challenge this command used */
        event(s, 3u, SEC_EVT_RECOVERY_USED, d, 6u);

        (void)ks_counter(ks, slot, &old);
        ks_counter_floor(ks, slot, floor_value);  /* trusted from now on */
        d[0] = slot;
        put32(&d[1], old);
        put32(&d[5], floor_value);
        event(s, 2u, SEC_EVT_COUNTER_SET, d, 9u);
        break;
    }
    case SEC_TC_RECOVERY_ACTIVATE:
        d[0] = SEC_TC_RECOVERY_ACTIVATE;
        d[1] = slot;
        (void)ks_challenge(ks, &old);
        put32(&d[2], old - 1u);
        event(s, 3u, SEC_EVT_RECOVERY_USED, d, 6u);
        r = do_activate(s, slot, 1u);
        break;
    default:
        r = PUS_FAIL_SUBTYPE;    /* not reachable: check() filtered it */
        break;
    }
    persist(s);
    return r;
}

int sec_service_register(sec_service_t *s, tc_ctx_t *ctx)
{
    return tc_register_service(ctx, (uint8_t)SEC_SVC, sec_service_check,
                               sec_service_exec, s);
}
