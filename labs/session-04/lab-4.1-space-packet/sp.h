/*
 * Lab 4.1 - CCSDS Space Packet encoder / decoder
 *
 * Portable C99, no OS dependencies, no dynamic memory, no bitfields.
 * All multi-octet fields are big endian (network order) as the Space
 * Packet Protocol specifies. Fields are packed with explicit shifts and
 * masks so the result does not depend on compiler or CPU endianness.
 *
 * Primary header (6 octets, 48 bits):
 *
 *   octet 0      octet 1      octet 2      octet 3      octet 4   octet 5
 *  +---+-+-+---+ +--------+ +--+------+ +--------+ +----------------------+
 *  |VVV|T|S|AAA| |AAAAAAAA| |FF|CCCCCC| |CCCCCCCC| |  packet data length  |
 *  +---+-+-+---+ +--------+ +--+------+ +--------+ +----------------------+
 *   V version (3)  T type (1)  S secondary header flag (1)  A APID (11)
 *   F sequence flags (2)       C sequence count (14)
 *
 * Packet Data Length = (number of octets in the packet data field) - 1.
 */
#ifndef SP_H
#define SP_H

#include <stdint.h>
#include <stddef.h>

#define SP_PRIMARY_HEADER_LEN 6u
#define SP_VERSION            0u       /* only version defined so far      */
#define SP_APID_MAX           0x7FFu   /* 11 bits                          */
#define SP_APID_IDLE          0x7FFu   /* all ones: reserved, idle packets */
#define SP_SEQ_COUNT_MAX      0x3FFFu  /* 14 bits                          */
#define SP_SEQ_FLAGS_UNSEG    3u       /* '11' unsegmented / standalone    */
#define SP_PAYLOAD_MIN        1u       /* data field holds >= 1 octet      */
#define SP_PAYLOAD_MAX        65536u   /* 16 bit length field, minus one   */

typedef enum { SP_TM = 0, SP_TC = 1 } sp_type_t;

typedef struct {
    uint8_t   version;       /* 3 bits */
    sp_type_t type;          /* 1 bit  */
    uint8_t   sec_hdr_flag;  /* 1 bit  */
    uint16_t  apid;          /* 11 bits */
    uint8_t   seq_flags;     /* 2 bits */
    uint16_t  seq_count;     /* 14 bits */
    uint16_t  data_length;   /* as encoded: payload octets minus 1 */
} sp_primary_header_t;

/* Error codes. All negative so they cannot be confused with a length. */
enum {
    SP_ERR_ARG       = -1,  /* NULL pointer argument                        */
    SP_ERR_FIELD     = -2,  /* header field does not fit its bit width      */
    SP_ERR_PAYLOAD   = -3,  /* payload_len is 0 or larger than 65536        */
    SP_ERR_CAPACITY  = -4,  /* output buffer too small                      */
    SP_ERR_TRUNCATED = -5,  /* input shorter than the primary header        */
    SP_ERR_VERSION   = -6,  /* packet version number is not 000             */
    SP_ERR_LENGTH    = -7   /* header claims more payload than buffer holds */
};

/*
 * Returns total packet size written, or negative on error.
 *
 * The length field is derived from payload_len. hdr->data_length is NOT
 * read by this function: there is one source of truth for the length, so
 * a caller cannot produce a header that disagrees with its payload.
 * Out of range fields are rejected, never truncated.
 */
int sp_encode(const sp_primary_header_t *hdr,
              const uint8_t *payload, size_t payload_len,
              uint8_t *out, size_t out_cap);

/*
 * Returns payload length, or negative on error.
 *
 * On success *payload_out points into `in` (no copy) and hdr is filled,
 * including data_length exactly as it was encoded. The total packet size
 * is SP_PRIMARY_HEADER_LEN + return value. in_len may be larger than
 * that (a stream containing several packets); a caller that expects
 * exactly one packet per frame must compare the sizes itself.
 */
int sp_decode(const uint8_t *in, size_t in_len,
              sp_primary_header_t *hdr,
              const uint8_t **payload_out);

#endif /* SP_H */
