#include "crc16.h"

/*
 * Bitwise implementation: no table, constant memory, easy to review.
 * Cost is 8 shift/XOR steps per octet, which is small against the
 * housekeeping period (see docs/tmtc-design.md, timing section). A 256
 * entry table is the standard optimisation if the budget ever needs it.
 */
uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = CRC16_INIT;
    size_t i;
    int bit;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0; bit < 8; bit++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}
