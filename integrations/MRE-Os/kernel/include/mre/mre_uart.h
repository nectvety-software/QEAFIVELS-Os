#ifndef MRE_UART_H
#define MRE_UART_H
#include "mre_types.h"
typedef uint32_t (*mre_uart_read32_fn)(void *context, uint32_t offset);
typedef void (*mre_uart_write32_fn)(void *context, uint32_t offset, uint32_t value);
typedef struct mre_uart_bus {
    mre_uart_read32_fn read32;
    mre_uart_write32_fn write32;
    void *context;
} mre_uart_bus_t;
typedef struct mre_uart_geni {
    mre_uart_bus_t bus;
    uint32_t fifo_depth_words;
    uint32_t fifo_width_bytes;
    uint32_t poll_limit;
    uint8_t attached;
} mre_uart_geni_t;
mre_status_t mre_uart_geni_attach(mre_uart_geni_t *uart, const mre_uart_bus_t *bus,
                                  uint32_t fifo_depth_words, uint32_t fifo_width_bytes,
                                  uint32_t poll_limit, int clocks_ready,
                                  int pins_ready, int inherited_uart_config_verified);
mre_status_t mre_uart_geni_write(mre_uart_geni_t *uart, const char *data, size_t length);
#endif
