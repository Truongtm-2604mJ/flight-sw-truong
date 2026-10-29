/*
 * Packet error control: CRC-16/CCITT-FALSE.
 * Polynomial 0x1021, initial value 0xFFFF, no reflection, no final XOR.
 * Check value: crc16_ccitt("123456789", 9) == 0x29B1.
 */
#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>
#include <stddef.h>

#define CRC16_INIT 0xFFFFu

uint16_t crc16_ccitt(const uint8_t *data, size_t len);

#endif /* CRC16_H */
