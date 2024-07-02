#pragma once

#include <inttypes.h>

void serial_begin(uint32_t speed);
void serial_end();
uint8_t serial_available();
int16_t serial_read();
int16_t serial_peek();
uint8_t serial_write(uint8_t c);
uint8_t serial_writes(const uint8_t *buf, uint8_t size);
uint8_t serial_print(const char *s);
uint8_t serial_println();
void serial_flush();
void serial_discard();
