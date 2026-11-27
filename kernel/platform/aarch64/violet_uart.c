#include "vqeaf/violet_boot.h"
#include "vqeaf/uart_aarch64.h"

int vqeaf_violet_uart_init(uint32_t verified_flags)
{
    const vqeaf_violet_boot_info_t *info = vqeaf_violet_current_info();
    if (info == NULL || !vqeaf_violet_is_target() ||
        !info->boot_handoff_verified || !info->uart_found ||
        !vqeaf_violet_mmu_is_enabled())
        return -1;
    return vqeaf_uart_select_geni(info->uart_base, info->uart_size,
                                  info->uart_irq, 16u, 4u, 100000u,
                                  verified_flags);
}
