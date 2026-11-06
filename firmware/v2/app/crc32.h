#ifndef CRC32_H
#define CRC32_H
#include <stdint.h>
/* CRC-32/IEEE (zlib), incremental: crc = crc32_update(0, p, n) ... */
uint32_t crc32_update(uint32_t crc, const void *p, uint32_t n);
#endif
