/* Lab 8.1 - Telecommand authentication and anti-replay. See tc_auth.h. */
#include "tc_auth.h"

#include <string.h>

#include <tinycrypt/utils.h>

void tc_auth_init(tc_auth_t *a, keystore_t *ks, tc_auth_persist_fn persist,
                  void *persist_arg)
{
    (void)memset(a, 0, sizeof(*a));
    a->ks = ks;
    a->persist = persist;
    a->persist_arg = persist_arg;
    a->cur_spi = SEC_SPI_NONE;
    a->hook.verify = tc_auth_verify;
    a->hook.commit = tc_auth_commit;
    a->hook.reject = tc_auth_reject;
    a->hook.arg = a;
}

const tc_security_t *tc_auth_hook(tc_auth_t *a)
{
    return &a->hook;
}

uint8_t tc_auth_current_spi(const tc_auth_t *a)
{
    return a->cur_spi;
}

sec_rej_t tc_auth_class(pus_fail_t code)
{
    switch (code) {
    case PUS_FAIL_CHECKSUM:  return SEC_REJ_CRC;
    case PUS_FAIL_AUTH:      return SEC_REJ_AUTH;
    case PUS_FAIL_REPLAY:    return SEC_REJ_REPLAY;
    case PUS_FAIL_WINDOW:    return SEC_REJ_WINDOW;
    case PUS_FAIL_SEC_STATE: return SEC_REJ_STATE;
    case PUS_OK:
    case PUS_FAIL_SP_VERSION:
    case PUS_FAIL_LENGTH:
    case PUS_FAIL_PACKET_HEADER:
    case PUS_FAIL_APID:
    case PUS_FAIL_PUS_VERSION:
    case PUS_FAIL_SERVICE:
    case PUS_FAIL_SUBTYPE:
    case PUS_FAIL_APP_DATA:
    case PUS_FAIL_EXECUTION:
    case PUS_FAIL_NO_REPORT:
    default:
        return SEC_REJ_STRUCTURE;
    }
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Steps 3 (authentication) and 4 (freshness). Nothing is changed here
 * except the shortened application data length: the counter moves in
 * commit(), once the command has passed every check. */
pus_fail_t tc_auth_verify(void *arg, const uint8_t *frame, size_t frame_len,
                          pus_tc_t *tc, uint32_t *token)
{
    tc_auth_t *a = (tc_auth_t *)arg;
    const uint8_t *trailer;
    uint8_t mac[KS_MAC_LEN];
    size_t covered;
    uint8_t spi;
    uint32_t seq, last;
    int r;

    (void)frame_len;  /* pus_tc_validate() already checked it */
    a->cur_spi = SEC_SPI_NONE;
    *token = 0u;

    if (a->trusted) {
        return PUS_OK;    /* authenticated when it was uplinked (TC(11,4)) */
    }
    if (tc->app_len < SEC_TRAILER_LEN) {
        return PUS_FAIL_APP_DATA;           /* structural: no trailer */
    }
    trailer = &tc->app_data[tc->app_len - SEC_TRAILER_LEN];
    spi = trailer[0];
    seq = get_u32(&trailer[1]);
    covered = (size_t)(trailer - frame) + 5u;

    /* 3. Authentication. An SPI without a usable key cannot be checked;
     * it is a state rejection, not an authentication failure. */
    if (!ks_usable(a->ks, spi) ||
        (spi != KS_RECOVERY && spi != a->ks->active)) {
        return PUS_FAIL_SEC_STATE;
    }
    if (ks_mac(a->ks, spi, frame, covered, mac) != 0 ||
        _compare(mac, &trailer[5], KS_MAC_LEN) != 0) {   /* constant time */
        return PUS_FAIL_AUTH;
    }

    /* Authorisation: the recovery key opens two commands, nothing else. */
    if (spi == KS_RECOVERY &&
        !(tc->service == SEC_SVC &&
          (tc->subtype == SEC_TC_RECOVERY_COUNTER ||
           tc->subtype == SEC_TC_RECOVERY_ACTIVATE))) {
        return PUS_FAIL_SEC_STATE;
    }

    /* 4. Freshness. */
    if (spi == KS_RECOVERY) {
        r = ks_challenge(a->ks, &last);
        if (r > 0) {
            a->vote_repairs |= 1u;
        }
        if (seq != last) {
            return (seq < last) ? PUS_FAIL_REPLAY : PUS_FAIL_WINDOW;
        }
    } else {
        if (ks_counter_untrusted(a->ks, spi)) {
            return PUS_FAIL_SEC_STATE;    /* factory state: first contact first */
        }
        r = ks_counter(a->ks, spi, &last);
        if (r > 0) {
            a->vote_repairs |= (uint8_t)(1u << spi);
        }
        if (seq <= last) {
            return PUS_FAIL_REPLAY;
        }
        if (seq - last > KS_WINDOW) {
            return PUS_FAIL_WINDOW;   /* policy: reject, never jump ahead */
        }
    }

    tc->app_len -= SEC_TRAILER_LEN;   /* services see their own data only */
    a->cur_spi = spi;
    *token = seq;
    return PUS_OK;
}

/* Step 6 begins: the command passed every check. Advance and persist. */
void tc_auth_commit(void *arg, const pus_tc_t *tc, uint32_t token)
{
    tc_auth_t *a = (tc_auth_t *)arg;

    (void)tc;
    if (a->cur_spi == SEC_SPI_NONE) {
        return;                         /* released from the schedule */
    }
    if (a->cur_spi == KS_RECOVERY) {
        ks_challenge_set(a->ks, token + 1u);   /* one use per challenge */
    } else {
        ks_counter_set(a->ks, a->cur_spi, token);
    }
    a->auth_accepted++;
    if (a->persist != NULL) {
        a->persist(a->ks, a->persist_arg);
    }
}

void tc_auth_reject(void *arg, pus_fail_t code)
{
    tc_auth_t *a = (tc_auth_t *)arg;
    sec_rej_t c = tc_auth_class(code);

    a->rej_total[c]++;
    a->rej_last_code[c] = (uint8_t)code;
    a->cur_spi = SEC_SPI_NONE;
}
