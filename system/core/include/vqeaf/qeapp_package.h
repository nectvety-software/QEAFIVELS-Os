/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_QEAPP_PACKAGE_H
#define VQEAF_QEAPP_PACKAGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VQEAF_QEAPP_PACKAGE_API_VERSION 1u
#define VQEAF_QEAPP_PACKAGE_NAME 64u
#define VQEAF_QEAPP_PACKAGE_VERSION 24u
#define VQEAF_QEAPP_PACKAGE_MESSAGE 512u
#define VQEAF_QEAPP_PACKAGE_PATH 512u
#define VQEAF_QEAPP_PACKAGE_ICON (64u * 64u * 4u)
#define VQEAF_QEAPP_PACKAGE_SHA256_SIZE 32u
#define VQEAF_QEAPP_PACKAGE_RESULT_MESSAGE 256u

typedef enum {
    VQEAF_QEAPP_PACKAGE_OK = 0,
    VQEAF_QEAPP_PACKAGE_OK_SESSION_ONLY = 1,
    VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT = 2,
    VQEAF_QEAPP_PACKAGE_IO_ERROR = 3,
    VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE = 4,
    VQEAF_QEAPP_PACKAGE_SIGNATURE_ERROR = 5,
    VQEAF_QEAPP_PACKAGE_UNSUPPORTED = 6,
    VQEAF_QEAPP_PACKAGE_CAPACITY = 7,
    VQEAF_QEAPP_PACKAGE_NOT_FOUND = 8,
    VQEAF_QEAPP_PACKAGE_API_VERSION_ERROR = 9,
    VQEAF_QEAPP_PACKAGE_INTERNAL_ERROR = 10
} vqeaf_qeapp_package_status_t;

/* Output record. Callers pass a writable struct; the implementation fills
 * struct_size and api_version and does not read uninitialized caller fields. */
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;
    char id[VQEAF_QEAPP_PACKAGE_NAME];
    char name[VQEAF_QEAPP_PACKAGE_NAME];
    char version[VQEAF_QEAPP_PACKAGE_VERSION];
    char message[VQEAF_QEAPP_PACKAGE_MESSAGE];
    char package_file[VQEAF_QEAPP_PACKAGE_PATH];
    int32_t native;
    int32_t abi_version;
    uint32_t permissions;
    uint32_t granted_permissions;
    int32_t has_icon;
    uint8_t icon[VQEAF_QEAPP_PACKAGE_ICON];
    uint8_t package_hash[VQEAF_QEAPP_PACKAGE_SHA256_SIZE];
} vqeaf_qeapp_package_info_t;

/* Output result. persisted is -1 when not applicable, otherwise 0 or 1.
 * updated is -1 for non-install operations, 0 for a new install and 1 when an
 * existing package id was updated. Both OK and OK_SESSION_ONLY mean the
 * in-memory mutation succeeded; only OK promises a verified durable save. */
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;
    int32_t status;
    int32_t persisted;
    int32_t updated;
    char message[VQEAF_QEAPP_PACKAGE_RESULT_MESSAGE];
} vqeaf_qeapp_package_result_t;

uint32_t vqeaf_qeapp_package_api_version(void);
int vqeaf_qeapp_package_preview(const char *path,
                                vqeaf_qeapp_package_info_t *info,
                                vqeaf_qeapp_package_result_t *result);
int vqeaf_qeapp_package_install(const char *path,
                                vqeaf_qeapp_package_result_t *result);
/* Installs only the exact bytes identified by a preceding preview hash. */
int vqeaf_qeapp_package_install_verified(const char *path,
                                         const uint8_t *expected_hash,
                                         vqeaf_qeapp_package_result_t *result);
/* Returns a zero-based installed index, or -1 for an absent/invalid id. */
int32_t vqeaf_qeapp_package_find(const char *id);
size_t vqeaf_qeapp_package_count(void);
int vqeaf_qeapp_package_get(size_t index,
                            vqeaf_qeapp_package_info_t *info,
                            vqeaf_qeapp_package_result_t *result);
int vqeaf_qeapp_package_uninstall(const char *id,
                                  vqeaf_qeapp_package_result_t *result);
const char *vqeaf_qeapp_package_status_name(int status);

#ifdef __cplusplus
}
#endif

#endif
