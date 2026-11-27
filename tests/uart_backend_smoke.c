#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "vqeaf/uart_aarch64.h"

int main(void)
{
    assert(vqeaf_uart_backend() == VQEAF_UART_PL011);
    assert(vqeaf_uart_rx_supported() == 1);
    assert(vqeaf_uart_select_geni(0x08800000ull, 0x4000u, 633u,
                                  16u, 4u, 100u, 0u) == -1);
    assert(vqeaf_uart_backend() == VQEAF_UART_NONE);
    assert(strcmp(vqeaf_uart_backend_name(), "none") == 0);
    assert(vqeaf_uart_rx_supported() == 0);
    vqeaf_uart_rx_irq_enable();
    assert(vqeaf_uart_rx_irq_pending() == 0);
    assert(vqeaf_uart_rx_available() == 0);
    assert(vqeaf_uart_rx_read() == 0u);
    vqeaf_uart_rx_irq_ack();
    puts("AArch64 UART backend fail-closed smoke: PASS");
    return 0;
}
