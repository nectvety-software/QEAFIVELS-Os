#ifndef MRE_FDT_H
#define MRE_FDT_H
#include "mre_types.h"
#define MRE_FDT_MAGIC 0xD00DFEEDu
#define MRE_FDT_MAX_CELLS 2u
typedef struct mre_fdt {
    const uint8_t *blob;
    uint32_t total_size, reserve_offset, struct_offset, strings_offset, struct_size, strings_size;
} mre_fdt_t;
typedef struct mre_fdt_node {
    uint32_t offset;
    uint32_t depth;
} mre_fdt_node_t;
typedef struct mre_fdt_reg {
    uint64_t address;
    uint64_t size;
} mre_fdt_reg_t;
mre_status_t mre_fdt_open(mre_fdt_t *fdt, const void *blob, size_t available);
mre_status_t mre_fdt_find_compatible(const mre_fdt_t *fdt, const char *compatible,
                                     uint32_t *cursor, mre_fdt_node_t *out);
mre_status_t mre_fdt_get_property(const mre_fdt_t *fdt, mre_fdt_node_t node,
                                  const char *name, const void **data, uint32_t *length);
mre_status_t mre_fdt_get_reg(const mre_fdt_t *fdt, mre_fdt_node_t node,
                             uint32_t index, mre_fdt_reg_t *out);
mre_status_t mre_fdt_get_reservations(const mre_fdt_t *fdt, mre_fdt_reg_t *out,
                                      size_t capacity, size_t *count);
mre_status_t mre_fdt_get_u32(const mre_fdt_t *fdt, mre_fdt_node_t node,
                             const char *name, uint32_t *out);
mre_status_t mre_fdt_get_parent(const mre_fdt_t *fdt, mre_fdt_node_t node,
                                mre_fdt_node_t *parent);
mre_status_t mre_fdt_find_phandle(const mre_fdt_t *fdt, uint32_t phandle,
                                  mre_fdt_node_t *out);
mre_status_t mre_fdt_get_interrupt_cells(const mre_fdt_t *fdt,
                                         mre_fdt_node_t node, uint32_t *out);
mre_status_t mre_fdt_find_node_name(const mre_fdt_t *fdt, const char *name,
                                    uint32_t *cursor, mre_fdt_node_t *out);
mre_status_t mre_fdt_find_property_string(const mre_fdt_t *fdt, const char *property,
                                           const char *value, uint32_t *cursor,
                                           mre_fdt_node_t *out);
mre_status_t mre_fdt_find_child(const mre_fdt_t *fdt, mre_fdt_node_t parent,
                                uint32_t *cursor, mre_fdt_node_t *out);
mre_status_t mre_fdt_find_descendant_property(const mre_fdt_t *fdt,
                                               mre_fdt_node_t parent,
                                               const char *property,
                                               uint32_t *cursor,
                                               mre_fdt_node_t *out);
#endif
