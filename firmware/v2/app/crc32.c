#include "crc32.h"
uint32_t crc32_update(uint32_t crc, const void *p, uint32_t n)
{
    const uint8_t *b = p;
    crc = ~crc;
    while (n--) {
        crc ^= *b++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
