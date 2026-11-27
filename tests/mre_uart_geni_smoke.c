#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mre_uart.h"

#define GENI_STATUS 0x040u
#define GENI_SER_M_CLK_CFG 0x048u
#define GENI_IF_FIFO_DISABLE_RO 0x064u
#define SE_GENI_M_CMD0 0x600u
#define SE_GENI_M_CMD_CTRL_REG 0x604u
#define SE_GENI_M_IRQ_STATUS 0x610u
#define SE_GENI_M_IRQ_EN 0x614u
#define SE_GENI_M_IRQ_CLEAR 0x618u
#define SE_GENI_TX_FIFOn 0x700u
#define SE_GENI_TX_FIFO_STATUS 0x800u
#define SE_GENI_TX_WATERMARK 0x80cu
#define SE_UART_TX_TRANS_LEN 0x270u
#define M_GENI_CMD_ACTIVE 0x1u
#define SER_CLK_EN 0x1u
#define FIFO_IF_DISABLE 0x1u
#define M_CMD_DONE_EN 0x1u
#define M_TX_FIFO_WATERMARK_EN 0x40000000u
#define M_CANCEL_IRQ 0x10u
#define M_ABORT_IRQ 0x20u
#define M_CANCEL_CMD 0x4u
#define M_ABORT_CMD 0x2u
#define UART_START_TX 0x08000000u

typedef enum {
    GENI_NORMAL,
    GENI_CANCEL,
    GENI_ABORT,
    GENI_HARD_TIMEOUT
} geni_scenario_t;

typedef struct {
    uint32_t regs[0x1000];
    geni_scenario_t scenario;
    uint32_t tx_count;
    uint32_t tx_expected;
    uint32_t cancel_writes;
    uint32_t abort_writes;
    int command_started;
    int cancel_irq;
    int abort_irq;
    int watermark;
} geni_fake_t;

static geni_fake_t fake;
static uint32_t fake_read32(void *context, uint32_t offset);
static void fake_write32(void *context, uint32_t offset, uint32_t value);

static uint32_t fake_read32(void *context, uint32_t offset)
{
    geni_fake_t *f = (geni_fake_t *)context;
    assert(offset < sizeof(f->regs) / sizeof(f->regs[0]));
    if (offset == GENI_STATUS)
        return f->command_started ? M_GENI_CMD_ACTIVE : 0u;
    if (offset == SE_GENI_M_IRQ_STATUS) {
        uint32_t status = 0u;
        if (f->watermark) status |= M_TX_FIFO_WATERMARK_EN;
        if (f->cancel_irq) status |= M_CANCEL_IRQ;
        if (f->abort_irq) status |= M_ABORT_IRQ;
        if (f->scenario == GENI_NORMAL && f->command_started &&
            f->tx_count >= f->tx_expected)
            status |= M_CMD_DONE_EN;
        return status;
    }
    if (offset == SE_GENI_TX_FIFO_STATUS) return 0u;
    return f->regs[offset];
}

static void fake_write32(void *context, uint32_t offset, uint32_t value)
{
    geni_fake_t *f = (geni_fake_t *)context;
    assert(offset < sizeof(f->regs) / sizeof(f->regs[0]));
    f->regs[offset] = value;
    if (offset == SE_GENI_TX_FIFOn) ++f->tx_count;
    if (offset == SE_UART_TX_TRANS_LEN) f->tx_expected = value;
    if (offset == SE_GENI_M_CMD0 && (value & UART_START_TX) != 0u) {
        f->command_started = 1;
        if (f->scenario == GENI_NORMAL) f->watermark = 1;
    }
    if (offset == SE_GENI_M_CMD_CTRL_REG) {
        if ((value & M_CANCEL_CMD) != 0u) {
            ++f->cancel_writes;
            if (f->scenario == GENI_CANCEL) f->cancel_irq = 1;
        }
        if ((value & M_ABORT_CMD) != 0u) {
            ++f->abort_writes;
            if (f->scenario == GENI_ABORT) f->abort_irq = 1;
        }
    }
    if (offset == SE_GENI_M_IRQ_CLEAR) {
        if ((value & M_TX_FIFO_WATERMARK_EN) != 0u) f->watermark = 0;
        if ((value & M_CANCEL_IRQ) != 0u) f->cancel_irq = 0;
        if ((value & M_ABORT_IRQ) != 0u) f->abort_irq = 0;
    }
}

static const mre_uart_bus_t bus = { fake_read32, fake_write32, &fake };

static void reset_fake(geni_scenario_t scenario)
{
    memset(&fake, 0, sizeof(fake));
    fake.scenario = scenario;
    fake.regs[GENI_SER_M_CLK_CFG] = SER_CLK_EN;
}

static void attach_uart(mre_uart_geni_t *uart)
{
    assert(mre_uart_geni_attach(uart, &bus, 16u, 4u, 2u, 1, 1, 1) == MRE_OK);
    assert(uart->attached == 1u);
}

static void test_normal_write(void)
{
    mre_uart_geni_t uart;
    reset_fake(GENI_NORMAL);
    attach_uart(&uart);
    assert(mre_uart_geni_write(&uart, "A\nB", 3u) == MRE_OK);
    assert(uart.attached == 1u);
    assert(fake.tx_count == 4u);
    assert(fake.cancel_writes == 0u && fake.abort_writes == 0u);
}

static void test_cancel_abort_timeout(void)
{
    mre_uart_geni_t uart;

    reset_fake(GENI_CANCEL);
    attach_uart(&uart);
    assert(mre_uart_geni_write(&uart, "X", 1u) == MRE_E_BUSY);
    assert(uart.attached == 0u);
    assert(fake.cancel_writes == 1u && fake.abort_writes == 0u);
    assert((fake.regs[SE_GENI_M_IRQ_EN] &
            (M_TX_FIFO_WATERMARK_EN | M_CMD_DONE_EN)) == 0u);
    assert(fake.regs[SE_GENI_TX_WATERMARK] == 0u);

    reset_fake(GENI_ABORT);
    attach_uart(&uart);
    assert(mre_uart_geni_write(&uart, "X", 1u) == MRE_E_BUSY);
    assert(uart.attached == 0u);
    assert(fake.cancel_writes == 1u && fake.abort_writes == 1u);

    reset_fake(GENI_HARD_TIMEOUT);
    attach_uart(&uart);
    assert(mre_uart_geni_write(&uart, "X", 1u) == MRE_E_BUSY);
    assert(uart.attached == 0u);
    assert(fake.cancel_writes == 1u && fake.abort_writes == 1u);
}

static void test_attach_gate(void)
{
    mre_uart_geni_t uart;
    reset_fake(GENI_NORMAL);
    assert(mre_uart_geni_attach(&uart, &bus, 16u, 4u, 2u, 0, 1, 1) ==
           MRE_E_PERMISSION);
    assert(uart.attached == 0u);
}

int main(void)
{
    test_normal_write();
    test_cancel_abort_timeout();
    test_attach_gate();
    puts("MRE GENI UART cancel/abort host tests: PASS");
    return 0;
}
