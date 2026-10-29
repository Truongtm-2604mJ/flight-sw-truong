/* Lab 4.2 - PUS packet layer. See pus.h. */
#include "pus.h"

#include <string.h>

#include "crc16.h"

void pus_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

void pus_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

uint16_t pus_get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

obt_t obt_from_ns(uint64_t ns_since_epoch)
{
    obt_t t;
    uint64_t frac_ns = ns_since_epoch % 1000000000u;

    t.coarse = (uint32_t)(ns_since_epoch / 1000000000u);
    t.fine   = (uint16_t)((frac_ns * 65536u) / 1000000000u);
    return t;
}

/* Wrap a finished packet data field (without CRC) into a Space Packet
 * and append the CRC over the whole packet. */
static int finish_packet(const sp_primary_header_t *hdr,
                         uint8_t *field, size_t field_len_without_crc,
                         uint8_t *out, size_t out_cap)
{
    int total;
    uint16_t crc;

    /* The CRC octets are part of the packet data field, so they must be
     * counted in the length field. Encode with placeholder zeros, then
     * overwrite them once the header octets exist. */
    field[field_len_without_crc]      = 0;
    field[field_len_without_crc + 1u] = 0;

    total = sp_encode(hdr, field, field_len_without_crc + PUS_CRC_LEN,
                      out, out_cap);
    if (total < 0) {
        return total;
    }

    crc = crc16_ccitt(out, (size_t)total - PUS_CRC_LEN);
    pus_put_u16(&out[(size_t)total - PUS_CRC_LEN], crc);
    return total;
}

int pus_tm_build(tm_source_t *src, uint8_t service, uint8_t subtype,
                 uint16_t dest_id, obt_t time,
                 const uint8_t *data, size_t data_len,
                 uint8_t *out, size_t out_cap)
{
    uint8_t field[PUS_TM_SEC_HDR_LEN + PUS_TM_MAX_DATA + PUS_CRC_LEN];
    sp_primary_header_t hdr;
    int total;

    if (src == NULL || out == NULL || (data == NULL && data_len > 0u)) {
        return SP_ERR_ARG;
    }
    if (data_len > PUS_TM_MAX_DATA) {
        return SP_ERR_PAYLOAD;
    }

    field[0] = (uint8_t)(PUS_VERSION << 4); /* time reference status = 0 */
    field[1] = service;
    field[2] = subtype;
    pus_put_u16(&field[3], 0);              /* message type counter: unused */
    pus_put_u16(&field[5], dest_id);
    pus_put_u32(&field[7], time.coarse);
    pus_put_u16(&field[11], time.fine);
    if (data_len > 0u) {
        memcpy(&field[PUS_TM_SEC_HDR_LEN], data, data_len);
    }

    hdr.version      = SP_VERSION;
    hdr.type         = SP_TM;
    hdr.sec_hdr_flag = 1;
    hdr.apid         = src->apid;
    hdr.seq_flags    = SP_SEQ_FLAGS_UNSEG;
    hdr.seq_count    = src->seq_count;
    hdr.data_length  = 0; /* derived by sp_encode */

    total = finish_packet(&hdr, field, PUS_TM_SEC_HDR_LEN + data_len,
                          out, out_cap);
    if (total > 0) {
        /* Count the packet as soon as it exists. If it is later dropped
         * (queue full), the ground sees a gap, which is the point. */
        src->seq_count = (uint16_t)((src->seq_count + 1u) & SP_SEQ_COUNT_MAX);
    }
    return total;
}

int pus_tc_build(uint16_t apid, uint16_t seq_count, uint8_t ack_flags,
                 uint8_t service, uint8_t subtype, uint16_t source_id,
                 const uint8_t *app_data, size_t app_len,
                 uint8_t *out, size_t out_cap)
{
    uint8_t field[PUS_TC_SEC_HDR_LEN + PUS_TC_MAX_DATA + PUS_CRC_LEN];
    sp_primary_header_t hdr;

    if (out == NULL || (app_data == NULL && app_len > 0u)) {
        return SP_ERR_ARG;
    }
    if (app_len > PUS_TC_MAX_DATA) {
        return SP_ERR_PAYLOAD;
    }
    if (ack_flags > 0x0Fu) {
        return SP_ERR_FIELD;
    }

    field[0] = (uint8_t)((PUS_VERSION << 4) | ack_flags);
    field[1] = service;
    field[2] = subtype;
    pus_put_u16(&field[3], source_id);
    if (app_len > 0u) {
        memcpy(&field[PUS_TC_SEC_HDR_LEN], app_data, app_len);
    }

    hdr.version      = SP_VERSION;
    hdr.type         = SP_TC;
    hdr.sec_hdr_flag = 1;
    hdr.apid         = apid;
    hdr.seq_flags    = SP_SEQ_FLAGS_UNSEG;
    hdr.seq_count    = seq_count;
    hdr.data_length  = 0;

    return finish_packet(&hdr, field, PUS_TC_SEC_HDR_LEN + app_len,
                         out, out_cap);
}

pus_fail_t pus_tc_validate(const uint8_t *frame, size_t frame_len,
                           uint16_t expected_apid, pus_tc_t *tc)
{
    const uint8_t *field = NULL;
    int field_len;
    uint16_t crc_calc;
    uint16_t crc_recv;

    if (frame == NULL || tc == NULL) {
        return PUS_FAIL_NO_REPORT;
    }
    memset(tc, 0, sizeof(*tc));

    /* Check 1: enough octets for a primary header. Below that there is
     * no request ID to report against, so the frame is only counted. */
    if (frame_len < SP_PRIMARY_HEADER_LEN) {
        return PUS_FAIL_NO_REPORT;
    }
    memcpy(tc->request_id, frame, PUS_REQUEST_ID_LEN);

    field_len = sp_decode(frame, frame_len, &tc->sp, &field);

    /* Check 2: packet version number. */
    if (field_len == SP_ERR_VERSION) {
        return PUS_FAIL_SP_VERSION;
    }

    /* Check 3: length. The header must describe exactly the frame that
     * arrived (no missing octets, no trailing octets), the frame must be
     * long enough to hold a PUS header and CRC, and not longer than the
     * largest packet this software handles. This runs before the CRC
     * because the CRC position is derived from the length. */
    if (field_len < 0 ||
        (size_t)field_len + SP_PRIMARY_HEADER_LEN != frame_len ||
        frame_len < PUS_TC_MIN_LEN ||
        frame_len > PUS_MAX_PACKET) {
        return PUS_FAIL_LENGTH;
    }

    /* Check 4: checksum over everything except the CRC itself. Nothing
     * inside the packet is interpreted before this passes. */
    crc_calc = crc16_ccitt(frame, frame_len - PUS_CRC_LEN);
    crc_recv = pus_get_u16(&frame[frame_len - PUS_CRC_LEN]);
    if (crc_calc != crc_recv) {
        return PUS_FAIL_CHECKSUM;
    }

    /* Check 5: it is an unsegmented telecommand with a secondary header. */
    if (tc->sp.type != SP_TC || tc->sp.sec_hdr_flag != 1u ||
        tc->sp.seq_flags != SP_SEQ_FLAGS_UNSEG) {
        return PUS_FAIL_PACKET_HEADER;
    }

    /* Check 6: addressed to this application process. */
    if (tc->sp.apid != expected_apid) {
        return PUS_FAIL_APID;
    }

    /* Check 7: PUS version. */
    if ((field[0] >> 4) != PUS_VERSION) {
        return PUS_FAIL_PUS_VERSION;
    }

    tc->ack_flags = (uint8_t)(field[0] & 0x0Fu);
    tc->service   = field[1];
    tc->subtype   = field[2];
    tc->source_id = pus_get_u16(&field[3]);
    tc->app_data  = &field[PUS_TC_SEC_HDR_LEN];
    tc->app_len   = (size_t)field_len - PUS_TC_SEC_HDR_LEN - PUS_CRC_LEN;

    return PUS_OK;
}
