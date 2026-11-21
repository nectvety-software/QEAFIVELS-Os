/* QEAPP native (v2) validation-only loader. No native code is executed.
 *
 * The header layout mirrors sdk/compiler/qen.py. A module is only accepted
 * when its magic, versions, architecture, section sizes, and payload hash all
 * agree. Nothing is mapped, relocated, or called.
 */
#ifndef VQEAF_QEAPP_NATIVE_H
#define VQEAF_QEAPP_NATIVE_H

#include <stddef.h>
#include <stdint.h>

#define VQEAF_QEN_HEADER_SIZE   64u
#define VQEAF_QEN_MAX_SIZE      (8u * 1024u * 1024u)
#define VQEAF_QEN_MAX_SECTIONS  16u

#define VQEAF_QEN_ARCH_ARM64    1
#define VQEAF_QEN_ARCH_X86_64   2

/* Returns 0 when the module is structurally valid, -1 otherwise.
 * expected_arch may be 0 to accept any supported architecture. */
int vqeaf_qeapp_native_validate_module(const uint8_t *data, size_t size,
                                       int expected_arch);

/* Maps a manifest architecture string ("arm64", "x86_64") to an id, or 0. */
int vqeaf_qeapp_native_arch_id(const char *name);

#endif /* VQEAF_QEAPP_NATIVE_H */
