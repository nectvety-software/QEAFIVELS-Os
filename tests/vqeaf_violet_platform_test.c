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

int main(int argc, char **argv)
{
    uint8_t *dtb;
    size_t len;
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
