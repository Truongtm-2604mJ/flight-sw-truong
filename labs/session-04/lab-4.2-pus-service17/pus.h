/*
 * Lab 4.2 - PUS packet layer on top of the Lab 4.1 Space Packet.
 *
 * Layout follows the structure of ECSS-E-ST-70-41C (PUS-C). Field widths
 * that the standard leaves to the mission are fixed here and listed in
 * docs/tmtc-design.md, section 6.
 *
 * Telecommand packet data field:
 *   octet 0     : TC PUS version (4 bits, = 2) | acknowledgement flags (4)
 *   octet 1     : service type
 *   octet 2     : message subtype
 *   octets 3-4  : source ID
 *   ...         : application data (may be empty)
 *   last 2      : CRC-16 over every preceding octet of the packet
 *
 * Telemetry packet data field:
 *   octet 0     : TM PUS version (4 bits, = 2) | time reference status (4)
 *   octet 1     : service type
 *   octet 2     : message subtype
 *   octets 3-4  : message type counter (not used, always 0)
 *   octets 5-6  : destination ID
 *   octets 7-12 : time, CUC 4 octets seconds + 2 octets of 1/65536 s
 *   ...         : source data (may be empty)
 *   last 2      : CRC-16 over every preceding octet of the packet
 *
 * Portable C99. No OS calls, no dynamic memory.
 */
#ifndef PUS_H
#define PUS_H

#include <stdint.h>
#include <stddef.h>

#include "sp.h"

#define PUS_VERSION          2u
#define PUS_TC_SEC_HDR_LEN   5u
#define PUS_TM_SEC_HDR_LEN   13u
#define PUS_CRC_LEN          2u
#define PUS_TC_MIN_LEN       (SP_PRIMARY_HEADER_LEN + PUS_TC_SEC_HDR_LEN + PUS_CRC_LEN)
#define PUS_TM_MIN_LEN       (SP_PRIMARY_HEADER_LEN + PUS_TM_SEC_HDR_LEN + PUS_CRC_LEN)

/* Largest packet this software builds or accepts. Chosen so one packet,
 * hex encoded, fits a 256 character console line (see link framing). */
#define PUS_MAX_PACKET       120u
#define PUS_TM_MAX_DATA      (PUS_MAX_PACKET - PUS_TM_MIN_LEN)
#define PUS_TC_MAX_DATA      (PUS_MAX_PACKET - PUS_TC_MIN_LEN)

/* Acknowledgement flags in the TC secondary header. */
#define PUS_ACK_ACCEPTANCE   0x1u
#define PUS_ACK_START        0x2u
#define PUS_ACK_PROGRESS     0x4u
#define PUS_ACK_COMPLETION   0x8u

/* Request ID carried by every service 1 report: the first four octets
 * of the telecommand primary header, copied as received. */
#define PUS_REQUEST_ID_LEN   4u

/*
 * Onboard time, CUC unsegmented: seconds and 1/65536 s since the epoch.
 * Lab epoch is application start (mission elapsed time), see design note.
 */
typedef struct {
    uint32_t coarse;
    uint16_t fine;
} obt_t;

obt_t obt_from_ns(uint64_t ns_since_epoch);

/*
 * One telemetry source = one APID = one sequence counter.
 * Each tm_source_t must be used by exactly one task. That rule is what
 * keeps the counters correct without a lock.
 */
typedef struct {
    uint16_t apid;
    uint16_t seq_count;
} tm_source_t;

/* A validated telecommand. Pointers refer into the received frame. */
typedef struct {
    sp_primary_header_t sp;
    uint8_t  ack_flags;
    uint8_t  service;
    uint8_t  subtype;
    uint16_t source_id;
    const uint8_t *app_data;
    size_t   app_len;
    uint8_t  request_id[PUS_REQUEST_ID_LEN];
} pus_tc_t;

/*
 * Failure codes reported in TM(1,2) and TM(1,8) failure notices.
 * Values 1..9 are assigned in the order the checks run.
 */
typedef enum {
    PUS_OK                    = 0,
    PUS_FAIL_SP_VERSION       = 1, /* packet version number is not 0       */
    PUS_FAIL_LENGTH           = 2, /* length field disagrees with the frame */
    PUS_FAIL_CHECKSUM         = 3, /* CRC mismatch                          */
    PUS_FAIL_PACKET_HEADER    = 4, /* not a TC, no sec. header, segmented   */
    PUS_FAIL_APID             = 5, /* not addressed to this application     */
    PUS_FAIL_PUS_VERSION      = 6, /* PUS version is not 2                  */
    PUS_FAIL_SERVICE          = 7, /* service type not implemented          */
    PUS_FAIL_SUBTYPE          = 8, /* subtype not implemented               */
    PUS_FAIL_APP_DATA         = 9, /* application data wrong size or value  */
    PUS_FAIL_EXECUTION        = 16,/* accepted, but execution failed        */
    PUS_FAIL_NO_REPORT        = 255/* too short to carry a request ID       */
} pus_fail_t;

/*
 * Build a complete TM packet (primary header, secondary header, data,
 * CRC) into out. Uses and then increments src->seq_count (wraps at
 * 2^14). Returns the total packet length or a negative SP_ERR_* code,
 * in which case the counter is not consumed.
 */
int pus_tm_build(tm_source_t *src, uint8_t service, uint8_t subtype,
                 uint16_t dest_id, obt_t time,
                 const uint8_t *data, size_t data_len,
                 uint8_t *out, size_t out_cap);

/* Build a complete TC packet. Used by the onboard unit tests; the ground
 * station has its own independent encoder in Python. */
int pus_tc_build(uint16_t apid, uint16_t seq_count, uint8_t ack_flags,
                 uint8_t service, uint8_t subtype, uint16_t source_id,
                 const uint8_t *app_data, size_t app_len,
                 uint8_t *out, size_t out_cap);

/*
 * Packet level validation of one received frame (checks 1 to 7 of the
 * validation sequence). Service, subtype and application data are
 * checked afterwards by the service that owns them.
 *
 * Returns PUS_OK and fills *tc, or the failure code of the first check
 * that failed. Unless the result is PUS_FAIL_NO_REPORT, tc->request_id
 * is valid and can be used in a failure report.
 */
pus_fail_t pus_tc_validate(const uint8_t *frame, size_t frame_len,
                           uint16_t expected_apid, pus_tc_t *tc);

/* Big endian helpers shared by the services. */
void     pus_put_u16(uint8_t *p, uint16_t v);
void     pus_put_u32(uint8_t *p, uint32_t v);
uint16_t pus_get_u16(const uint8_t *p);

#endif /* PUS_H */
