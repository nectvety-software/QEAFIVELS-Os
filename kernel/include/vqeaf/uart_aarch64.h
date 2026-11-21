#ifndef VQEAF_UART_AARCH64_H
#define VQEAF_UART_AARCH64_H
#include <stdint.h>
/* PL011 UART on QEMU virt (MMIO 0x09000000). Used before and after the MMU is
 * enabled while the identity map covers the device region. */
void vqeaf_uart_init(void);
void vqeaf_uart_char(char c);
void vqeaf_uart_write(const char *s);
void vqeaf_uart_u64(uint64_t v);
void vqeaf_uart_hex64(uint64_t v);
/* F3 InputFlinger path: PL011 RX interrupt (QEMU virt SPI 1 = GIC INTID 33).
 * The IRQ handler drains the FIFO and acks; these are the only MMIO accesses
 * it needs, so no device register definitions leak into irq.c. */
void vqeaf_uart_rx_irq_enable(void);
int vqeaf_uart_rx_irq_pending(void);
int vqeaf_uart_rx_available(void);
uint8_t vqeaf_uart_rx_read(void);
void vqeaf_uart_rx_irq_ack(void);
#endif