/* Lab 4.1 - CCSDS Space Packet encoder / decoder. See sp.h. */
#include "sp.h"

#include <string.h>

int sp_encode(const sp_primary_header_t *hdr,
              const uint8_t *payload, size_t payload_len,
              uint8_t *out, size_t out_cap)
{
    uint16_t word;
    uint16_t length_field;

    if (hdr == NULL || payload == NULL || out == NULL) {
        return SP_ERR_ARG;
    }

    /* Reject, do not mask: a silently truncated APID sends the packet
     * to the wrong application process. */
    if (hdr->version != SP_VERSION ||
        (hdr->type != SP_TM && hdr->type != SP_TC) ||
        hdr->sec_hdr_flag > 1u ||
        hdr->apid > SP_APID_MAX ||
        hdr->seq_flags > 3u ||
        hdr->seq_count > SP_SEQ_COUNT_MAX) {
        return SP_ERR_FIELD;
    }

    if (payload_len < SP_PAYLOAD_MIN || payload_len > SP_PAYLOAD_MAX) {
        return SP_ERR_PAYLOAD;
    }

    if (out_cap < SP_PRIMARY_HEADER_LEN + payload_len) {
        return SP_ERR_CAPACITY;
    }

    /* Octets 0-1: version | type | secondary header flag | APID */
    word = (uint16_t)(((uint16_t)hdr->version << 13) |
                      ((uint16_t)hdr->type << 12) |
                      ((uint16_t)hdr->sec_hdr_flag << 11) |
                      hdr->apid);
    out[0] = (uint8_t)(word >> 8);
    out[1] = (uint8_t)(word & 0xFFu);

    /* Octets 2-3: sequence flags | sequence count */
    word = (uint16_t)(((uint16_t)hdr->seq_flags << 14) | hdr->seq_count);
    out[2] = (uint8_t)(word >> 8);
    out[3] = (uint8_t)(word & 0xFFu);

    /* Octets 4-5: packet data length = octets in data field MINUS ONE */
    length_field = (uint16_t)(payload_len - 1u);
    out[4] = (uint8_t)(length_field >> 8);
    out[5] = (uint8_t)(length_field & 0xFFu);

    memcpy(&out[SP_PRIMARY_HEADER_LEN], payload, payload_len);

    return (int)(SP_PRIMARY_HEADER_LEN + payload_len);
}

int sp_decode(const uint8_t *in, size_t in_len,
              sp_primary_header_t *hdr,
              const uint8_t **payload_out)
{
    uint16_t word;
    size_t payload_len;

    if (in == NULL || hdr == NULL || payload_out == NULL) {
        return SP_ERR_ARG;
    }

    if (in_len < SP_PRIMARY_HEADER_LEN) {
        return SP_ERR_TRUNCATED;
    }

    word = (uint16_t)(((uint16_t)in[0] << 8) | in[1]);
    hdr->version      = (uint8_t)((word >> 13) & 0x07u);
    hdr->type         = ((word >> 12) & 0x01u) ? SP_TC : SP_TM;
    hdr->sec_hdr_flag = (uint8_t)((word >> 11) & 0x01u);
    hdr->apid         = (uint16_t)(word & 0x07FFu);

    word = (uint16_t)(((uint16_t)in[2] << 8) | in[3]);
    hdr->seq_flags = (uint8_t)((word >> 14) & 0x03u);
    hdr->seq_count = (uint16_t)(word & 0x3FFFu);

    hdr->data_length = (uint16_t)(((uint16_t)in[4] << 8) | in[5]);

    if (hdr->version != SP_VERSION) {
        return SP_ERR_VERSION;
    }

    /* The check that matters on a real uplink: never trust the length
     * field further than the octets that actually arrived. */
    payload_len = (size_t)hdr->data_length + 1u;
    if (in_len - SP_PRIMARY_HEADER_LEN < payload_len) {
        return SP_ERR_LENGTH;
    }

    *payload_out = &in[SP_PRIMARY_HEADER_LEN];
    return (int)payload_len;
}
