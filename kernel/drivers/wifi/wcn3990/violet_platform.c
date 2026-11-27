/* SPDX-License-Identifier: MIT */
/*
 * Violet Native Platform Backend for WCN3990.
 * Parses the stock SM6150 DTB node `qcom,icnss` and exposes only DT-verified
 * resources. Hardware operations require injected providers and fail closed.
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "vqeaf/violet_platform.h"
#include "vqeaf/wcn3990_phase2.h"
#include "vqeaf/wcn3990.h"
#include "vqeaf/wcn3990_ce.h"
#include "vqeaf/violet_boot.h"
#include "mre_mmu.h"

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

/* ARM GIC DT binding stores SPI/PPI indexes, while the GIC CPU interface
 * dispatches INTIDs. SPI n maps to INTID n+32; PPI n maps to n+16. */
static void parse_irq(const uint8_t *d, uint32_t len, uint32_t cells,
                      int is_gic, uint32_t *out, uint32_t *out_type,
                      uint32_t *out_flags, size_t max, size_t *count)
{
    uint32_t off = 0, step;
    *count = 0;
    if(cells == 0 || cells > 4u) return;
    step = cells * 4u;
    while(off + step <= len && *count < max) {
        uint32_t type = cells >= 3u ? rd32(d + off) : 0u;
        uint32_t raw = rd32(d + off + (cells >= 2u ? 4u : 0u));
        uint32_t irq = raw;
        uint32_t flags = cells >= 3u ? rd32(d + off + 8u) : 0u;
        if(is_gic) {
            if(cells != 3u) { off += step; continue; }
            if(type == 0u) {
                if(raw > 987u) { off += step; continue; }
                irq = raw + 32u;
            } else if(type == 1u) {
                if(raw > 15u) { off += step; continue; }
                irq = raw + 16u;
            } else {
                off += step;
                continue;
            }
        }
        if(irq >= 16u && irq < 1020u) {
            out[*count] = irq;
            if(out_type) out_type[*count] = type;
            if(out_flags) out_flags[*count] = flags;
            (*count)++;
        }
        off += step;
    }
}

static int parse_reg_names(const uint8_t *own, size_t own_len,
                           char out[3][32], size_t expected)
{
    static const char *const wanted[3] = {
        "membase", "smmu_iova_base", "smmu_iova_ipa"
    };
    const uint8_t *data;
    uint32_t len, off = 0, index = 0;
    int rc = node_prop(own, own_len, "reg-names", &data, &len);
    if(rc) return rc;
    while(off < len) {
        uint32_t n = 0;
        while(off + n < len && data[off + n] != 0u) n++;
        if(n == 0u || off + n >= len || index >= 3u ||
           n >= sizeof(out[0]) || strlen(wanted[index]) != n ||
           memcmp(data + off, wanted[index], n) != 0)
            return -EINVAL;
        memcpy(out[index], data + off, n);
        out[index][n] = '\0';
        off += n + 1u;
        index++;
    }
    return (index == expected && index == 3u) ? 0 : -EINVAL;
}

static int phandle_is_gic(const void *fdt, uint32_t phandle)
{
    PhandleCtx pc;
    const uint8_t *data;
    uint32_t len, off = 0;
    if(find_phandle_node(fdt, phandle, &pc) != 0 ||
       node_prop(pc.own, pc.own_len, "compatible", &data, &len) != 0)
        return 0;
    while(off < len) {
        uint32_t n = 0;
        while(off + n < len && data[off + n] != 0u) n++;
        if(n == 10u && memcmp(data + off, "arm,gic-v3", 10u) == 0) return 1;
        if(n == 11u && memcmp(data + off, "arm,gic-400", 11u) == 0) return 1;
        off += n + 1u;
    }
    return 0;
}

/* Root-level interrupt-parent applies to descendants unless overridden. */
static int root_node_prop(const void *fdt, const char *want,
                          const uint8_t **data, uint32_t *out_len)
{
    const uint8_t *b = (const uint8_t *)fdt;
    uint32_t so = rd32(b + 8), ss = rd32(b + 36);
    size_t pos = 0, start;
    if(ss < 8u || rd32(b + so) != FDT_BEGIN_NODE) return -EIO;
    pos = 4u;
    while(pos < ss && b[so + pos] != 0u) pos++;
    if(pos >= ss) return -EIO;
    pos = (pos + 1u + 3u) & ~((size_t)3u);
    start = pos;
    while(pos + 4u <= ss) {
        uint32_t tok = rd32(b + so + pos);
        if(tok != FDT_PROP) break;
        pos += 4u;
        if(pos + 8u > ss) return -EIO;
        uint32_t len = rd32(b + so + pos);
        pos += 8u;
        if(len > ss - pos) return -EIO;
        pos += (len + 3u) & ~((size_t)3u);
    }
    return node_prop(b + so + start, pos - start, want, data, out_len);
}

static int phandle_interrupts_to_gic(const void *fdt, uint32_t phandle,
                                     int *is_pdc)
{
    PhandleCtx pc;
    const uint8_t *data;
    uint32_t len, off = 0;
    if(is_pdc) *is_pdc = 0;
    if(phandle_is_gic(fdt, phandle)) return 1;
    if(find_phandle_node(fdt, phandle, &pc) != 0 ||
       node_prop(pc.own, pc.own_len, "compatible", &data, &len) != 0)
        return 0;
    while(off < len) {
        uint32_t n = 0;
        while(off + n < len && data[off + n] != 0u) n++;
        if(n == 15u && memcmp(data + off, "qcom,pdc-sm6150", 15u) == 0) {
            const uint8_t *parent;
            uint32_t parent_len;
            if(is_pdc) *is_pdc = 1;
            if(node_prop(pc.own, pc.own_len, "interrupt-parent",
                         &parent, &parent_len) == 0 && parent_len >= 4u)
                return phandle_is_gic(fdt, rd32(parent));
            return 0;
        }
        off += n + 1u;
    }
    return 0;
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
    g_res.status_enabled = 1;
    if(node_prop(icnss.own, icnss.own_len, "status", &d, &len) == 0) {
        if(len == 0u || d[len - 1u] != 0u) return -EINVAL;
        if(strcmp((const char *)d, "disabled") == 0) g_res.status_enabled = 0;
        else if(strcmp((const char *)d, "okay") != 0 &&
                strcmp((const char *)d, "ok") != 0) return -EINVAL;
        if(!g_res.status_enabled) return -ENODEV;
    }
    static const char *supplies[4] = {
        "vdd-cx-mx-supply", "vdd-1.8-xo-supply",
        "vdd-1.3-rfa-supply", "vdd-3.3-ch0-supply"
    };
    size_t k;
    rc = parse_reg_cells(icnss.own, icnss.own_len,
                         icnss.parent, icnss.parent_len,
                         g_res.mmio, &g_res.mmio_count);
    if(rc != 0 || g_res.mmio_count != 3u) return rc ? rc : -EINVAL;
    rc = parse_reg_names(icnss.own, icnss.own_len,
                         g_res.reg_names, g_res.mmio_count);
    if(rc != 0) return rc;
    g_res.smmu_iova_base = g_res.mmio[1][0];
    g_res.smmu_iova_size = g_res.mmio[1][1];
    g_res.smmu_ipa_base = g_res.mmio[2][0];
    g_res.smmu_ipa_size = g_res.mmio[2][1];

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
    /* interrupt-parent is inherited by child nodes in the Device Tree. */
    rc = node_prop(icnss.own, icnss.own_len, "interrupt-parent", &d, &len);
    if(rc == -ENOENT)
        rc = node_prop(icnss.parent, icnss.parent_len, "interrupt-parent", &d, &len);
    if(rc == -ENOENT)
        rc = root_node_prop(fdt, "interrupt-parent", &d, &len);
    if(rc == 0 && len >= 4u) {
        PhandleCtx pc;
        g_res.irq_parent_phandle = rd32(d);
        if(find_phandle_node(fdt, g_res.irq_parent_phandle, &pc) == 0) {
            const uint8_t *cd;
            uint32_t clen;
            g_res.irq_parent_found = 1;
            g_res.irq_parent_is_gic = phandle_interrupts_to_gic(
                fdt, g_res.irq_parent_phandle, &g_res.irq_parent_is_pdc);
            if(node_prop(pc.own, pc.own_len, "#interrupt-cells",
                         &cd, &clen) == 0 && clen >= 4u)
                g_res.irq_cells = rd32(cd);
        }
    }
    if(node_prop(icnss.own, icnss.own_len, "interrupts", &d, &len) == 0)
        parse_irq(d, len,
                  g_res.irq_parent_found ? g_res.irq_cells : 3u,
                  g_res.irq_parent_is_gic,
                  g_res.ce_irq, g_res.ce_irq_type, g_res.ce_irq_flags,
                  16u, &g_res.ce_irq_count);
    if(node_prop(icnss.own, icnss.own_len, "qcom,smmu-s1-bypass", &d, &len) == 0)
        g_res.smmu_s1_bypass = 1;
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
    g_prov.irq = (g_res.irq_parent_is_gic &&
                  g_res.ce_irq_count == 12u) ?
                 VQEAF_PROV_VERIFIED : VQEAF_PROV_UNKNOWN;
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

/* Hardware callbacks are provided by the verified SM6150 PMIC/RPMh/SMMU
 * services. The platform parser never guesses rail voltages or clock IDs. */
static vqeaf_violet_hw_ops_t g_hw_ops;
static int g_hw_ops_ready;
static int g_snoc_mapped;
static VqeafWcn3990Ce g_ce;
static VqeafWcn3990P2Device g_p2;
static struct { uint8_t ce_id; } g_ce_irq_ctx[VQEAF_WCN3990_CE_MAX];

int vqeaf_violet_register_hw_ops(const vqeaf_violet_hw_ops_t *ops)
{
    if(!g_res.probed || !g_res.status_enabled || ops == NULL || g_hw_ops_ready)
        return -EINVAL;
    if(!ops->regulators_enable || !ops->regulators_disable ||
       !ops->clocks_enable || !ops->clocks_disable ||
       !ops->icnss_power_on || !ops->icnss_power_off ||
       !ops->msa_prepare || !ops->msa_release ||
       !ops->dma_alloc || !ops->dma_free || !ops->ce_irq ||
       !ops->now_ms || !ops->delay_ms)
        return -EINVAL;
    memcpy(&g_hw_ops, ops, sizeof(g_hw_ops));
    g_hw_ops_ready = 1;
    return 0;
}

int vqeaf_violet_hw_provider_ready(void)
{
    return g_hw_ops_ready ? 1 : 0;
}

int vqeaf_violet_map_snoc_mmu(void)
{
    mre_mmu_region_t region;
    uint64_t base, size, start, end;
    if(!g_res.probed || g_res.mmio_count != 3u) return -ENODEV;
    base = g_res.mmio[0][0];
    size = g_res.mmio[0][1];
    if(base == 0u || size == 0u || base > UINT64_MAX - size ||
       size < VQEAF_WCN3990_CE_REGISTER_END)
        return -EINVAL;
    start = base & ~((uint64_t)0xfffu);
    if(base + size > UINT64_MAX - 0xfffu) return -ERANGE;
    end = (base + size + 0xfffu) & ~((uint64_t)0xfffu);
    if(end <= start) return -ERANGE;
    region.virtual_address = start;
    region.physical_address = start;
    region.size = end - start;
    region.type = MRE_MEMORY_DEVICE;
    region.writable = 1u;
    region.executable = 0u;
    {
        mre_status_t st = mre_mmu_map_range(&region);
        if(st != MRE_OK) return (int)st;
    }
    g_snoc_mapped = 1;
    return 0;
}

static int ce_mmio_read32(void *ctx, uint32_t offset, uint32_t *value)
{
    volatile uint32_t *reg;
    (void)ctx;
    if(!g_snoc_mapped || value == NULL || (offset & 3u) != 0u ||
       offset > g_res.mmio[0][1] || g_res.mmio[0][1] - offset < 4u)
        return -EINVAL;
    reg = (volatile uint32_t *)(uintptr_t)(g_res.mmio[0][0] + offset);
    *value = *reg;
    return 0;
}

static int ce_mmio_write32(void *ctx, uint32_t offset, uint32_t value)
{
    volatile uint32_t *reg;
    (void)ctx;
    if(!g_snoc_mapped || (offset & 3u) != 0u ||
       offset > g_res.mmio[0][1] || g_res.mmio[0][1] - offset < 4u)
        return -EINVAL;
    reg = (volatile uint32_t *)(uintptr_t)(g_res.mmio[0][0] + offset);
    *reg = value;
    return 0;
}

static void ce_mmio_barrier(void *ctx)
{
    (void)ctx;
#if defined(__aarch64__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

static int op_map_snoc(void *ctx, uint64_t base, uint64_t size)
{
    VqeafWcn3990CeMmio mmio;
    (void)ctx;
    if(!g_snoc_mapped || !vqeaf_violet_mmu_is_enabled() ||
       base != g_res.mmio[0][0] || size != g_res.mmio[0][1])
        return -ENOSYS;
    memset(&mmio, 0, sizeof(mmio));
    mmio.window_base = base;
    mmio.window_size = size;
    mmio.dma_bits = VQEAF_WCN3990_DMA_BITS;
    mmio.address_high_mask = 0x1fu;
    mmio.mmio.ctx = NULL;
    mmio.mmio.read32 = ce_mmio_read32;
    mmio.mmio.write32 = ce_mmio_write32;
    mmio.mmio.barrier = ce_mmio_barrier;
    return vqeaf_wcn3990_ce_init(&g_ce, &mmio);
}

static void op_unmap_snoc(void *ctx)
{
    (void)ctx;
    /* The SNOC page-table mapping remains for the lifetime of the kernel. */
}

static int op_regulators_enable(void *ctx)
{
    (void)ctx;
    return g_hw_ops_ready ?
        g_hw_ops.regulators_enable(g_hw_ops.ctx, &g_res) : -ENOSYS;
}
static void op_regulators_disable(void *ctx)
{
    (void)ctx;
    if(g_hw_ops_ready) g_hw_ops.regulators_disable(g_hw_ops.ctx, &g_res);
}
static int op_clocks_enable(void *ctx)
{
    (void)ctx;
    return g_hw_ops_ready ? g_hw_ops.clocks_enable(g_hw_ops.ctx, &g_res) : -ENOSYS;
}
static void op_clocks_disable(void *ctx)
{
    (void)ctx;
    if(g_hw_ops_ready) g_hw_ops.clocks_disable(g_hw_ops.ctx, &g_res);
}
static int op_icnss_power_on(void *ctx)
{
    (void)ctx;
    return g_hw_ops_ready ? g_hw_ops.icnss_power_on(g_hw_ops.ctx, &g_res) : -ENOSYS;
}
static void op_icnss_power_off(void *ctx)
{
    (void)ctx;
    if(g_hw_ops_ready) g_hw_ops.icnss_power_off(g_hw_ops.ctx, &g_res);
}
static int op_msa_prepare(void *ctx, uint64_t base, uint64_t size)
{
    (void)ctx;
    if(!g_hw_ops_ready || !g_res.msa_resolved || base != g_res.msa_base ||
       size != g_res.msa_size || size < g_res.msa_requested_size ||
       !g_res.has_smmu || !g_res.smmu_s1_bypass ||
       !g_res.smmu_iova_size || !g_res.smmu_ipa_size)
        return -ENOSYS;
    return g_hw_ops.msa_prepare(g_hw_ops.ctx, &g_res);
}
static void op_msa_release(void *ctx)
{
    (void)ctx;
    if(g_hw_ops_ready) g_hw_ops.msa_release(g_hw_ops.ctx, &g_res);
}

static void ce_irq_dispatch(uint32_t irq, void *context)
{
    uint8_t id = (uint8_t)(uintptr_t)context;
    if(g_hw_ops_ready && id < VQEAF_WCN3990_CE_MAX &&
       irq == g_res.ce_irq[id])
        (void)g_hw_ops.ce_irq(g_hw_ops.ctx, id, irq);
}

static int op_request_irq(void *ctx, uint32_t irq, uint8_t id)
{
    int rc;
    (void)ctx;
    if(!g_hw_ops_ready || id >= VQEAF_WCN3990_CE_MAX ||
       irq != g_res.ce_irq[id] || !vqeaf_violet_irq_is_active())
        return -ENOSYS;
    g_ce_irq_ctx[id].ce_id = id;
    rc = vqeaf_violet_irq_request(irq, ce_irq_dispatch,
                                  (void *)(uintptr_t)id);
    if(rc != 0) g_ce_irq_ctx[id].ce_id = 0xffu;
    return rc;
}

static int op_free_irq(void *ctx, uint32_t irq, uint8_t id)
{
    int rc;
    (void)ctx;
    if(id >= VQEAF_WCN3990_CE_MAX || irq != g_res.ce_irq[id]) return -EINVAL;
    rc = vqeaf_violet_irq_release(irq);
    if(rc == 0) g_ce_irq_ctx[id].ce_id = 0xffu;
    return rc;
}

static int op_dma_alloc(void *ctx, uint64_t size, uint64_t align,
                        void **cpu, uint64_t *dma)
{
    int rc;
    (void)ctx;
    if(!g_hw_ops_ready || !cpu || !dma || !size || !align ||
       (align & (align - 1u)) != 0u) return -EINVAL;
    *cpu = NULL;
    *dma = 0u;
    rc = g_hw_ops.dma_alloc(g_hw_ops.ctx, size, align, cpu, dma);
    if(rc != 0) return rc;
    if(*cpu == NULL || *dma == 0u || (*dma & (align - 1u)) != 0u ||
       *dma >= (1ull << VQEAF_WCN3990_DMA_BITS) ||
       size > (1ull << VQEAF_WCN3990_DMA_BITS) - *dma) {
        if(*cpu != NULL) g_hw_ops.dma_free(g_hw_ops.ctx, size, *cpu, *dma);
        *cpu = NULL;
        *dma = 0u;
        return -ERANGE;
    }
    return 0;
}
static void op_dma_free(void *ctx, uint64_t size, void *cpu, uint64_t dma)
{
    (void)ctx;
    if(g_hw_ops_ready && cpu != NULL)
        g_hw_ops.dma_free(g_hw_ops.ctx, size, cpu, dma);
}
static int op_ce_configure(void *ctx, const VqeafWcn3990CeConfig *cfg,
                           uint64_t src, uint64_t dst)
{
    (void)ctx;
    return vqeaf_wcn3990_ce_configure(&g_ce, cfg, src, dst);
}
static int op_ce_disable(void *ctx, uint8_t id)
{
    (void)ctx;
    return vqeaf_wcn3990_ce_disable(&g_ce, id);
}
static int op_ce_rri(void *ctx, void *cpu, uint64_t dma)
{
    (void)ctx;
    return vqeaf_wcn3990_ce_rri_enable(&g_ce, cpu, dma);
}
static int op_ce_rri_disable(void *ctx)
{
    (void)ctx;
    return vqeaf_wcn3990_ce_rri_disable(&g_ce);
}
static uint64_t op_now_ms(void *ctx)
{
    (void)ctx;
    return g_hw_ops_ready ? g_hw_ops.now_ms(g_hw_ops.ctx) : 0u;
}
static void op_delay_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    if(g_hw_ops_ready) g_hw_ops.delay_ms(g_hw_ops.ctx, ms);
}

int vqeaf_violet_bind_phase2(void)
{
    VqeafWcn3990P2Ops *o;
    if(!g_res.probed || !g_res.status_enabled || g_res.mmio_count != 3u ||
       !g_res.irq_parent_is_gic || g_res.ce_irq_count != VQEAF_WCN3990_CE_MAX ||
       !g_res.msa_resolved || g_res.msa_size == 0u || !g_res.has_smmu ||
       !g_res.smmu_s1_bypass || !g_res.smmu_iova_size || !g_res.smmu_ipa_size)
        return -ENODEV;
    /* Resource preflight is side-effect free. Never enter PM/IRQ/DMA setup
     * until a complete, board-verified hardware provider is registered. */
    if(!g_hw_ops_ready) return -ENOSYS;
    memset(&g_p2, 0, sizeof(g_p2));
    g_p2.abi = VQEAF_WCN3990_P2_ABI;
    g_p2.size = sizeof(g_p2);
    g_p2.resources.snoc_base = g_res.mmio[0][0];
    g_p2.resources.snoc_size = g_res.mmio[0][1];
    g_p2.resources.msa_base = g_res.msa_base;
    g_p2.resources.msa_size = g_res.msa_size;
    g_p2.resources.ce_irq_count = (uint32_t)g_res.ce_irq_count;
    memcpy(g_p2.resources.ce_irq, g_res.ce_irq,
           g_res.ce_irq_count * sizeof(uint32_t));
    o = &g_p2.ops;
    o->ctx = NULL;
    o->map_snoc = op_map_snoc; o->unmap_snoc = op_unmap_snoc;
    o->regulators_enable = op_regulators_enable;
    o->regulators_disable = op_regulators_disable;
    o->clocks_enable = op_clocks_enable; o->clocks_disable = op_clocks_disable;
    o->icnss_power_on = op_icnss_power_on; o->icnss_power_off = op_icnss_power_off;
    o->msa_prepare = op_msa_prepare; o->msa_release = op_msa_release;
    o->request_irq = op_request_irq; o->free_irq = op_free_irq;
    o->dma_alloc = op_dma_alloc; o->dma_free = op_dma_free;
    o->ce_configure = op_ce_configure; o->ce_disable = op_ce_disable;
    o->ce_rri = op_ce_rri; o->ce_rri_disable = op_ce_rri_disable;
    o->now_ms = op_now_ms; o->delay_ms = op_delay_ms;
    return vqeaf_wcn3990_phase2_bind(&g_p2, &(uint32_t){0});
}

int vqeaf_violet_bind_wcn3990(void)
{
    /* Legacy one-shot path had no WLFW/netdev backend and used success stubs.
     * Only the explicit Phase-2 + WLFW lifecycle is allowed to bind hardware. */
    return -ENOSYS;
}

const char *vqeaf_violet_state_name(int state)
{
    switch(state) {
    case VQEAF_PROV_VERIFIED: return "VERIFIED";
    case VQEAF_PROV_UNSUPPORTED: return "UNSUPPORTED";
    default: return "UNKNOWN";
    }
}
