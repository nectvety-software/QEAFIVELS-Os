#include "mre_fdt.h"
#include <string.h>

#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE   2u
#define FDT_PROP       3u
#define FDT_NOP        4u
#define FDT_END        9u
#define FDT_HEADER_SIZE 40u

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint32_t align4(uint32_t v) { return (v + 3u) & ~3u; }
static int range_ok(uint32_t offset, uint32_t length, uint32_t limit)
{
    return offset <= limit && length <= limit - offset;
}
static mre_status_t token_at(const mre_fdt_t *fdt, uint32_t offset,
                             uint32_t *token, uint32_t *next)
{
    const uint8_t *s;
    uint32_t p, len;
    if (fdt == NULL || token == NULL || next == NULL ||
        !range_ok(offset, 4u, fdt->struct_size)) return MRE_E_INVALID;
    s = fdt->blob + fdt->struct_offset;
    *token = be32(s + offset);
    p = offset + 4u;
    if (*token == FDT_BEGIN_NODE) {
        while (p < fdt->struct_size && s[p] != 0u) ++p;
        if (p >= fdt->struct_size) return MRE_E_INVALID;
        *next = align4(p + 1u);
    } else if (*token == FDT_PROP) {
        if (!range_ok(p, 8u, fdt->struct_size)) return MRE_E_INVALID;
        len = be32(s + p);
        p += 8u;
        if (!range_ok(p, len, fdt->struct_size)) return MRE_E_INVALID;
        *next = align4(p + len);
    } else if (*token == FDT_END_NODE || *token == FDT_NOP || *token == FDT_END) {
        *next = p;
    } else {
        return MRE_E_INVALID;
    }
    if (*next > fdt->struct_size) return MRE_E_INVALID;
    return MRE_OK;
}

mre_status_t mre_fdt_open(mre_fdt_t *fdt, const void *blob, size_t available)
{
    const uint8_t *b = (const uint8_t *)blob;
    uint32_t total, ro, so, stro, ss, strs, version, last;
    if (fdt == NULL || blob == NULL || available < FDT_HEADER_SIZE) return MRE_E_INVALID;
    if (be32(b) != MRE_FDT_MAGIC) return MRE_E_INVALID;
    total = be32(b + 4); so = be32(b + 8); stro = be32(b + 12); ro = be32(b + 16);
    version = be32(b + 20); last = be32(b + 24);
    strs = be32(b + 32); ss = be32(b + 36);
    if (total < FDT_HEADER_SIZE || total > available || version < 17u || last > version)
        return MRE_E_INVALID;
    if (!range_ok(so, ss, total) || !range_ok(stro, strs, total) ||
        so < FDT_HEADER_SIZE || stro < FDT_HEADER_SIZE ||
        ro < FDT_HEADER_SIZE || ro >= so || (ro & 7u) != 0u)
        return MRE_E_INVALID;
    fdt->blob = b; fdt->total_size = total; fdt->reserve_offset = ro; fdt->struct_offset = so;
    fdt->strings_offset = stro; fdt->struct_size = ss; fdt->strings_size = strs;
    return MRE_OK;
}

mre_status_t mre_fdt_get_property(const mre_fdt_t *fdt, mre_fdt_node_t node,
                                  const char *name, const void **data, uint32_t *length)
{
    const uint8_t *s, *strings;
    uint32_t p, token, next, depth = 0;
    if (fdt == NULL || name == NULL || data == NULL || length == NULL ||
        node.offset >= fdt->struct_size) return MRE_E_INVALID;
    s = fdt->blob + fdt->struct_offset;
    strings = fdt->blob + fdt->strings_offset;
    if (token_at(fdt, node.offset, &token, &next) != MRE_OK || token != FDT_BEGIN_NODE)
        return MRE_E_INVALID;
    p = next;
    while (p < fdt->struct_size) {
        uint32_t nameoff, len;
        const char *prop_name;
        if (token_at(fdt, p, &token, &next) != MRE_OK) return MRE_E_INVALID;
        if (token == FDT_END_NODE || token == FDT_END) break;
        if (token == FDT_BEGIN_NODE) { if (depth == 0u) break; ++depth; }
        else if (token == FDT_END_NODE) { if (depth != 0u) --depth; }
        else if (token == FDT_PROP && depth == 0u) {
            len = be32(s + p + 4u);
            nameoff = be32(s + p + 8u);
            if (nameoff >= fdt->strings_size) return MRE_E_INVALID;
            prop_name = (const char *)(strings + nameoff);
            if (memchr(prop_name, 0, fdt->strings_size - nameoff) == NULL)
                return MRE_E_INVALID;
            if (strcmp(prop_name, name) == 0) {
                *data = s + p + 12u; *length = len; return MRE_OK;
            }
        }
        p = next;
    }
    return MRE_E_NOT_FOUND;
}

mre_status_t mre_fdt_get_u32(const mre_fdt_t *fdt, mre_fdt_node_t node,
                             const char *name, uint32_t *out)
{
    const void *data; uint32_t length;
    mre_status_t st;
    if (out == NULL) return MRE_E_INVALID;
    st = mre_fdt_get_property(fdt, node, name, &data, &length);
    if (st != MRE_OK) return st;
    if (length != 4u) return MRE_E_INVALID;
    *out = be32((const uint8_t *)data);
    return MRE_OK;
}

mre_status_t mre_fdt_find_compatible(const mre_fdt_t *fdt, const char *compatible,
                                     uint32_t *cursor, mre_fdt_node_t *out)
{
    uint32_t p = 0, token, next, depth = 0, start;
    if (fdt == NULL || compatible == NULL || cursor == NULL || out == NULL ||
        *cursor > fdt->struct_size) return MRE_E_INVALID;
    start = *cursor;
    while (p < fdt->struct_size) {
        if (token_at(fdt, p, &token, &next) != MRE_OK) return MRE_E_INVALID;
        if (token == FDT_BEGIN_NODE) {
            mre_fdt_node_t node;
            const void *data; uint32_t len, i = 0;
            node.offset = p; node.depth = depth;
            if (p >= start && mre_fdt_get_property(fdt, node, "compatible", &data, &len) == MRE_OK) {
                const char *list = (const char *)data;
                while (i < len) {
                    size_t n = 0;
                    while (i + n < len && list[i + n] != 0) ++n;
                    if (i + n >= len) return MRE_E_INVALID;
                    if (strlen(compatible) == n && memcmp(list + i, compatible, n) == 0) {
                        *out = node; *cursor = next; return MRE_OK;
                    }
                    i += (uint32_t)n + 1u;
                }
            }
            ++depth;
        } else if (token == FDT_END_NODE) {
            if (depth == 0u) return MRE_E_INVALID;
            --depth;
        } else if (token == FDT_END) break;
        p = next;
    }
    *cursor = fdt->struct_size;
    return MRE_E_NOT_FOUND;
}

static mre_status_t find_parent(const mre_fdt_t *fdt, uint32_t target,
                                mre_fdt_node_t *parent)
{
    uint32_t stack[64], depth = 0, p = 0, token, next;
    while (p < fdt->struct_size) {
        if (token_at(fdt, p, &token, &next) != MRE_OK) return MRE_E_INVALID;
        if (token == FDT_BEGIN_NODE) {
            if (p == target) {
                if (depth == 0u) return MRE_E_NOT_FOUND;
                parent->offset = stack[depth - 1u]; parent->depth = depth - 1u;
                return MRE_OK;
            }
            if (depth >= MRE_ARRAY_SIZE(stack)) return MRE_E_INVALID;
            stack[depth++] = p;
        } else if (token == FDT_END_NODE) {
            if (depth == 0u) return MRE_E_INVALID;
            --depth;
        } else if (token == FDT_END) break;
        p = next;
    }
    return MRE_E_NOT_FOUND;
}
mre_status_t mre_fdt_get_parent(const mre_fdt_t *fdt, mre_fdt_node_t node,
                                mre_fdt_node_t *parent)
{
    if (fdt == NULL || parent == NULL) return MRE_E_INVALID;
    return find_parent(fdt, node.offset, parent);
}

mre_status_t mre_fdt_find_phandle(const mre_fdt_t *fdt, uint32_t wanted,
                                  mre_fdt_node_t *out)
{
    uint32_t p = 0u, token, next, depth = 0u;
    if (fdt == NULL || out == NULL || wanted == 0u) return MRE_E_INVALID;
    while (p < fdt->struct_size) {
        if (token_at(fdt, p, &token, &next) != MRE_OK) return MRE_E_INVALID;
        if (token == FDT_BEGIN_NODE) {
            mre_fdt_node_t node;
            uint32_t value;
            node.offset = p;
            node.depth = depth;
            if (mre_fdt_get_u32(fdt, node, "phandle", &value) == MRE_OK &&
                value == wanted) {
                *out = node;
                return MRE_OK;
            }
            if (mre_fdt_get_u32(fdt, node, "linux,phandle", &value) == MRE_OK &&
                value == wanted) {
                *out = node;
                return MRE_OK;
            }
            ++depth;
        } else if (token == FDT_END_NODE) {
            if (depth == 0u) return MRE_E_INVALID;
            --depth;
        } else if (token == FDT_END) {
            break;
        }
        p = next;
    }
    return MRE_E_NOT_FOUND;
}

mre_status_t mre_fdt_get_interrupt_cells(const mre_fdt_t *fdt,
                                         mre_fdt_node_t node, uint32_t *out)
{
    mre_fdt_node_t current = node;
    uint32_t depth;
    if (fdt == NULL || out == NULL) return MRE_E_INVALID;
    for (depth = 0u; depth < 64u; ++depth) {
        const void *data;
        uint32_t length, phandle, cells;
        mre_status_t st = mre_fdt_get_property(
            fdt, current, "interrupt-parent", &data, &length);
        if (st == MRE_OK) {
            mre_fdt_node_t controller;
            if (length != 4u) return MRE_E_INVALID;
            phandle = be32((const uint8_t *)data);
            st = mre_fdt_find_phandle(fdt, phandle, &controller);
            if (st != MRE_OK) return st;
            st = mre_fdt_get_u32(fdt, controller, "#interrupt-cells", &cells);
            if (st != MRE_OK || cells == 0u || cells > 4u)
                return MRE_E_INVALID;
            *out = cells;
            return MRE_OK;
        }
        if (st != MRE_E_NOT_FOUND) return st;
        if (current.depth == 0u) break;
        st = find_parent(fdt, current.offset, &current);
        if (st != MRE_OK) return st;
    }
    return MRE_E_NOT_FOUND;
}

static mre_status_t node_cells(const mre_fdt_t *fdt, mre_fdt_node_t node,
                               const char *name, uint32_t fallback, uint32_t *out)
{
    mre_status_t st = mre_fdt_get_u32(fdt, node, name, out);
    if (st == MRE_E_NOT_FOUND) { *out = fallback; return MRE_OK; }
    if (st != MRE_OK || *out == 0u || *out > MRE_FDT_MAX_CELLS) return MRE_E_INVALID;
    return MRE_OK;
}
static uint64_t cells_to_u64(const uint8_t *p, uint32_t cells)
{
    uint64_t v = 0; uint32_t i;
    for (i = 0; i < cells; ++i) v = (v << 32) | be32(p + i * 4u);
    return v;
}
mre_status_t mre_fdt_get_reg(const mre_fdt_t *fdt, mre_fdt_node_t node,
                             uint32_t index, mre_fdt_reg_t *out)
{
    mre_fdt_node_t parent;
    const void *data; uint32_t len, ac, sc, stride, off;
    mre_status_t st;
    if (out == NULL) return MRE_E_INVALID;
    st = find_parent(fdt, node.offset, &parent); if (st != MRE_OK) return st;
    st = node_cells(fdt, parent, "#address-cells", 2u, &ac); if (st != MRE_OK) return st;
    st = node_cells(fdt, parent, "#size-cells", 1u, &sc); if (st != MRE_OK) return st;
    st = mre_fdt_get_property(fdt, node, "reg", &data, &len); if (st != MRE_OK) return st;
    stride = (ac + sc) * 4u;
    if (stride == 0u || len % stride != 0u || index >= len / stride) return MRE_E_INVALID;
    off = index * stride;
    out->address = cells_to_u64((const uint8_t *)data + off, ac);
    out->size = cells_to_u64((const uint8_t *)data + off + ac * 4u, sc);
    return MRE_OK;
}

mre_status_t mre_fdt_find_node_name(const mre_fdt_t *fdt,const char *name,
                                    uint32_t *cursor,mre_fdt_node_t *out)
{
    const uint8_t *s;
    uint32_t p=0,token,next,depth=0,start;
    if(fdt==NULL || name==NULL || cursor==NULL || out==NULL ||
       *cursor>fdt->struct_size) return MRE_E_INVALID;
    s=fdt->blob+fdt->struct_offset; start=*cursor;
    while(p<fdt->struct_size) {
        if(token_at(fdt,p,&token,&next)!=MRE_OK) return MRE_E_INVALID;
        if(token==FDT_BEGIN_NODE) {
            if(p>=start) {
                const char *node_name=(const char *)(s+p+4u);
                size_t n=0;
                while(p+4u+n<fdt->struct_size && node_name[n]!=0) ++n;
                if(p+4u+n>=fdt->struct_size) return MRE_E_INVALID;
                if(strlen(name)==n && memcmp(name,node_name,n)==0) {
                    out->offset=p; out->depth=depth; *cursor=next; return MRE_OK;
                }
            }
            ++depth;
        } else if(token==FDT_END_NODE) {
            if(depth==0u) return MRE_E_INVALID;
            --depth;
        } else if(token==FDT_END) break;
        p=next;
    }
    *cursor=fdt->struct_size; return MRE_E_NOT_FOUND;
}

mre_status_t mre_fdt_find_property_string(const mre_fdt_t *fdt,const char *property,
                                           const char *value,uint32_t *cursor,
                                           mre_fdt_node_t *out)
{
    uint32_t p=0,token,next,depth=0,start;
    if(fdt==NULL || property==NULL || value==NULL || cursor==NULL || out==NULL ||
       *cursor>fdt->struct_size) return MRE_E_INVALID;
    start=*cursor;
    while(p<fdt->struct_size) {
        if(token_at(fdt,p,&token,&next)!=MRE_OK) return MRE_E_INVALID;
        if(token==FDT_BEGIN_NODE) {
            mre_fdt_node_t node; const void *data; uint32_t len;
            node.offset=p; node.depth=depth;
            if(p>=start && mre_fdt_get_property(fdt,node,property,&data,&len)==MRE_OK) {
                size_t n=strlen(value);
                if(len==n+1u && memcmp(data,value,n)==0 && ((const char *)data)[n]==0) {
                    *out=node; *cursor=next; return MRE_OK;
                }
            }
            ++depth;
        } else if(token==FDT_END_NODE) {
            if(depth==0u) return MRE_E_INVALID;
            --depth;
        } else if(token==FDT_END) break;
        p=next;
    }
    *cursor=fdt->struct_size; return MRE_E_NOT_FOUND;
}

mre_status_t mre_fdt_find_child(const mre_fdt_t *fdt,mre_fdt_node_t parent,
                                uint32_t *cursor,mre_fdt_node_t *out)
{
    uint32_t p,token,next,depth=0,start;
    if(fdt==NULL || cursor==NULL || out==NULL || parent.offset>=fdt->struct_size ||
       *cursor>fdt->struct_size) return MRE_E_INVALID;
    if(token_at(fdt,parent.offset,&token,&p)!=MRE_OK || token!=FDT_BEGIN_NODE)
        return MRE_E_INVALID;
    start=*cursor;
    while(p<fdt->struct_size) {
        if(token_at(fdt,p,&token,&next)!=MRE_OK) return MRE_E_INVALID;
        if(token==FDT_BEGIN_NODE) {
            if(depth==0u && p>=start) {
                out->offset=p; out->depth=parent.depth+1u; *cursor=next; return MRE_OK;
            }
            ++depth;
        } else if(token==FDT_END_NODE) {
            if(depth==0u) { *cursor=fdt->struct_size; return MRE_E_NOT_FOUND; }
            --depth;
        } else if(token==FDT_END) break;
        p=next;
    }
    *cursor=fdt->struct_size; return MRE_E_NOT_FOUND;
}

static uint64_t be64(const uint8_t *p)
{
    return ((uint64_t)be32(p)<<32)|(uint64_t)be32(p+4u);
}
mre_status_t mre_fdt_get_reservations(const mre_fdt_t *fdt,mre_fdt_reg_t *out,
                                      size_t capacity,size_t *count)
{
    uint32_t p;
    size_t n=0;
    if(fdt==NULL || count==NULL || (capacity!=0u && out==NULL)) return MRE_E_INVALID;
    p=fdt->reserve_offset;
    while(range_ok(p,16u,fdt->total_size) && p+16u<=fdt->struct_offset) {
        uint64_t address=be64(fdt->blob+p),size=be64(fdt->blob+p+8u);
        p+=16u;
        if(address==0u && size==0u) { *count=n; return MRE_OK; }
        if(size==0u || address>UINT64_MAX-size) return MRE_E_INVALID;
        if(n>=capacity) { *count=n; return MRE_E_NOMEM; }
        out[n].address=address;out[n].size=size;++n;
    }
    *count=n;return MRE_E_INVALID;
}
