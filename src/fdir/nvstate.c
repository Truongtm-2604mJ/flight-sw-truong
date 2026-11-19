/* Lab 5.2 - FDIR non volatile record. See nvstate.h. */
#include "nvstate.h"

#include <string.h>

#include "crc16.h"

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void nv_defaults(nv_record_t *r)
{
    memset(r, 0, sizeof(*r));
    r->last_esc_task = 0xFFu;
}

/*
 *  0  magic u32          4  version u16        6  boot_count u16
 *  8  reset_count u16   10  pending u8        11  last_reset u8
 * 12  esc_count u16     14  esc_task u8       15  esc_action u8
 * 16  esc_boot u16      18  esc_uptime u32    22  safe_src u8
 * 23  spare u8          24  safe_detail u16   26  crc u16
 */
void nv_encode(const nv_record_t *r, uint8_t image[NV_IMAGE_LEN])
{
    put32(&image[0], NV_MAGIC);
    put16(&image[4], NV_VERSION);
    put16(&image[6], r->boot_count);
    put16(&image[8], r->reset_count);
    image[10] = r->pending_reset;
    image[11] = r->last_reset_cause;
    put16(&image[12], r->escalation_count);
    image[14] = r->last_esc_task;
    image[15] = r->last_esc_action;
    put16(&image[16], r->last_esc_boot);
    put32(&image[18], r->last_esc_uptime_s);
    image[22] = r->last_safe_src;
    image[23] = r->spare;
    put16(&image[24], r->last_safe_detail);
    put16(&image[26], crc16_ccitt(image, NV_IMAGE_LEN - 2u));
}

int nv_decode(const uint8_t image[NV_IMAGE_LEN], nv_record_t *r)
{
    if (get32(&image[0]) != NV_MAGIC || get16(&image[4]) != NV_VERSION ||
        crc16_ccitt(image, NV_IMAGE_LEN - 2u) != get16(&image[26])) {
        nv_defaults(r);
        return -1;
    }
    r->boot_count        = get16(&image[6]);
    r->reset_count       = get16(&image[8]);
    r->pending_reset     = image[10];
    r->last_reset_cause  = image[11];
    r->escalation_count  = get16(&image[12]);
    r->last_esc_task     = image[14];
    r->last_esc_action   = image[15];
    r->last_esc_boot     = get16(&image[16]);
    r->last_esc_uptime_s = get32(&image[18]);
    r->last_safe_src     = image[22];
    r->spare             = image[23];
    r->last_safe_detail  = get16(&image[24]);
    return 0;
}

nv_reset_cause_t nv_boot(nv_record_t *r, const uint8_t image[NV_IMAGE_LEN],
                         int *was_valid)
{
    int valid = (nv_decode(image, r) == 0);
    nv_reset_cause_t cause = NV_RESET_NONE;

    if (valid && r->pending_reset != NV_RESET_NONE) {
        cause = (nv_reset_cause_t)r->pending_reset;
        r->reset_count = (uint16_t)(r->reset_count + 1u);
    }
    r->last_reset_cause = (uint8_t)cause;
    r->pending_reset    = NV_RESET_NONE;
    r->boot_count       = (uint16_t)(r->boot_count + 1u);
    if (was_valid != NULL) {
        *was_valid = valid;
    }
    return cause;
}
