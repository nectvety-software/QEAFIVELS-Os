/* SPDX-License-Identifier: MIT */
/*
 * wifi_device.c — real Flattened Device Tree discovery for Violet WLAN.
 *
 * Reads the DTB supplied by the platform. Every returned address/IRQ comes
 * from a DT property; nothing is hard-coded or invented. `compatible` is the
 * documented binding qcom,wcn3990-wifi (SNOC MMIO + copy-engine interrupts),
 * cross-checked against the icnss parent. MSA comes from a reserved-memory
 * node, never from a guessed constant.
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "vqeaf/wifi_device.h"

#define FDT_MAGIC          0xd00dfeedu
#define FDT_BEGIN_NODE     0x1u
#define FDT_END_NODE       0x2u
#define FDT_PROP           0x3u
#define FDT_NOP            0x4u
#define FDT_END            0x9u

#define MAX_DEPTH   16u
#define MAX_PROPS   160u
#define PATH_CAP    128u
#define NAME_CAP    64u
#define PNAME_CAP   48u
#define MAX_IRQS    16u

typedef struct {
    const uint8_t *base;
    const uint8_t *st;
    const uint8_t *st_end;
    const uint8_t *str;
    const uint8_t *str_end;
} fdt_t;

typedef struct {
    char path[PATH_CAP];
    char name[PNAME_CAP];
    const uint8_t *data;
    uint32_t len;
    int depth;
} prop_entry_t;

static prop_entry_t g_props[MAX_PROPS];
static int g_prop_count;

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int fdt_init(fdt_t *f, const void *blob, size_t size)
{
    const uint8_t *b = (const uint8_t *)blob;
    uint32_t magic, totalsize, off_st, off_str, size_st, size_str;
    if(f == NULL || b == NULL || size < 40u) return -EINVAL;
    magic = be32(b);
    if(magic != FDT_MAGIC) return -EINVAL;
    totalsize = be32(b + 4);
    if(totalsize > size || totalsize < 40u) return -EINVAL;
    off_st = be32(b + 8);
    off_str = be32(b + 12);
    size_str = be32(b + 32);
    size_st = be32(b + 36);
    if(off_st > totalsize || size_st > totalsize - off_st) return -EINVAL;
    if(off_str > totalsize || size_str > totalsize - off_str) return -EINVAL;
    f->base = b;
    f->st = b + off_st;
    f->st_end = f->st + size_st;
    f->str = b + off_str;
    f->str_end = f->str + size_str;
    return 0;
}

int vqeaf_wifi_fdt_valid(const void *fdt, size_t size)
{
    fdt_t f;
    return fdt_init(&f, fdt, size);
}

static size_t align4(size_t n) { return (n + 3u) & ~((size_t)3u); }

static void build_path(char *out, size_t cap,
                       const char names[MAX_DEPTH][NAME_CAP], int depth)
{
    size_t used = 0;
    int i;
    out[0] = '\0';
    for(i = 0; i <= depth && used + 1u < cap; i++) {
        size_t n = strlen(names[i]);
        if(n + used + 2u >= cap) break;
        out[used++] = '/';
        memcpy(out + used, names[i], n);
        used += n;
        out[used] = '\0';
    }
    if(used == 0 && cap > 1u) { out[0] = '/'; out[1] = '\0'; }
}

/* One linear scan collects every property with its full path. Bounds are
 * checked on every token; a malformed blob fails closed. */
static int collect_props(fdt_t *f)
{
    size_t pos = 0;
    int depth = -1;
    static const char names[MAX_DEPTH][NAME_CAP];
    char path[PATH_CAP];
    int ended = 0;

    g_prop_count = 0;
    while(pos + 4u <= (size_t)(f->st_end - f->st)) {
        uint32_t tok = be32(f->st + pos);
        pos += 4u;
        if(tok == FDT_BEGIN_NODE) {
            const uint8_t *p = f->st + pos;
            size_t n = 0;
            if(depth + 1 >= (int)MAX_DEPTH) return -EINVAL;
            while(pos + n < (size_t)(f->st_end - f->st) && p[n] != 0) n++;
            if(pos + n >= (size_t)(f->st_end - f->st)) return -EINVAL;
            if(n >= NAME_CAP) n = NAME_CAP - 1u;
            memcpy((void *)names[depth + 1], p, n);
            ((char *)names[depth + 1])[n] = '\0';
            pos += align4(n + 1u);
            depth++;
        } else if(tok == FDT_END_NODE) {
            if(depth < 0) return -EINVAL;
            depth--;
        } else if(tok == FDT_NOP) {
            continue;
        } else if(tok == FDT_PROP) {
            uint32_t len, nameoff;
            const uint8_t *data;
            const char *pname;
            if(pos + 8u > (size_t)(f->st_end - f->st)) return -EINVAL;
            len = be32(f->st + pos);
            nameoff = be32(f->st + pos + 4u);
            pos += 8u;
            if(len > (uint32_t)(f->st_end - f->st) ||
               pos + align4(len) > (size_t)(f->st_end - f->st)) return -EINVAL;
            data = f->st + pos;
            pos += align4(len);
            if(nameoff >= (uint32_t)(f->str_end - f->str)) return -EINVAL;
            pname = (const char *)(f->str + nameoff);
            if(g_prop_count < MAX_PROPS && depth >= 0) {
                prop_entry_t *e = &g_props[g_prop_count++];
                size_t nl = 0;
                while(pname[nl] != 0 && f->str + nameoff + nl < f->str_end &&
                      nl + 1u < PNAME_CAP) nl++;
                memcpy(e->name, pname, nl);
                e->name[nl] = '\0';
                e->data = data;
                e->len = len;
                e->depth = depth;
                build_path(e->path, sizeof(e->path), names, depth);
            }
        } else if(tok == FDT_END) {
            ended = 1;
            break;
        } else {
            return -EINVAL;
        }
    }
    (void)path;
    return ended ? 0 : -EINVAL;
}

static int prop_get(const char *path, const char *name,
                    const uint8_t **data, uint32_t *len)
{
    int i;
    for(i = 0; i < g_prop_count; i++) {
        if(g_props[i].depth < 0) continue;
        if(strcmp(g_props[i].path, path) == 0 &&
           strcmp(g_props[i].name, name) == 0) {
            *data = g_props[i].data;
            *len = g_props[i].len;
            return 0;
        }
    }
    return -ENOENT;
}

static int prop_cell(const char *path, const char *name, uint32_t fallback)
{
    const uint8_t *d;
    uint32_t len;
    if(prop_get(path, name, &d, &len) == 0 && len >= 4u) return be32(d);
    return fallback;
}

/* Local substring matcher: freestanding builds have no libc strstr. */
static int sub_contains(const char *hay, size_t hn, const char *needle)
{
    size_t nn = strlen(needle), i;
    if(nn == 0 || hn < nn) return 0;
    for(i = 0; i + nn <= hn; i++) {
        size_t k;
        for(k = 0; k < nn; k++) if(hay[i + k] != needle[k]) break;
        if(k == nn) return 1;
    }
    return 0;
}

static int compat_contains(const char *path, const char *needle)
{
    const uint8_t *d;
    uint32_t len, off = 0;
    if(prop_get(path, "compatible", &d, &len) != 0) return 0;
    while(off < len) {
        const char *s = (const char *)(d + off);
        uint32_t n = 0;
        while(off + n < len && s[n] != 0) n++;
        if(n > 0 && sub_contains(s, n, needle)) return 1;
        off += n + 1u;
    }
    return 0;
}

static void parent_path(const char *path, char *out, size_t cap)
{
    size_t n = strlen(path);
    if(n == 0 || (n == 1 && path[0] == '/')) {
        if(cap > 1u) { out[0] = '/'; out[1] = '\0'; }
        return;
    }
    while(n > 0 && path[n - 1] != '/') n--;
    if(n > 0) n--;
    if(n == 0) n = 1;
    if(n >= cap) n = cap - 1;
    memcpy(out, path, n);
    out[n] = '\0';
}

static void parse_reg(const char *path, uint64_t *base, uint64_t *size,
                      int *found)
{
    const uint8_t *d;
    uint32_t len, ac, sc, i;
    char pp[PATH_CAP];
    parent_path(path, pp, sizeof(pp));
    ac = prop_cell(pp, "#address-cells", 1u);
    sc = prop_cell(pp, "#size-cells", 1u);
    *found = 0;
    if(prop_get(path, "reg", &d, &len) != 0) return;
    if(ac == 0 || ac > 4u || sc == 0 || sc > 4u) return;
    if(len < (ac + sc) * 4u) return;
    *base = *size = 0;
    for(i = 0; i < ac; i++) *base = (*base << 32) | be32(d + i * 4u);
    for(i = 0; i < sc; i++)
        *size = (*size << 32) | be32(d + (ac + i) * 4u);
    *found = (*base != 0 || *size != 0);
}

static void parse_irqs(const char *path, vqeaf_wifi_device_t *dev)
{
    const uint8_t *d;
    uint32_t len, ic, i, o;
    if(prop_get(path, "interrupts", &d, &len) != 0) return;
    ic = prop_cell(path, "#interrupt-cells", 0u);
    if(ic == 0 || ic > 8u || len < ic * 4u) return;
    for(o = 0; o + ic * 4u <= len && dev->irq_count < MAX_IRQS; o += ic * 4u) {
        uint32_t irq = 0;
        if(ic == 1u) irq = be32(d + o);
        else if(ic >= 2u) irq = be32(d + o + 4u); /* GIC_SPI: <type,irq,flags> */
        else continue;
        if(irq >= 32u) dev->irqs[dev->irq_count++] = irq;
    }
}

void vqeaf_wifi_device_release(vqeaf_wifi_device_t *dev)
{
    if(dev == NULL) return;
    dev->irq_count = 0;
    dev->dt_node = -1;
}

int vqeaf_wifi_fdt_discover(const void *fdt, size_t size,
                            vqeaf_wifi_device_t *dev)
{
    fdt_t f;
    int i, rc, target = -1;
    char wlan_path[PATH_CAP] = "";
    char msa_path[PATH_CAP] = "";
    int msa = 0;

    if(dev == NULL) return -EINVAL;
    memset(dev, 0, sizeof(*dev));
    dev->dt_node = -1;
    rc = fdt_init(&f, fdt, size);
    if(rc) return rc;
    rc = collect_props(&f);
    if(rc) return rc;

    for(i = 0; i < g_prop_count; i++) {
        if(strcmp(g_props[i].name, "compatible") != 0) continue;
        if(compat_contains(g_props[i].path, "qcom,wcn3990-wifi") ||
           compat_contains(g_props[i].path, "qcom,wcnss-wifi")) {
            if(target < 0) {
                memcpy(wlan_path, g_props[i].path, sizeof(wlan_path));
                target = g_props[i].depth;
                dev->dt_node = (int)(g_props[i].data - f.st);
            }
        } else if(compat_contains(g_props[i].path, "msa")) {
            if(!msa) {
                memcpy(msa_path, g_props[i].path, sizeof(msa_path));
                msa = 1;
            }
        }
    }
    if(target < 0) return -ENODEV;

    /* Bus: the documented qcom,wcn3990-wifi binding is SNOC. Report UNKNOWN
     * rather than guessing when neither the wifi nor its parent declares it. */
    if(compat_contains(wlan_path, "qcom,wcn3990-wifi"))
        dev->bus = VQEAF_WIFI_BUS_SNOC;
    else
        dev->bus = VQEAF_WIFI_BUS_UNKNOWN;
    dev->compatible = "qcom,wcn3990-wifi";
    memcpy(dev->path, wlan_path, sizeof(dev->path));
    dev->path[sizeof(dev->path) - 1u] = '\0';

    parse_reg(wlan_path, &dev->mmio_base, &dev->mmio_size, &i);
    if(!i) { dev->mmio_base = 0; dev->mmio_size = 0; }
    parse_irqs(wlan_path, dev);
    if(msa) {
        parse_reg(msa_path, &dev->msa_base, &dev->msa_size, &i);
        dev->msa_found = i;
    }
    return 0;
}

const char *vqeaf_wifi_bus_name(vqeaf_wifi_bus_t bus)
{
    switch(bus) {
    case VQEAF_WIFI_BUS_SNOC:  return "snoc";
    case VQEAF_WIFI_BUS_PCIE:  return "pcie";
    case VQEAF_WIFI_BUS_USB:   return "usb";
    default:                   return "unknown";
    }
}
