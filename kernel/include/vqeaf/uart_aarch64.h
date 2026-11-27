#ifndef VQEAF_UART_AARCH64_H
#define VQEAF_UART_AARCH64_H
#include <stddef.h>
#include <stdint.h>

typedef enum {
    VQEAF_UART_PL011 = 0,
    VQEAF_UART_GENI = 1,
    VQEAF_UART_NONE = 2
} VqeafUartBackend;

#define VQEAF_UART_GENI_CLOCKS_READY          (1u << 0)
#define VQEAF_UART_GENI_PINS_READY            (1u << 1)
#define VQEAF_UART_GENI_CONFIG_INHERITED      (1u << 2)
#define VQEAF_UART_GENI_MMU_MAPPED            (1u << 3)
#define VQEAF_UART_GENI_REQUIRED_FLAGS \
    (VQEAF_UART_GENI_CLOCKS_READY | VQEAF_UART_GENI_PINS_READY | \
     VQEAF_UART_GENI_CONFIG_INHERITED | VQEAF_UART_GENI_MMU_MAPPED)

/* QEMU starts on PL011. A verified Violet boot path may select GENI only after
 * its clocks, pinmux, inherited UART configuration and device mapping are known.
 * A failed GENI selection disables output instead of falling back to PL011. */
int vqeaf_uart_select_geni(uint64_t base, uint64_t size, uint32_t irq,
                           uint32_t fifo_depth_words, uint32_t fifo_width_bytes,
                           uint32_t poll_limit, uint32_t verified_flags);
VqeafUartBackend vqeaf_uart_backend(void);
const char *vqeaf_uart_backend_name(void);
int vqeaf_uart_rx_supported(void);

void vqeaf_uart_init(void);
void vqeaf_uart_char(char c);
void vqeaf_uart_write(const char *s);
void vqeaf_uart_u64(uint64_t v);
void vqeaf_uart_hex64(uint64_t v);
/* RX is implemented only by the QEMU PL011 backend. */
void vqeaf_uart_rx_irq_enable(void);
int vqeaf_uart_rx_irq_pending(void);
int vqeaf_uart_rx_available(void);
uint8_t vqeaf_uart_rx_read(void);
void vqeaf_uart_rx_irq_ack(void);
#endif