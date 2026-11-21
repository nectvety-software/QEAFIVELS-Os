/* Host test for the QEAPP native module validator.
 *
 * Proves that structurally valid modules are accepted and that bad magic,
 * ABI, architecture, sizes, and hashes are rejected. No code is executed.
 */
#include "vqeaf_qeapp_native.h"

#include <stdio.h>
#include <string.h>

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint32_t fnv1a32(const uint8_t *data, size_t size)
{
    uint32_t value = 0x811C9DC5u;
    for(size_t i = 0; i < size; i++) {
        value ^= data[i];
        value *= 0x01000193u;
    }
    return value;
}

static void make_stub(uint8_t *out, uint8_t arch, size_t payload)
{
    memset(out, 0, VQEAF_QEN_HEADER_SIZE + payload);
    memcpy(out, "QEAF-QEN1", 9);
    out[9] = 1;
    out[10] = 2;
    out[11] = arch;
    put16(out + 13, VQEAF_QEN_HEADER_SIZE);
    put16(out + 15, 0);
    put32(out + 17, (uint32_t)(VQEAF_QEN_HEADER_SIZE + payload));
    put32(out + 21, 0);
    put32(out + 25, 0);
    put32(out + 29, 0);
    put32(out + 33, 0);
    put32(out + 37, 0);
    put32(out + 41, 0);
    put32(out + 45, 0);
    put32(out + 49, fnv1a32(out + VQEAF_QEN_HEADER_SIZE, payload));
}

static int failures;

static void expect(const char *name, int got, int want)
{
    if(got != want) {
        printf("FAIL %s: got %d want %d\n", name, got, want);
        failures++;
    } else {
        printf("ok   %s\n", name);
    }
}

int main(void)
{
    uint8_t module[VQEAF_QEN_HEADER_SIZE + 32];
    size_t size = VQEAF_QEN_HEADER_SIZE;

    make_stub(module, VQEAF_QEN_ARCH_ARM64, 0);
    expect("valid arm64 stub", vqeaf_qeapp_native_validate_module(module, size, 0), 0);
    expect("valid for arm64 manifest",
           vqeaf_qeapp_native_validate_module(module, size, VQEAF_QEN_ARCH_ARM64), 0);
    expect("arch mismatch rejected",
           vqeaf_qeapp_native_validate_module(module, size, VQEAF_QEN_ARCH_X86_64), -1);
    expect("truncated rejected",
           vqeaf_qeapp_native_validate_module(module, 32, 0), -1);
    expect("null rejected", vqeaf_qeapp_native_validate_module(NULL, size, 0), -1);
    expect("oversized rejected",
           vqeaf_qeapp_native_validate_module(module, VQEAF_QEN_MAX_SIZE + 1, 0), -1);

    make_stub(module, VQEAF_QEN_ARCH_ARM64, 0);
    module[0] = 'X';
    expect("bad magic rejected", vqeaf_qeapp_native_validate_module(module, size, 0), -1);

    make_stub(module, VQEAF_QEN_ARCH_ARM64, 0);
    module[10] = 3;
    expect("abi v3 accepted", vqeaf_qeapp_native_validate_module(module, size, 0), 0);

    make_stub(module, VQEAF_QEN_ARCH_ARM64, 0);
    module[10] = 4;
    expect("abi v4 accepted", vqeaf_qeapp_native_validate_module(module, size, 0), 0);

    make_stub(module, VQEAF_QEN_ARCH_ARM64, 0);
    module[10] = 5;
    expect("abi v5 accepted", vqeaf_qeapp_native_validate_module(module, size, 0), 0);

    make_stub(module, VQEAF_QEN_ARCH_ARM64, 0);
    module[10] = 6;
    expect("wrong abi 6 rejected", vqeaf_qeapp_native_validate_module(module, size, 0), -1);

    make_stub(module, VQEAF_QEN_ARCH_ARM64, 0);
    module[10] = 1;
    expect("unsupported abi 1 rejected",
           vqeaf_qeapp_native_validate_module(module, size, 0), -1);

    make_stub(module, 9, 0);
    expect("unknown arch rejected", vqeaf_qeapp_native_validate_module(module, size, 0), -1);

    make_stub(module, VQEAF_QEN_ARCH_ARM64, 0);
    module[12] = 1;
    expect("nonzero flags rejected", vqeaf_qeapp_native_validate_module(module, size, 0), -1);

    make_stub(module, VQEAF_QEN_ARCH_ARM64, 0);
    put32(module + 17, (uint32_t)(size + 1));
    expect("size mismatch rejected", vqeaf_qeapp_native_validate_module(module, size, 0), -1);

    make_stub(module, VQEAF_QEN_ARCH_ARM64, 0);
    put32(module + 21, (uint32_t)(size + 1));
    expect("section overrun rejected", vqeaf_qeapp_native_validate_module(module, size, 0), -1);

    make_stub(module, VQEAF_QEN_ARCH_ARM64, 0);
    put32(module + 49, 0);
    expect("bad hash rejected", vqeaf_qeapp_native_validate_module(module, size, 0), -1);

    make_stub(module, VQEAF_QEN_ARCH_ARM64, 4);
    module[VQEAF_QEN_HEADER_SIZE] = 1;
    expect("tampered payload rejected",
           vqeaf_qeapp_native_validate_module(module, size + 4, 0), -1);

    make_stub(module, VQEAF_QEN_ARCH_ARM64, 4);
    expect("valid payload accepted",
           vqeaf_qeapp_native_validate_module(module, size + 4, 0), 0);

    make_stub(module, VQEAF_QEN_ARCH_ARM64, 0);
    module[63] = 1;
    expect("reserved byte rejected", vqeaf_qeapp_native_validate_module(module, size, 0), -1);

    expect("arch name arm64", vqeaf_qeapp_native_arch_id("arm64"), VQEAF_QEN_ARCH_ARM64);
    expect("arch name x86_64", vqeaf_qeapp_native_arch_id("x86_64"), VQEAF_QEN_ARCH_X86_64);
    expect("arch name unknown", vqeaf_qeapp_native_arch_id("mips"), 0);

    if(failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all native module validation tests passed\n");
    return 0;
}
