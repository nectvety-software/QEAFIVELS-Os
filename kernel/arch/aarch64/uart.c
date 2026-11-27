#include <stdint.h>
#include <string.h>
#include "vqeaf/uart_aarch64.h"
#include "mre_uart.h"

#define UART0_BASE 0x09000000ull
#define UART_DR (*(volatile uint32_t *)(UART0_BASE + 0x000u))
#define UART_FR (*(volatile uint32_t *)(UART0_BASE + 0x018u))
#define UART_CR (*(volatile uint32_t *)(UART0_BASE + 0x030u))
#define UART_IMSC (*(volatile uint32_t *)(UART0_BASE + 0x038u))
#define UART_MIS (*(volatile uint32_t *)(UART0_BASE + 0x040u))
#define UART_ICR (*(volatile uint32_t *)(UART0_BASE + 0x044u))
#define UART_FR_TXFF (1u << 5)
#define UART_FR_RXFE (1u << 4)
#define UART_CR_EN (1u | (1u << 8) | (1u << 9))
#define UART_IMSC_RXIM (1u << 4)
#define UART_ICR_ALL 0x7FFu

static VqeafUartBackend g_backend = VQEAF_UART_PL011;
static mre_uart_geni_t g_geni;
static uint64_t g_geni_base;

static uint32_t geni_read32(void *context, uint32_t offset)
{
    (void)context;
    return *(volatile uint32_t *)(g_geni_base + offset);
}

static void geni_write32(void *context, uint32_t offset, uint32_t value)
{
    (void)context;
    *(volatile uint32_t *)(g_geni_base + offset) = value;
}

int vqeaf_uart_select_geni(uint64_t base, uint64_t size, uint32_t irq,
                           uint32_t fifo_depth_words, uint32_t fifo_width_bytes,
                           uint32_t poll_limit, uint32_t verified_flags)
{
    mre_uart_bus_t bus;
    mre_status_t st;

    g_backend = VQEAF_UART_NONE;
    memset(&g_geni, 0, sizeof(g_geni));
    if (base == 0u || size < 0x1000u || base > UINT64_MAX - size ||
        irq == 0u || (verified_flags & VQEAF_UART_GENI_REQUIRED_FLAGS) !=
                     VQEAF_UART_GENI_REQUIRED_FLAGS ||
        (verified_flags & ~VQEAF_UART_GENI_REQUIRED_FLAGS) != 0u)
        return -1;

    g_geni_base = base;
    bus.read32 = geni_read32;
    bus.write32 = geni_write32;
    bus.context = NULL;
    st = mre_uart_geni_attach(&g_geni, &bus, fifo_depth_words,
                              fifo_width_bytes, poll_limit,
                              (verified_flags & VQEAF_UART_GENI_CLOCKS_READY) != 0u,
                              (verified_flags & VQEAF_UART_GENI_PINS_READY) != 0u,
                              (verified_flags & VQEAF_UART_GENI_CONFIG_INHERITED) != 0u);
    if (st != MRE_OK) return (int)st;
    g_backend = VQEAF_UART_GENI;
    return 0;
}

VqeafUartBackend vqeaf_uart_backend(void)
{
    return g_backend;
}

const char *vqeaf_uart_backend_name(void)
{
    switch (g_backend) {
    case VQEAF_UART_PL011: return "pl011";
    case VQEAF_UART_GENI: return "geni";
    default: return "none";
    }
}

int vqeaf_uart_rx_supported(void)
{
    return g_backend == VQEAF_UART_PL011 ? 1 : 0;
}

void vqeaf_uart_init(void)
{
    if (g_backend == VQEAF_UART_PL011) UART_CR = UART_CR_EN;
}

void vqeaf_uart_char(char c)
{
    if (g_backend == VQEAF_UART_PL011) {
        for (int i = 0; i < 1000000 && (UART_FR & UART_FR_TXFF); i++) {}
        UART_DR = (uint32_t)(unsigned char)c;
    } else if (g_backend == VQEAF_UART_GENI) {
        if (mre_uart_geni_write(&g_geni, &c, 1u) != MRE_OK)
            g_backend = VQEAF_UART_NONE;
    }
}

void vqeaf_uart_write(const char *s)
{
    if (s == NULL) return;
    if (g_backend == VQEAF_UART_GENI) {
        size_t length = strlen(s);
        if (length != 0u && mre_uart_geni_write(&g_geni, s, length) != MRE_OK)
            g_backend = VQEAF_UART_NONE;
        return;
    }
    while (*s) vqeaf_uart_char(*s++);
}

void vqeaf_uart_u64(uint64_t v)
{
    char buf[24];
    int i = 0;
    if (v == 0) { vqeaf_uart_char('0'); return; }
    while (v) { buf[i++] = (char)('0' + v % 10u); v /= 10u; }
    while (i) vqeaf_uart_char(buf[--i]);
}

void vqeaf_uart_hex64(uint64_t v)
{
    static const char hex[] = "0123456789abcdef";
    vqeaf_uart_write("0x");
    for (int i = 60; i >= 0; i -= 4)
        vqeaf_uart_char(hex[(v >> i) & 0xFu]);
}

void vqeaf_uart_rx_irq_enable(void)
{
    if (g_backend != VQEAF_UART_PL011) return;
    UART_ICR = UART_ICR_ALL;
    UART_IMSC = UART_IMSC_RXIM;
}

int vqeaf_uart_rx_irq_pending(void)
{
    return g_backend == VQEAF_UART_PL011 && (UART_MIS & UART_IMSC_RXIM) != 0;
}

int vqeaf_uart_rx_available(void)
{
    return g_backend == VQEAF_UART_PL011 && (UART_FR & UART_FR_RXFE) == 0;
}

uint8_t vqeaf_uart_rx_read(void)
{
    return g_backend == VQEAF_UART_PL011 ? (uint8_t)(UART_DR & 0xFFu) : 0u;
}

void vqeaf_uart_rx_irq_ack(void)
{
    if (g_backend == VQEAF_UART_PL011) UART_ICR = UART_ICR_ALL;
}
