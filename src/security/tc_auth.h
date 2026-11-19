/*
 * Lab 8.1 - Telecommand authentication and anti-replay.
 *
 * Plugs into the Session 4 handler through tc_ctx_t.security, so the
 * Session 4 order of checks is kept and extended:
 *
 *   1 structural   pus_tc_validate(): version, length, header, APID   (S4)
 *   2 checksum     pus_tc_validate(): CRC-16                          (S4)
 *   3 authentication  tc_auth verify(): HMAC-SHA-256/128 over the packet
 *   4 freshness       tc_auth verify(): anti-replay counter, window
 *   5 semantic     service, subtype, application data                 (S4)
 *   6 execute      after commit(): the counter advances only here
 *
 * Secure telecommand (the "trailer" is inside the packet, before the CRC,
 * and the packet length field counts it):
 *
 *   | primary 6 | PUS sec hdr 5 | app data | SPI 1 | SEQ 4 | MAC 16 | CRC 2 |
 *   \_________________ covered by the MAC _______________/
 *
 *   SPI  security parameter index = key slot: 0 recovery, 1 or 2 operational
 *   SEQ  big endian. SPI 1/2: anti-replay counter of that slot, must be
 *        above the last accepted value and at most KS_WINDOW above it.
 *        SPI 0: must equal the recovery challenge published in HK SID 3.
 *   MAC  first 16 octets of HMAC-SHA-256(key[SPI], primary .. SEQ)
 *
 * The checksum is checked before the MAC on purpose: a CRC costs a few
 * microseconds, an HMAC about a millisecond on the target, and a CRC
 * failure is noise, not an attack, so it is classified separately.
 *
 * Rejections up to and including freshness are counted by class and get
 * no TM(1,2) (reject() below); the class counters feed the rate limited
 * reporter (sec_report.h). Rejections after authentication are normal
 * PUS failures with TM(1,2): the sender has proven who it is.
 */
#ifndef TC_AUTH_H
#define TC_AUTH_H

#include <stddef.h>
#include <stdint.h>

#include "keystore.h"
#include "tc_handler.h"

#define SEC_TRAILER_LEN (1u + 4u + KS_MAC_LEN)
#define SEC_SPI_NONE    0xFFu     /* released from the onboard schedule */

/* Service 210, key management (lab-8.2). The recovery key (SPI 0) is
 * accepted for these two subtypes and nothing else. */
#define SEC_SVC                 210u
#define SEC_TC_LOAD_KEY         1u
#define SEC_TC_ACTIVATE         2u
#define SEC_TC_ERASE            3u
#define SEC_TC_RECOVERY_COUNTER 10u
#define SEC_TC_RECOVERY_ACTIVATE 11u

typedef enum {
    SEC_REJ_STRUCTURE = 0,  /* packet level checks 1, 2, 4..6, short trailer */
    SEC_REJ_CRC       = 1,
    SEC_REJ_AUTH      = 2,  /* MAC does not verify                          */
    SEC_REJ_REPLAY    = 3,  /* counter not above the last accepted           */
    SEC_REJ_WINDOW    = 4,  /* counter too far ahead                         */
    SEC_REJ_STATE     = 5,  /* SPI unknown/not usable, counter corrupt,
                               recovery key used for a normal command        */
    SEC_REJ_CLASSES   = 6
} sec_rej_t;

/* Called after every change of the key store that must survive a reset. */
typedef void (*tc_auth_persist_fn)(const keystore_t *ks, void *arg);

typedef struct {
    keystore_t        *ks;
    tc_auth_persist_fn persist;
    void              *persist_arg;
    tc_security_t      hook;

    /* Written by the telecommand path only, under the tc lock. */
    uint32_t rej_total[SEC_REJ_CLASSES];
    uint8_t  rej_last_code[SEC_REJ_CLASSES];
    uint32_t auth_accepted;           /* authenticated and accepted        */
    int      trusted;                 /* 1 while the schedule releases a TC */
    uint8_t  cur_spi;                 /* SPI of the TC being processed     */
    uint8_t  vote_repairs;            /* bit n: counter of slot n had no
                                         majority; bit 0: the challenge.
                                         Cleared by the reporter.          */
} tc_auth_t;

void tc_auth_init(tc_auth_t *a, keystore_t *ks, tc_auth_persist_fn persist,
                  void *persist_arg);

/* The hook to put in tc_ctx_t.security. */
const tc_security_t *tc_auth_hook(tc_auth_t *a);

/* Class of a code passed to reject(). */
sec_rej_t tc_auth_class(pus_fail_t code);

/* SPI of the telecommand currently between verify() and the end of
 * tc_handle() (for the service 210 checks); SEC_SPI_NONE if trusted. */
uint8_t tc_auth_current_spi(const tc_auth_t *a);

/* Exposed for the unit tests. */
pus_fail_t tc_auth_verify(void *arg, const uint8_t *frame, size_t frame_len,
                          pus_tc_t *tc, uint32_t *token);
void tc_auth_commit(void *arg, const pus_tc_t *tc, uint32_t token);
void tc_auth_reject(void *arg, pus_fail_t code);

#endif /* TC_AUTH_H */
