/* SPDX-License-Identifier: MIT */
/* Offline public advisory metadata for the QEAF security centre. */
#ifndef VQEAF_SECURITY_H
#define VQEAF_SECURITY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VQEAF_ADVISORY_SEARCH_MAX 16

typedef struct {
    const char *id;
    const char *title;
    const char *product;
    const char *published;
    const char *severity;
    const char *source;
    const char *summary;
    const char *remediation;
} vqeaf_advisory_t;

const char *vqeaf_advisory_catalog_revision(void);
size_t vqeaf_advisory_count(void);
const vqeaf_advisory_t *vqeaf_advisory_at(size_t index);
int vqeaf_advisory_search(const char *query, size_t *out, int max);

#ifdef __cplusplus
}
#endif

#endif
