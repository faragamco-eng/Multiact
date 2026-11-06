/*
 * bus_uart.c - Panel side of the 2-wire bus on an ESP32 UART (2400 8N1).
 *
 * Panel PHY hardware (next design phase, see docs/08):
 *   RX: comparator on the bus voltage (threshold 20 V between 24 V mark / 16 V space)
 *   TX: GPIO switches a 20 mA current sink for a space (0)
 * The UART RX timeout (in character times) marks the end of a Modbus frame.
 */
#include "driver/uart.h"
#include "esp_log.h"
#include "bus_uart.h"

#define BUS_UART   UART_NUM_1
#define BUS_TX_PIN 17
#define BUS_RX_PIN 18

void bus_uart_init(void)
{
    const uart_config_t cfg = { .baud_rate = 2400, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
                                .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                                .source_clk = UART_SCLK_DEFAULT };
    ESP_ERROR_CHECK(uart_driver_install(BUS_UART, 512, 512, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(BUS_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(BUS_UART, BUS_TX_PIN, BUS_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    /* current sink is ON for a space: TX idle high must switch it OFF -> invert TX if the sink is active high */
    ESP_ERROR_CHECK(uart_set_line_inverse(BUS_UART, UART_SIGNAL_TXD_INV));
    ESP_ERROR_CHECK(uart_set_rx_timeout(BUS_UART, 4));      /* ~3.5 characters */
}

void bus_uart_send(void *ctx, const uint8_t *f, uint16_t n)
{
    (void)ctx;
    uart_flush_input(BUS_UART);
    uart_write_bytes(BUS_UART, f, n);
    uart_wait_tx_done(BUS_UART, pdMS_TO_TICKS(1000));
    /* half duplex: our own transmission is not echoed by the PHY, nothing to discard */
}

/* returns a complete frame (bytes read up to the RX timeout) or 0 */
int bus_uart_read_frame(uint8_t *buf, int cap)
{
    size_t avail = 0;
    uart_get_buffered_data_len(BUS_UART, &avail);
    if (!avail) return 0;
    return uart_read_bytes(BUS_UART, buf, cap, pdMS_TO_TICKS(20));   /* returns after the gap */
}
