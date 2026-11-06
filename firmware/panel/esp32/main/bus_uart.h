#pragma once
#include <stdint.h>
void bus_uart_init(void);
void bus_uart_send(void *ctx, const uint8_t *f, uint16_t n);
int  bus_uart_read_frame(uint8_t *buf, int cap);
