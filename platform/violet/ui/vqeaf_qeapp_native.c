#include "vqeaf_qeapp_native.h"

#include <string.h>

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
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

int vqeaf_qeapp_native_arch_id(const char *name)
{
    if(name == NULL) return 0;
    if(strcmp(name, "arm64") == 0) return VQEAF_QEN_ARCH_ARM64;
    if(strcmp(name, "x86_64") == 0) return VQEAF_QEN_ARCH_X86_64;
    return 0;
}

int vqeaf_qeapp_native_validate_module(const uint8_t *data, size_t size,
                                       int expected_arch)
{
    uint8_t module_version;
    uint8_t abi_version;
    uint8_t arch;
    uint8_t flags;
    uint16_t header_size;
    uint16_t section_count;
    uint32_t module_size;
    uint32_t code_size;
    uint32_t rodata_size;
    uint32_t data_size;
    uint32_t imports_size;
    uint32_t exports_size;
    uint32_t entry_offset;
    uint32_t payload_hash;

    if(data == NULL) return -1;
    if(size < VQEAF_QEN_HEADER_SIZE) return -1;
    if(size > VQEAF_QEN_MAX_SIZE) return -1;
    if(memcmp(data, "QEAF-QEN1", 9) != 0) return -1;

    module_version = data[9];
    abi_version    = data[10];
    arch           = data[11];
    flags          = data[12];
    header_size    = le16(data + 13);
    section_count  = le16(data + 15);
    module_size    = le32(data + 17);
    code_size      = le32(data + 21);
    rodata_size    = le32(data + 25);
    data_size      = le32(data + 29);
    imports_size   = le32(data + 41);
    exports_size   = le32(data + 45);
    entry_offset   = le32(data + 37);
    payload_hash   = le32(data + 49);

    if(module_version != 1u) return -1;
    if(abi_version != 2u && abi_version != 3u && abi_version != 4u &&
       abi_version != 5u) return -1;
    if(arch != VQEAF_QEN_ARCH_ARM64 && arch != VQEAF_QEN_ARCH_X86_64) return -1;
    if(expected_arch != 0 && arch != (uint8_t)expected_arch) return -1;
    if(flags != 0u) return -1;
    if(header_size != VQEAF_QEN_HEADER_SIZE) return -1;
    if(section_count > VQEAF_QEN_MAX_SECTIONS) return -1;
    if(module_size != size) return -1;
    {
        uint64_t sections = (uint64_t)code_size + rodata_size + data_size +
                            imports_size + exports_size;
        if(sections > (uint64_t)(size - VQEAF_QEN_HEADER_SIZE)) return -1;
    }
    {
        size_t off = VQEAF_QEN_HEADER_SIZE + code_size + rodata_size + data_size;
        size_t end = off + imports_size;
        size_t i = off;
        while(i < end) {
            size_t j = i;
            while(j < end && data[j] != 0u) j++;
            if(j >= end || j == i) return -1;
            for(size_t k = i; k < j; k++)
                if(data[k] < 0x20u || data[k] > 0x7eu) return -1;
            i = j + 1;
        }
    }
    {
        size_t off = VQEAF_QEN_HEADER_SIZE + code_size + rodata_size +
                     data_size + imports_size;
        size_t end = off + exports_size;
        size_t i = off;
        while(i < end) {
            size_t j = i;
            while(j < end && data[j] != 0u) j++;
            if(j >= end || j == i || j + 5 > end) return -1;
            if(code_size != 0u && le32(data + j + 1) >= code_size) return -1;
            i = j + 5;
        }
    }
    if(code_size != 0u && entry_offset >= code_size) return -1;
    if(payload_hash != fnv1a32(data + VQEAF_QEN_HEADER_SIZE,
                               size - VQEAF_QEN_HEADER_SIZE))
        return -1;
    for(int i = 55; i < 64; i++)
        if(data[i] != 0u) return -1;
    return 0;
}
