/* PL011 UART driver for the QEMU virt AArch64 kernel port.
 * MMIO base 0x09000000; with the MMU off (early boot) physical == virtual. */
#include <stdint.h>
#include "vqeaf/uart_aarch64.h"

#define UART0_BASE 0x09000000ull
#define UART_DR (*(volatile uint32_t *)(UART0_BASE + 0x000u))
#define UART_FR (*(volatile uint32_t *)(UART0_BASE + 0x018u))
#define UART_CR (*(volatile uint32_t *)(UART0_BASE + 0x030u))
#define UART_IMSC (*(volatile uint32_t *)(UART0_BASE + 0x038u))
#define UART_MIS (*(volatile uint32_t *)(UART0_BASE + 0x040u))
#define UART_ICR (*(volatile uint32_t *)(UART0_BASE + 0x044u))
#define UART_FR_TXFF (1u << 5)
#define UART_FR_RXFE (1u << 4)
#define UART_CR_EN (1u | (1u << 8) | (1u << 9)) /* UARTEN | TXE | RXE */
#define UART_IMSC_RXIM (1u << 4)
#define UART_ICR_ALL 0x7FFu

void vqeaf_uart_init(void)
{
    UART_CR = UART_CR_EN;
}

void vqeaf_uart_char(char c)
{
    /* Bounded spin so a real FIFO cannot overflow; QEMU drains synchronously. */
    for(int i = 0; i < 1000000 && (UART_FR & UART_FR_TXFF); i++) {}
    UART_DR = (uint32_t)(unsigned char)c;
}

void vqeaf_uart_write(const char *s)
{
    while(*s) vqeaf_uart_char(*s++);
}

void vqeaf_uart_u64(uint64_t v)
{
    char buf[24];
    int i = 0;
    if(v == 0) { vqeaf_uart_char('0'); return; }
    while(v) { buf[i++] = (char)('0' + v % 10u); v /= 10u; }
    while(i) vqeaf_uart_char(buf[--i]);
}

void vqeaf_uart_hex64(uint64_t v)
{
    static const char hex[] = "0123456789abcdef";
    vqeaf_uart_write("0x");
    for(int i = 60; i >= 0; i -= 4)
        vqeaf_uart_char(hex[(v >> i) & 0xFu]);
}

/* ---- RX interrupt (input path) ------------------------------------------ */

void vqeaf_uart_rx_irq_enable(void)
{
    UART_ICR = UART_ICR_ALL;      /* clear stale status before unmasking */
    UART_IMSC = UART_IMSC_RXIM;   /* RX FIFO interrupt only */
}

int vqeaf_uart_rx_irq_pending(void)
{
    return (UART_MIS & UART_IMSC_RXIM) != 0;
}

int vqeaf_uart_rx_available(void)
{
    return (UART_FR & UART_FR_RXFE) == 0;
}

uint8_t vqeaf_uart_rx_read(void)
{
    return (uint8_t)(UART_DR & 0xFFu);
}

void vqeaf_uart_rx_irq_ack(void)
{
    UART_ICR = UART_ICR_ALL;
}