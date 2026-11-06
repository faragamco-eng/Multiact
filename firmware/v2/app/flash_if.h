/* flash_if.h - minimal flash access used by params.c and fwu.c (host: RAM emulation) */
#ifndef FLASH_IF_H
#define FLASH_IF_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t page_size;                                           /* 2048 on STM32G431 */
    bool (*erase)(uint32_t addr);                                 /* erase the page at addr */
    bool (*program)(uint32_t addr, const void *src, uint32_t len);/* len multiple of 8, erased area */
    const uint8_t *(*ptr)(uint32_t addr);                         /* memory-mapped read */
} flash_if_t;
#endif
