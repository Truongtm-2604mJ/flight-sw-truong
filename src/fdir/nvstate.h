/*
 * Lab 5.2 - FDIR record kept in non volatile memory.
 *
 * Holds what the ground needs to reconstruct what happened across a
 * reset: boot and reset counts, why the last reset happened, and the
 * last escalation. Stored as a fixed 28 octet big endian image with a
 * magic number, a version and a CRC-16, so a corrupted or never written
 * record is detected and replaced by defaults (cold start) instead of
 * being trusted.
 *
 * The storage itself (EEPROM, MRAM, a no-init RAM area, or a file on
 * the host) is not this module's business: callers move the image.
 *
 * Portable C99.
 */
#ifndef NVSTATE_H
#define NVSTATE_H

#include <stdint.h>

#define NV_MAGIC        0x46444952u  /* "FDIR" */
#define NV_VERSION      1u
#define NV_IMAGE_LEN    28u

typedef enum {
    NV_RESET_NONE     = 0,  /* power on / cold start  */
    NV_RESET_WATCHDOG = 1
} nv_reset_cause_t;

typedef struct {
    uint16_t boot_count;          /* every start, including resets      */
    uint16_t reset_count;         /* unplanned resets                   */
    uint8_t  pending_reset;       /* written just before a reset        */
    uint8_t  last_reset_cause;    /* set at boot from pending_reset     */
    uint16_t escalation_count;
    uint8_t  last_esc_task;       /* 0xFF = none                        */
    uint8_t  last_esc_action;     /* mode requested by the escalation   */
    uint16_t last_esc_boot;       /* boot_count when it happened        */
    uint32_t last_esc_uptime_s;
    uint8_t  last_safe_src;       /* cause of the last SAFE entry       */
    uint8_t  spare;
    uint16_t last_safe_detail;
} nv_record_t;

/* Defaults for a cold start (everything zero, no escalation). */
void nv_defaults(nv_record_t *r);

/* Serialise, with magic, version and CRC. */
void nv_encode(const nv_record_t *r, uint8_t image[NV_IMAGE_LEN]);

/* 0 and fills *r if the image is valid; -1 (and *r = defaults) if the
 * magic, version or CRC does not match. */
int nv_decode(const uint8_t image[NV_IMAGE_LEN], nv_record_t *r);

/* The boot sequence: decode, move pending_reset into last_reset_cause,
 * count the boot (and the reset, if there was one), and clear the
 * pending flag. Returns the reset cause of this boot. */
nv_reset_cause_t nv_boot(nv_record_t *r, const uint8_t image[NV_IMAGE_LEN],
                         int *was_valid);

#endif /* NVSTATE_H */
