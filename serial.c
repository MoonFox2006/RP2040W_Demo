#include <string.h>
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "serial.h"

#define UART_ID     uart0
#define UART_IRQ    UART0_IRQ

#define UART_TX_PIN 0
#define UART_RX_PIN 1

#define RX_BUF_SIZE 64

static const char CRLF[] = "\r\n";

static volatile uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint8_t rx_len, rx_pos;

static void uart_iqr() {
    while (uart_is_readable(UART_ID)) {
        char c = uart_getc(UART_ID);

        if (rx_len < RX_BUF_SIZE) {
            rx_buf[rx_pos++] = c;
            if (rx_pos >= RX_BUF_SIZE)
                rx_pos = 0;
            ++rx_len;
        }
    }
}

void serial_begin(uint32_t speed) {
    rx_len = rx_pos = 0;

    uart_init(UART_ID, speed);

    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

//    uart_set_translate_crlf(UART_ID, false);
//    uart_set_fifo_enabled(UART_ID, false);
    irq_set_exclusive_handler(UART_IRQ, uart_iqr);
    irq_set_enabled(UART_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);
}

void serial_end() {
    uart_set_irq_enables(UART_ID, false, false);
    irq_set_enabled(UART_IRQ, false);
    irq_remove_handler(UART_IRQ, uart_iqr);
    uart_deinit(UART_ID);

    gpio_set_function(UART_TX_PIN, GPIO_FUNC_NULL);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_NULL);
}

uint8_t serial_available() {
    uint8_t result;

    irq_set_enabled(UART_IRQ, false);
    result = rx_len;
    irq_set_enabled(UART_IRQ, true);
    return result;
}

int16_t serial_read() {
    int16_t result = -1;

    irq_set_enabled(UART_IRQ, false);
    if (rx_len) {
        result = rx_buf[(RX_BUF_SIZE + rx_pos - rx_len--) % RX_BUF_SIZE];
    }
    irq_set_enabled(UART_IRQ, true);
    return result;
}

int16_t serial_peek() {
    int16_t result = -1;

    irq_set_enabled(UART_IRQ, false);
    if (rx_len) {
        result = rx_buf[(RX_BUF_SIZE + rx_pos - rx_len) % RX_BUF_SIZE];
    }
    irq_set_enabled(UART_IRQ, true);
    return result;
}

uint8_t serial_write(uint8_t c) {
    uart_putc_raw(UART_ID, (char)c);
    return sizeof(c);
}

uint8_t serial_writes(const uint8_t *buf, uint8_t size) {
    uart_write_blocking(UART_ID, buf, size);
    return size;
}

uint8_t serial_print(const char *s) {
    return serial_writes((const uint8_t*)s, strlen(s));
}

uint8_t serial_println() {
    return serial_writes((const uint8_t*)CRLF, sizeof(CRLF) - 1);
}

void serial_flush() {
    uart_tx_wait_blocking(UART_ID);
}

void serial_discard() {
    irq_set_enabled(UART_IRQ, false);
    rx_len = rx_pos = 0;
    irq_set_enabled(UART_IRQ, true);
}
