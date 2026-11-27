/* SPDX-License-Identifier: MIT */
/*
 * violet_platform.c — Violet Native Platform Backend for WCN3990.
 * Parses the stock SM6150 DTB node `qcom,icnss` and exposes only DT-verified
 * resources. Hardware ops fail closed with -ENOSYS until the freestanding
 * kernel gains real ioremap/IRQ/DMA/regulator primitives.
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "vqeaf/violet_platform.h"
#include "vqeaf/wcn3990_phase2.h"
#include "vqeaf/wcn3990.h"
#include "vqeaf/wcn3990_ce.h"

#define FDT_MAGIC 0xd00dfeedu
#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE 2u
#define FDT_PROP 3u
#define FDT_NOP 4u
#define FDT_END 9u
#define FDT_MAX_NODE_DEPTH 32u

static vqeaf_violet_resources_t g_res;
static vqeaf_provider_census_t g_prov;
static const uint8_t *g_strings;
static size_t g_strings_size;

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int header_ok(const void *blob, size_t size)
{
    const uint8_t *b = blob;
    uint32_t total, so, ss, sto, sts;
    if(b == NULL || size < 40u || rd32(b) != FDT_MAGIC) return -EINVAL;
    total = rd32(b + 4);
    so = rd32(b + 8); sto = rd32(b + 12);
    ss = rd32(b + 32); sts = rd32(b + 36);
    if(total > size || so > total || sts > total - so ||
       sto > total || ss > total - sto) return -EINVAL;
    g_strings = b + sto; g_strings_size = ss;
    return 0;
}

static const char *str_at(uint32_t off)
{
    size_t i;
    if(off >= g_strings_size) return NULL;
    for(i = off; i < g_strings_size; i++)
        if(g_strings[i] == 0) return (const char *)(g_strings + off);
    return NULL;
}

/* Forward declaration: node_prop works on a property-list region produced by
 * the walker, so it shares prop_next's bounds-checked token handling. */
static int prop_next(const uint8_t *region, size_t len, size_t *pos,
                     const char **name, const uint8_t **data, uint32_t *dlen);

static int node_prop(const uint8_t *node, size_t len, const char *want,
                     const uint8_t **data, uint32_t *out_len)
{
    size_t pos = 0;
    const char *pname;
    uint32_t dlen;
    int rc;
    while((rc = prop_next(node, len, &pos, &pname, data, &dlen)) == 1) {
        if(strcmp(pname, want) == 0) { *out_len = dlen; return 0; }
    }
    return rc ? rc : -ENOENT;
}

/* ---- generic FDT tree walker used for phandle -> reg resolution ---- */
typedef void (*NodeVisitor)(void *ctx, const char *name,
                            const uint8_t *own, size_t own_len,
                            const uint8_t *parent, size_t parent_len);

static int skip_node_body(const uint8_t *base, size_t len, size_t *pos)
{
    int depth = 0;
    while(*pos + 4u <= len) {
        uint32_t tok = rd32(base + *pos);
        *pos += 4u;
        if(tok == FDT_BEGIN_NODE) {
            while(*pos < len && base[*pos] != 0) (*pos)++;
            if(*pos >= len) return -EIO;
            (*pos)++; *pos = (*pos + 3u) & ~((size_t)3u);
            depth++;
        } else if(tok == FDT_END_NODE) {
            if(depth == 0) return 0;
            depth--;
        } else if(tok == FDT_PROP) {
            uint32_t l;
            if(*pos + 8u > len) return -EIO;
            l = rd32(base + *pos); *pos += 8u;
            if(l > len - *pos) return -EIO;
            *pos += (l + 3u) & ~((size_t)3u);
        } else if(tok == FDT_END) {
            return -EIO;
        } else if(tok != FDT_NOP) {
            return -EIO;
        }
    }
    return -EIO;
}

static int prop_next(const uint8_t *region, size_t len, size_t *pos,
                     const char **name, const uint8_t **data, uint32_t *dlen)
{
    while(*pos + 4u <= len) {
        uint32_t tok = rd32(region + *pos);
        *pos += 4u;
        if(tok == FDT_PROP) {
            uint32_t l, no;
            if(*pos + 8u > len) return -EIO;
            l = rd32(region + *pos); no = rd32(region + *pos + 4u);
            *pos += 8u;
            if(l > len - *pos) return -EIO;
            *name = str_at(no); *data = region + *pos; *dlen = l;
            *pos += (l + 3u) & ~((size_t)3u);
            return *name != NULL ? 1 : -EIO;
        }
        if(tok == FDT_BEGIN_NODE) {
            size_t n = 0;
            while(*pos + n < len && region[*pos + n] != 0) n++;
            if(*pos + n >= len) return -EIO;
            *pos += n + 1u; *pos = (*pos + 3u) & ~((size_t)3u);
            if(skip_node_body(region, len, pos)) return -EIO;
            continue;
        }
        if(tok == FDT_END || tok == FDT_END_NODE) return 0;
        if(tok != FDT_NOP) return -EIO;
    }
    return 0;
}

static int walk_nodes(const uint8_t *base, size_t len, size_t *pos,
                      NodeVisitor cb, void *ctx,
                      const uint8_t *parent, size_t parent_len, uint32_t depth)
{
    char name[128];
    size_t n = 0, own_start, props_end;
    /* Bound untrusted DT recursion: each frame carries a 128-byte name. */
    if(depth >= FDT_MAX_NODE_DEPTH) return -EIO;
    if(*pos + 4u > len || rd32(base + *pos) != FDT_BEGIN_NODE) return -EIO;
    *pos += 4u;
    while(*pos + n < len && base[*pos + n] != 0) n++;
    if(*pos + n >= len || n >= sizeof(name)) return -EIO;
    memcpy(name, base + *pos, n); name[n] = 0;
    *pos += n + 1u; *pos = (*pos + 3u) & ~((size_t)3u);
    own_start = props_end = *pos;
    while(*pos + 4u <= len) {
        uint32_t tok = rd32(base + *pos);
        *pos += 4u;
        if(tok == FDT_PROP) {
            uint32_t l;
            if(*pos + 8u > len) return -EIO;
            l = rd32(base + *pos); *pos += 8u;
            if(l > len - *pos) return -EIO;
            *pos += (l + 3u) & ~((size_t)3u);
            props_end = *pos;
        } else if(tok == FDT_BEGIN_NODE) {
            /* `pos` is past the token; recursion expects the BEGIN token. */
            size_t child_start = *pos - 4u;
            if(walk_nodes(base, len, &child_start, cb, ctx,
                          base + own_start, props_end - own_start,
                          depth + 1u)) return -EIO;
            *pos = child_start;
        } else if(tok == FDT_END_NODE) {
            cb(ctx, name, base + own_start, props_end - own_start,
               parent, parent_len);
            return 0;
        } else if(tok == FDT_END) {
            return -EIO;
        } else if(tok != FDT_NOP) {
            return -EIO;
        }
    }
    return -EIO;
}

typedef struct {
    uint32_t phandle;
    int found;
    const uint8_t *own;
    size_t own_len;
    const uint8_t *parent;
    size_t parent_len;
} PhandleCtx;

static void visit_phandle(void *ctx, const char *name,
                          const uint8_t *own, size_t own_len,
                          const uint8_t *parent, size_t parent_len)
{
    PhandleCtx *c = ctx;
    size_t pos = 0;
    const char *pname;
    const uint8_t *data;
    uint32_t dlen;
    int rc;
    (void)name;
    if(c->found) return;
    while((rc = prop_next(own, own_len, &pos, &pname, &data, &dlen)) == 1) {
        if((strcmp(pname, "phandle") == 0 ||
            strcmp(pname, "linux,phandle") == 0) &&
           dlen >= 4u && rd32(data) == c->phandle) {
            c->found = 1; c->own = own; c->own_len = own_len;
            c->parent = parent; c->parent_len = parent_len;
            return;
        }
    }
}

/* Locate a node by its phandle and return its property regions. */
static int find_phandle_node(const void *fdt, uint32_t ph, PhandleCtx *c)
{
    const uint8_t *b = fdt;
    uint32_t so = rd32(b + 8), sts = rd32(b + 36);
    size_t pos = 0;
    memset(c, 0, sizeof(*c));
    c->phandle = ph;
    while(pos + 4u <= sts) {
        if(rd32(b + so + pos) == FDT_BEGIN_NODE) {
            if(walk_nodes(b + so, sts, &pos, visit_phandle, c, NULL, 0, 0u))
                return -EIO;
        } else {
            pos += 4u;
        }
    }
    return c->found ? 0 : -ENODEV;
}

/* Resolve a phandle to its first `reg` tuple using the PARENT node's
 * #address-cells/#size-cells. No address or size value is invented. */
static int resolve_phandle_reg(const void *fdt, uint32_t ph,
                               uint64_t *base, uint64_t *size)
{
    PhandleCtx c;
    size_t pos = 0;
    uint32_t ac = 1, sc = 1, dlen, i;
    const char *pname;
    const uint8_t *data;
    int rc = find_phandle_node(fdt, ph, &c);
    if(rc) return rc;

    pos = 0;
    while((rc = prop_next(c.parent, c.parent_len, &pos,
                          &pname, &data, &dlen)) == 1) {
        if(dlen >= 4u && strcmp(pname, "#address-cells") == 0)
            ac = rd32(data);
        else if(dlen >= 4u && strcmp(pname, "#size-cells") == 0)
            sc = rd32(data);
    }
    if(ac == 0 || ac > 2 || sc == 0 || sc > 2) return -ERANGE;

    pos = 0;
    while((rc = prop_next(c.own, c.own_len, &pos, &pname, &data, &dlen)) == 1) {
        if(strcmp(pname, "reg") == 0) {
            if(dlen < (ac + sc) * 4u) return -EINVAL;
            *base = *size = 0;
            for(i = 0; i < ac; i++) *base = (*base << 32) | rd32(data + i * 4u);
            for(i = 0; i < sc; i++)
                *size = (*size << 32) | rd32(data + (ac + i) * 4u);
            return *size ? 0 : -EINVAL;
        }
    }
    return -ENOENT;
}

typedef struct {
    const char *want;
    int found;
    const uint8_t *own, *parent;
    size_t own_len, parent_len;
} CompatCtx;

static void visit_compatible(void *ctx, const char *name,
                             const uint8_t *own, size_t own_len,
                             const uint8_t *parent, size_t parent_len)
{
    CompatCtx *c = ctx;
    size_t pos = 0;
    const char *pname;
    const uint8_t *data;
    uint32_t dlen;
    int rc;
    (void)name;
    if(c->found) return;
    while((rc = prop_next(own, own_len, &pos, &pname, &data, &dlen)) == 1) {
        if(strcmp(pname, "compatible") != 0) continue;
        {
            uint32_t off = 0;
            while(off < dlen) {
                uint32_t n = 0;
                while(off + n < dlen && ((const char *)data)[off + n] != 0) n++;
                if(n > 0 && strlen(c->want) == n &&
                   strncmp((const char *)data + off, c->want, n) == 0) {
                    c->found = 1; c->own = own; c->own_len = own_len;
                    c->parent = parent; c->parent_len = parent_len;
                    return;
                }
                off += n + 1u;
            }
        }
    }
}

static int find_node_ex(const void *fdt, const char *want, CompatCtx *c)
{
    const uint8_t *b = fdt;
    uint32_t so = rd32(b + 8), sts = rd32(b + 36);
    size_t pos = 0;
    memset(c, 0, sizeof(*c));
    c->want = want;
    while(pos + 4u <= sts) {
        if(rd32(b + so + pos) == FDT_BEGIN_NODE) {
            if(walk_nodes(b + so, sts, &pos, visit_compatible, c, NULL, 0, 0u))
                return -EIO;
        } else {
            pos += 4u;
        }
    }
    return c->found ? 0 : -ENODEV;
}

/* Read reg tuples using the PARENT node's cell counts (DT default 1/1). */
static int parse_reg_cells(const uint8_t *own, size_t own_len,
                           const uint8_t *parent, size_t parent_len,
                           uint64_t out[3][2], size_t *count)
{
    size_t pos = 0;
    uint32_t ac = 1, sc = 1, dlen, i;
    const char *pname;
    const uint8_t *data;
    int rc;
    *count = 0;
    pos = 0;
    while((rc = prop_next(parent, parent_len, &pos, &pname, &data, &dlen)) == 1) {
        if(dlen >= 4u && strcmp(pname, "#address-cells") == 0) ac = rd32(data);
        else if(dlen >= 4u && strcmp(pname, "#size-cells") == 0) sc = rd32(data);
    }
    if(ac == 0 || ac > 2 || sc == 0 || sc > 2) return -ERANGE;
    pos = 0;
    while((rc = prop_next(own, own_len, &pos, &pname, &data, &dlen)) == 1) {
        if(strcmp(pname, "reg") != 0) continue;
        {
            uint32_t tuple = (ac + sc) * 4u, off = 0;
            while(off + tuple <= dlen && *count < 3u) {
                uint64_t base = 0, size = 0;
                for(i = 0; i < ac; i++) base = (base << 32) | rd32(data + off + i * 4u);
                for(i = 0; i < sc; i++)
                    size = (size << 32) | rd32(data + off + (ac + i) * 4u);
                out[*count][0] = base; out[*count][1] = size;
                (*count)++;
                off += tuple;
            }
        }
        return 0;
    }
    return -ENOENT;
}

/* GIC_SPI interrupts are <type,irq,flags> (3 cells). Read the cell count
 * from the interrupt parent; never assume a fixed layout when it is known. */
static void parse_irq(const uint8_t *d, uint32_t len, uint32_t cells,
                      uint32_t *out, size_t max, size_t *count)
{
    uint32_t off = 0, step;
    *count = 0;
    if(cells == 0 || cells > 4u) return;
    step = cells * 4u;
    while(off + step <= len && *count < max) {
        uint32_t irq = rd32(d + off + (cells >= 2u ? 4u : 0u));
        if(irq >= 32u) out[(*count)++] = irq;
        off += step;
    }
}

int vqeaf_violet_probe(const void *fdt, size_t size)
{
    CompatCtx icnss;
    const uint8_t *d;
    uint32_t len;
    int rc;

    memset(&g_res, 0, sizeof(g_res));
    memset(&g_prov, 0, sizeof(g_prov));
    rc = header_ok(fdt, size);
    if(rc) return rc;
    rc = find_node_ex(fdt, "qcom,icnss", &icnss);
    if(rc) return rc;

    strncpy(g_res.compatible, "qcom,icnss", sizeof(g_res.compatible) - 1u);
    strncpy(g_res.path, "/soc/qcom,icnss@18800000", sizeof(g_res.path) - 1u);
    static const char *supplies[4] = {
        "vdd-cx-mx-supply", "vdd-1.8-xo-supply",
        "vdd-1.3-rfa-supply", "vdd-3.3-ch0-supply"
    };
    size_t k;
    (void)parse_reg_cells(icnss.own, icnss.own_len,
                          icnss.parent, icnss.parent_len,
                          g_res.mmio, &g_res.mmio_count);

    /* Regulator supplies: resource-level discovery only. Enabling stays
     * UNSUPPORTED until the freestanding kernel has a regulator primitive. */
    for(k = 0; k < 4u; k++) {
        if(node_prop(icnss.own, icnss.own_len, supplies[k], &d, &len) == 0 &&
           len >= 4u) {
            strncpy(g_res.regulator_supplies[g_res.regulator_count],
                    supplies[k], sizeof(g_res.regulator_supplies[0]) - 1u);
            g_res.regulator_phandles[g_res.regulator_count] = rd32(d);
            g_res.regulator_count++;
        }
    }

    /* Interrupt parent tells us how many cells each interrupt uses. */
    if(node_prop(icnss.own, icnss.own_len, "interrupt-parent", &d, &len) == 0 &&
       len >= 4u) {
        PhandleCtx pc;
        g_res.irq_parent_phandle = rd32(d);
        if(find_phandle_node(fdt, g_res.irq_parent_phandle, &pc) == 0) {
            const uint8_t *cd;
            uint32_t clen;
            g_res.irq_parent_found = 1;
            if(node_prop(pc.own, pc.own_len, "#interrupt-cells",
                         &cd, &clen) == 0 && clen >= 4u)
                g_res.irq_cells = rd32(cd);
        }
    }
    if(node_prop(icnss.own, icnss.own_len, "interrupts", &d, &len) == 0)
        parse_irq(d, len,
                  g_res.irq_parent_found ? g_res.irq_cells : 3u,
                  g_res.ce_irq, 16u, &g_res.ce_irq_count);
    if(node_prop(icnss.own, icnss.own_len,"qcom,wlan-msa-memory", &d, &len) == 0 &&
       len >= 4u)
        g_res.msa_requested_size = rd32(d);
    /* iommus = <&apps_smmu 0x1640 0x1>: cell 0 is the phandle, ID/flags follow. */
    if(node_prop(icnss.own, icnss.own_len,"iommus", &d, &len) == 0 && len >= 12u) {
        g_res.smmu_id = rd32(d + 4u);
        g_res.smmu_flags = rd32(d + 8u);
        g_res.has_smmu = 1;
    }
    /* qcom,wlan-msa-fixed-region = <&wlan_msa_mem>: resolve the phandle to the
     * reserved-memory node and read its reg using the parent's cell counts. */
    if(node_prop(icnss.own, icnss.own_len,"qcom,wlan-msa-fixed-region", &d, &len) == 0 &&
       len >= 4u) {
        uint64_t base, size;
        if(resolve_phandle_reg(fdt, rd32(d), &base, &size) == 0 && size > 0 &&
           size >= g_res.msa_requested_size &&
           g_res.mmio_count > 0 &&
           g_res.mmio[0][0] <= UINT64_MAX - g_res.mmio[0][1] &&
           base <= UINT64_MAX - size &&
           !(base < g_res.mmio[0][0] + g_res.mmio[0][1] &&
             g_res.mmio[0][0] < base + size)) {
            g_res.msa_base = base;
            g_res.msa_size = size;
            g_res.msa_resolved = 1;
        }
    }
    g_prov.mmio = g_res.mmio_count ? VQEAF_PROV_VERIFIED : VQEAF_PROV_UNKNOWN;
    g_prov.irq = g_res.ce_irq_count ? VQEAF_PROV_VERIFIED : VQEAF_PROV_UNKNOWN;
    g_prov.iommu = g_res.has_smmu ? VQEAF_PROV_VERIFIED : VQEAF_PROV_UNKNOWN;
    g_prov.memory = g_res.msa_resolved ? VQEAF_PROV_VERIFIED : VQEAF_PROV_UNKNOWN;
    g_prov.regulators = VQEAF_PROV_UNSUPPORTED;
    g_prov.clocks = VQEAF_PROV_UNSUPPORTED;
    g_prov.resets = VQEAF_PROV_UNSUPPORTED;
    g_prov.dma = VQEAF_PROV_UNSUPPORTED;
    g_prov.remoteproc = VQEAF_PROV_UNSUPPORTED;
    g_res.probed = 1;
    return 0;
}

const vqeaf_violet_resources_t *vqeaf_violet_resources(void)
{
    return g_res.probed ? &g_res : NULL;
}
const vqeaf_provider_census_t *vqeaf_violet_providers(void)
{
    return g_res.probed ? &g_prov : NULL;
}

/* Fail-closed ops, one per distinct callback signature. */
static int op2(void *c, uint64_t a, uint64_t b) { (void)c;(void)a;(void)b; return -ENOSYS; }
static int op0(void *c) { (void)c; return -ENOSYS; }
static void opv(void *c) { (void)c; }
static int opirq(void *c, uint32_t i, uint8_t n) { (void)c;(void)i;(void)n; return -ENOSYS; }
static int opirq32(void *c, uint32_t first, uint32_t count)
{ (void)c;(void)first;(void)count; return -ENOSYS; }
static int opfirmware(void *c, const char *a, const char *b)
{ (void)c;(void)a;(void)b; return -ENOSYS; }
static void opirqfree(void *c, uint32_t i, uint8_t n) { (void)c;(void)i;(void)n; }
static int opdma(void *c, uint64_t a, uint64_t b, void **p, uint64_t *d)
{ (void)c;(void)a;(void)b;(void)p;(void)d; return -ENOSYS; }
static void opdmafree(void *c, uint64_t a, void *p, uint64_t d)
{ (void)c;(void)a;(void)p;(void)d; }
static VqeafWcn3990Ce g_ce;
static int opce(void *c, const VqeafWcn3990CeConfig *x, uint64_t a, uint64_t b)
{ (void)c; return vqeaf_wcn3990_ce_configure(&g_ce, x, a, b); }
static int opcedis(void *c, uint8_t id)
{ (void)c; return vqeaf_wcn3990_ce_disable(&g_ce, id); }
static void op_shutdown(void *c, VqeafWcn3990State s) { (void)c;(void)s; }

static VqeafWcn3990P2Device g_p2;
int vqeaf_violet_bind_phase2(void)
{
    VqeafWcn3990P2Ops *o;
    if(!g_res.probed || g_res.mmio_count == 0 || !g_res.msa_resolved ||
       g_res.msa_size == 0) return -ENODEV;
    if(g_res.ce_irq_count != VQEAF_WCN3990_CE_MAX) return -EINVAL;
    memset(&g_p2, 0, sizeof(g_p2));
    g_p2.abi = VQEAF_WCN3990_P2_ABI; g_p2.size = sizeof(g_p2);
    g_p2.resources.snoc_base = g_res.mmio[0][0];
    g_p2.resources.snoc_size = g_res.mmio[0][1];
    g_p2.resources.msa_base = g_res.msa_base;
    g_p2.resources.msa_size = g_res.msa_size;
    g_p2.resources.ce_irq_count = (uint32_t)g_res.ce_irq_count;
    memcpy(g_p2.resources.ce_irq, g_res.ce_irq,
           g_res.ce_irq_count * sizeof(uint32_t));
    o = &g_p2.ops;
    o->map_snoc = op2; o->unmap_snoc = opv;
    o->regulators_enable = op0; o->regulators_disable = opv;
    o->clocks_enable = op0; o->clocks_disable = opv;
    o->icnss_power_on = op0; o->icnss_power_off = opv;
    o->msa_prepare = op2; o->msa_release = opv;
    o->request_irq = opirq; o->free_irq = opirqfree;
    o->dma_alloc = opdma; o->dma_free = opdmafree;
    o->ce_configure = opce; o->ce_disable = opcedis;
    return vqeaf_wcn3990_phase2_bind(&g_p2, &(uint32_t){0});
}

static VqeafWcn3990Device g_wcn;
int vqeaf_violet_bind_wcn3990(void)
{
    VqeafWcn3990Ops *o;
    if(!g_res.probed || g_res.mmio_count == 0 || !g_res.msa_resolved ||
       g_res.msa_size == 0 || g_res.ce_irq_count == 0) return -ENODEV;
    for(size_t i = 1u; i < g_res.ce_irq_count; i++)
        if(g_res.ce_irq[i - 1u] == UINT32_MAX ||
           g_res.ce_irq[i] != g_res.ce_irq[i - 1u] + 1u) return -EINVAL;
    memset(&g_wcn, 0, sizeof(g_wcn));
    g_wcn.abi = VQEAF_WCN3990_DEVICE_ABI; g_wcn.size = sizeof(g_wcn);
    g_wcn.resources.compatible = "qcom,icnss";
    g_wcn.resources.bus = VQEAF_WCN3990_BUS_SNOC;
    g_wcn.resources.regs_base = g_res.mmio[0][0];
    g_wcn.resources.regs_size = g_res.mmio[0][1];
    g_wcn.resources.msa_base = g_res.msa_base;
    g_wcn.resources.msa_size = g_res.msa_size;
    g_wcn.resources.ce_irq_first = g_res.ce_irq[0];
    g_wcn.resources.ce_irq_count = (uint32_t)g_res.ce_irq_count;
    o = &g_wcn.ops;
    o->map_registers = op2; o->request_ce_irqs = opirq32;
    o->load_firmware_and_board_data = opfirmware;
    o->init_copy_engines = op0; o->start_wlfw = op0;
    o->register_netdev = op0; o->shutdown = op_shutdown;
    return vqeaf_wcn3990_bind(&g_wcn, &(uint32_t){0});
}

const char *vqeaf_violet_state_name(int state)
{
    switch(state) {
    case VQEAF_PROV_VERIFIED: return "VERIFIED";
    case VQEAF_PROV_UNSUPPORTED: return "UNSUPPORTED";
    default: return "UNKNOWN";
    }
}
