/* Host gate for the Violet Native Platform Backend: real DTB, no fake ops. */
#include "vqeaf_assert.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vqeaf/violet_platform.h"
#include "vqeaf/wcn3990_phase2.h"
#include "vqeaf/wcn3990.h"

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long n;
    if(!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if(n <= 0) { fclose(f); return NULL; }
    b = malloc((size_t)n);
    if(!b || fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b); fclose(f); return NULL;
    }
    fclose(f); *len = (size_t)n; return b;
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static int find_icnss_interrupts(const uint8_t *b, size_t len,
                                 size_t *data_off, uint32_t *data_len)
{
    static const char node_name[] = "qcom,icnss@18800000";
    const uint8_t *strings;
    uint32_t so, sto, ss, sts;
    size_t pos, end, name_at;
    int found = 0;
    if(len < 40u) return -1;
    so = rd32(b + 8); sto = rd32(b + 12);
    ss = rd32(b + 32); sts = rd32(b + 36);
    if(so > len || sts > len - so || sto > len || ss > len - sto) return -1;
    strings = b + sto; pos = so; end = so + sts;
    while(pos + 4u <= end) {
        uint32_t tok = rd32(b + pos);
        pos += 4u;
        if(tok == 1u) {
            size_t n = 0;
            while(pos + n < end && b[pos + n] != 0) n++;
            if(pos + n >= end) return -1;
            name_at = pos;
            pos += n + 1u; pos = (pos + 3u) & ~(size_t)3u;
            if(n == sizeof(node_name) - 1u &&
               memcmp(b + name_at, node_name, sizeof(node_name) - 1u) == 0) {
                found = 1;
                break;
            }
        } else if(tok == 3u) {
            uint32_t l;
            if(pos + 8u > end) return -1;
            l = rd32(b + pos); pos += 8u;
            if(l > end - pos) return -1;
            pos += (l + 3u) & ~(uint32_t)3u;
        } else if(tok == 9u) {
            break;
        } else if(tok != 2u && tok != 4u) {
            return -1;
        }
    }
    if(!found) return -1;
    while(pos + 4u <= end) {
        uint32_t tok = rd32(b + pos);
        uint32_t l, no;
        pos += 4u;
        if(tok == 2u || tok == 9u) break;
        if(tok == 4u) continue;
        if(tok != 3u || pos + 8u > end) return -1;
        l = rd32(b + pos); no = rd32(b + pos + 4u); pos += 8u;
        if(l > end - pos) return -1;
        if(no < ss) {
            size_t n = 0;
            while(no + n < ss && strings[no + n] != 0) n++;
            if(no + n < ss && n == 10u &&
               memcmp(strings + no, "interrupts", 10u) == 0) {
                *data_off = pos; *data_len = l; return 0;
            }
        }
        pos += (l + 3u) & ~(uint32_t)3u;
    }
    return -1;
}

static uint8_t *make_irq_overflow_dtb(const uint8_t *src, size_t len,
                                      size_t *out_len)
{
    size_t data_off, insert_at;
    uint32_t data_len, total, sto, sts;
    uint8_t *out;
    if(len > SIZE_MAX - 12u || find_icnss_interrupts(src, len, &data_off,
                                                   &data_len) != 0 ||
       data_len != VQEAF_WCN3990_CE_MAX * 12u) return NULL;
    total = rd32(src + 4); sto = rd32(src + 12); sts = rd32(src + 36);
    if(total != len || sto < data_off + data_len || total > UINT32_MAX - 12u ||
       sts > UINT32_MAX - 12u) return NULL;
    out = malloc(len + 12u);
    if(!out) return NULL;
    insert_at = data_off + data_len;
    memcpy(out, src, insert_at);
    memcpy(out + insert_at, src + insert_at - 12u, 12u);
    wr32(out + insert_at + 4u, 426u);
    memcpy(out + insert_at + 12u, src + insert_at, len - insert_at);
    wr32(out + data_off - 8u, data_len + 12u);
    wr32(out + 4, total + 12u);
    wr32(out + 12, sto + 12u);
    wr32(out + 36, sts + 12u);
    *out_len = len + 12u;
    return out;
}

static uint8_t *make_deep_dtb(size_t *out_len)
{
    uint32_t depth = 64u;
    size_t struct_len = 12u + depth * 8u + (depth + 1u) * 4u;
    size_t total = 56u + struct_len, pos = 56u;
    uint8_t *out = malloc(total);
    if(!out) return NULL;
    memset(out, 0, total);
    wr32(out, 0xd00dfeedu); wr32(out + 4, (uint32_t)total);
    wr32(out + 8, 56u); wr32(out + 12, (uint32_t)total);
    wr32(out + 16, 40u); wr32(out + 20, 17u); wr32(out + 24, 16u);
    wr32(out + 32, 0u); wr32(out + 36, (uint32_t)struct_len);
    wr32(out + pos, 1u); pos += 8u;
    for(uint32_t i = 0; i < depth; i++) {
        wr32(out + pos, 1u); out[pos + 4u] = 'n'; pos += 8u;
    }
    for(uint32_t i = 0; i <= depth; i++) {
        wr32(out + pos, 2u); pos += 4u;
    }
    wr32(out + pos, 9u); pos += 4u;
    assert(pos == total);
    *out_len = total;
    return out;
}

int main(int argc, char **argv)
{
    uint8_t *dtb, *overflow_dtb, *deep_dtb;
    size_t len, overflow_len, deep_len;
    const vqeaf_violet_resources_t *r;
    const vqeaf_provider_census_t *p;

    if(argc != 2) return 2;
    dtb = read_file(argv[1], &len);
    assert(dtb != NULL);

    assert(vqeaf_wcn3990_phase2_register() == 0);
    assert(vqeaf_wcn3990_register() == 0);
    assert(vqeaf_violet_probe(NULL, 0) < 0);
    assert(vqeaf_violet_probe(dtb, 8) < 0);
    assert(vqeaf_violet_resources() == NULL);
    {
        uint32_t so = rd32(dtb + 8), sto = rd32(dtb + 12);
        uint32_t ss = rd32(dtb + 32), sts = rd32(dtb + 36);
        wr32(dtb + 8, UINT32_MAX); wr32(dtb + 36, 16u);
        assert(vqeaf_violet_probe(dtb, len) == -EINVAL);
        assert(vqeaf_violet_resources() == NULL);
        wr32(dtb + 8, so); wr32(dtb + 36, sts);
        wr32(dtb + 12, UINT32_MAX); wr32(dtb + 32, 16u);
        assert(vqeaf_violet_probe(dtb, len) == -EINVAL);
        assert(vqeaf_violet_resources() == NULL);
        wr32(dtb + 12, sto); wr32(dtb + 32, ss);
    }
    deep_dtb = make_deep_dtb(&deep_len);
    assert(deep_dtb != NULL);
    assert(vqeaf_violet_probe(deep_dtb, deep_len) == -EIO);
    assert(vqeaf_violet_resources() == NULL);
    free(deep_dtb);
    assert(vqeaf_violet_probe(dtb, len) == 0);

    r = vqeaf_violet_resources();
    p = vqeaf_violet_providers();
    assert(r != NULL && p != NULL);
    assert(strcmp(r->compatible, "qcom,icnss") == 0);
    assert(r->mmio_count >= 1);
    assert(r->mmio[0][0] == 0x18800000ull);
    assert(r->mmio[0][1] == 0x800000ull);
    assert(r->ce_irq_count == 12);
    assert(r->ce_irq[0] == 414);
    assert(r->ce_irq[11] == 425);
    assert(r->msa_requested_size == 0x100000u);
    /* MSA resolved via qcom,wlan-msa-fixed-region phandle -> reserved-memory
     * reg, using the parent's 2+2 address/size cells (from the real DTS). */
    assert(r->msa_resolved == 1);
    assert(r->msa_base == 0x93300000ull);
    assert(r->msa_size == 0x200000ull);
    assert(r->msa_size >= r->msa_requested_size);
    assert(r->has_smmu == 1 && r->smmu_id == 0x1640u);
    /* Regulator supplies are discovered from DT but enabling is UNSUPPORTED. */
    assert(r->regulator_count == 4);
    assert(strcmp(r->regulator_supplies[0], "vdd-cx-mx-supply") == 0);
    assert(strcmp(r->regulator_supplies[3], "vdd-3.3-ch0-supply") == 0);
    assert(r->regulator_phandles[0] != 0 && r->regulator_phandles[3] != 0);
    assert(p->mmio == VQEAF_PROV_VERIFIED);
    assert(p->irq == VQEAF_PROV_VERIFIED);
    assert(p->memory == VQEAF_PROV_VERIFIED);
    assert(p->regulators == VQEAF_PROV_UNSUPPORTED);
    assert(p->clocks == VQEAF_PROV_UNSUPPORTED);
    assert(p->dma == VQEAF_PROV_UNSUPPORTED);

    overflow_dtb = make_irq_overflow_dtb(dtb, len, &overflow_len);
    assert(overflow_dtb != NULL);
    assert(vqeaf_violet_probe(overflow_dtb, overflow_len) == 0);
    r = vqeaf_violet_resources();
    assert(r != NULL && r->ce_irq_count == 13);
    assert(vqeaf_violet_bind_phase2() == -EINVAL);
    assert(vqeaf_wcn3990_phase2_state() == VQEAF_WCN_P2_OFF);
    free(overflow_dtb);
    assert(vqeaf_violet_probe(dtb, len) == 0);
    {
        size_t data_off;
        uint32_t data_len, saved_irqs[VQEAF_WCN3990_CE_MAX];
        assert(find_icnss_interrupts(dtb, len, &data_off, &data_len) == 0);
        assert(data_len == VQEAF_WCN3990_CE_MAX * 12u);
        for(uint32_t i = 0; i < VQEAF_WCN3990_CE_MAX; i++)
            saved_irqs[i] = rd32(dtb + data_off + i * 12u + 4u);
        wr32(dtb + data_off + 11u * 12u + 4u, 426u);
        assert(vqeaf_violet_probe(dtb, len) == 0);
        assert(vqeaf_violet_bind_wcn3990() == -EINVAL);
        for(uint32_t i = 0; i < VQEAF_WCN3990_CE_MAX; i++)
            wr32(dtb + data_off + i * 12u + 4u, saved_irqs[i]);
        assert(vqeaf_violet_probe(dtb, len) == 0);
        wr32(dtb + data_off + 4u, UINT32_MAX);
        for(uint32_t i = 1; i < VQEAF_WCN3990_CE_MAX; i++)
            wr32(dtb + data_off + i * 12u + 4u, i + 31u);
        assert(vqeaf_violet_probe(dtb, len) == 0);
        assert(vqeaf_violet_bind_wcn3990() == -EINVAL);
        for(uint32_t i = 0; i < VQEAF_WCN3990_CE_MAX; i++)
            wr32(dtb + data_off + i * 12u + 4u, saved_irqs[i]);
        assert(vqeaf_violet_probe(dtb, len) == 0);
    }

    /* Resource discovery is now complete, but the freestanding kernel has no
     * MMIO primitive: probe must stop at map_snoc with -ENOSYS and roll back.
     * This is a controlled failure, never a fake success. */
    assert(vqeaf_violet_bind_phase2() == -ENOSYS);
    assert(vqeaf_wcn3990_phase2_state() == VQEAF_WCN_P2_FAILED);
    assert(vqeaf_wcn3990_phase2_error() == -ENOSYS);
    assert(vqeaf_violet_bind_wcn3990() == -ENOSYS);

    free(dtb);
    printf("vqeaf_violet_platform: ALL OK\n");
    return 0;
}
