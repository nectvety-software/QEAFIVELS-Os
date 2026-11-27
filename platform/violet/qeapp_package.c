/* SPDX-License-Identifier: MIT */
#include <vqeaf/qeapp_package.h>

#include <stdio.h>
#include <string.h>

#include "ui/vqeaf_qeapp.h"

static void result_init(vqeaf_qeapp_package_result_t *result)
{
    if(result == NULL) return;
    memset(result, 0, sizeof(*result));
    result->struct_size = sizeof(*result);
    result->api_version = VQEAF_QEAPP_PACKAGE_API_VERSION;
    result->status = VQEAF_QEAPP_PACKAGE_INTERNAL_ERROR;
    result->persisted = -1;
    result->updated = -1;
}

static int finish(vqeaf_qeapp_package_result_t *result, int status,
                  const char *message, int persisted, int updated)
{
    if(result != NULL) {
        result->status = status;
        result->persisted = persisted;
        result->updated = updated;
        snprintf(result->message, sizeof(result->message), "%s",
                 message != NULL ? message : "");
    }
    return status;
}

static int fail(vqeaf_qeapp_package_result_t *result, int status,
                const char *message)
{
    return finish(result, status, message, -1, -1);
}

static void info_init(vqeaf_qeapp_package_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->struct_size = sizeof(*info);
    info->api_version = VQEAF_QEAPP_PACKAGE_API_VERSION;
}

static void copy_info(vqeaf_qeapp_package_info_t *info,
                      const vqeaf_qeapp_preview_t *preview,
                      const vqeaf_qeapp_t *app)
{
    const char *id = app != NULL ? app->id : preview->id;
    const char *name = app != NULL ? app->name : preview->name;
    const char *version = app != NULL ? app->version : preview->version;
    const char *message = app != NULL ? app->message : preview->message;
    const uint8_t *icon = app != NULL ? app->icon : preview->icon;
    int native = app != NULL ? app->native : preview->native;
    int abi = app != NULL ? app->abi_version : preview->abi_version;
    int has_icon = app != NULL ? app->has_icon : preview->has_icon;

    info_init(info);
    snprintf(info->id, sizeof(info->id), "%s", id);
    snprintf(info->name, sizeof(info->name), "%s", name);
    snprintf(info->version, sizeof(info->version), "%s", version);
    snprintf(info->message, sizeof(info->message), "%s", message);
    info->native = native;
    info->abi_version = abi;
    info->has_icon = has_icon;
    if(has_icon) memcpy(info->icon, icon, sizeof(info->icon));
    if(app == NULL) memcpy(info->package_hash, preview->package_hash,
                           sizeof(info->package_hash));
    if(app != NULL) {
        info->permissions = app->permissions;
        info->granted_permissions = app->granted_permissions;
        snprintf(info->package_file, sizeof(info->package_file), "%s",
                 app->package_file);
    }
    else {
        info->permissions = preview->permissions;
    }
}

uint32_t vqeaf_qeapp_package_api_version(void)
{
    return VQEAF_QEAPP_PACKAGE_API_VERSION;
}

int vqeaf_qeapp_package_preview(const char *path,
                                vqeaf_qeapp_package_info_t *info,
                                vqeaf_qeapp_package_result_t *result)
{
    if(info != NULL) info_init(info);
#ifndef __linux__
    (void)path;
    result_init(result);
    return fail(result, VQEAF_QEAPP_PACKAGE_UNSUPPORTED,
                "Host renderer has no QEAPP package backend.");
#else
    vqeaf_qeapp_preview_t preview;
    char error[VQEAF_QEAPP_PACKAGE_RESULT_MESSAGE] = "";

    result_init(result);
    if(path == NULL || path[0] == '\0' || info == NULL)
        return fail(result, VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT,
                    "Package path and info output are required.");
    if(vqeaf_qeapp_preview(path, &preview, error, sizeof(error)) != 0) {
        int status = vqeaf_qeapp_last_status();
        if(status <= VQEAF_QEAPP_PACKAGE_OK)
            status = VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE;
        return fail(result, status, error[0] ? error :
                    "Package preview failed.");
    }
    copy_info(info, &preview, NULL);
    return finish(result, VQEAF_QEAPP_PACKAGE_OK, "Package verified.", -1, -1);
#endif
}

static int install_common(const char *path, const uint8_t *expected_hash,
                          vqeaf_qeapp_package_result_t *result)
{
#ifndef __linux__
    (void)path;
    (void)expected_hash;
    return fail(result, VQEAF_QEAPP_PACKAGE_UNSUPPORTED,
                "Host renderer cannot install QEAPP packages.");
#else
    char error[VQEAF_QEAPP_PACKAGE_RESULT_MESSAGE] = "";
    size_t before;
    int status;
    int persisted;
    int updated;

    if(path == NULL || path[0] == '\0')
        return fail(result, VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT,
                    "Package path is required.");
    int install_rc;

    before = vqeaf_qeapp_package_count();
    install_rc = expected_hash != NULL ?
        vqeaf_qeapp_install_verified(path, expected_hash, error,
                                     sizeof(error)) :
        vqeaf_qeapp_install(path, error, sizeof(error));
    if(install_rc != 0) {
        status = vqeaf_qeapp_last_status();
        if(status <= VQEAF_QEAPP_PACKAGE_OK)
            status = VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE;
        return fail(result, status, error[0] ? error :
                    "Package installation failed.");
    }
    persisted = vqeaf_qeapp_persisted();
    updated = vqeaf_qeapp_package_count() == before;
    status = persisted ? VQEAF_QEAPP_PACKAGE_OK :
                         VQEAF_QEAPP_PACKAGE_OK_SESSION_ONLY;
    if(status == VQEAF_QEAPP_PACKAGE_OK_SESSION_ONLY)
        snprintf(error, sizeof(error),
                 "Installed for this boot; p77 catalog persistence failed.");
    else
        snprintf(error, sizeof(error), "Installed and saved to p77.");
    return finish(result, status, error, persisted, updated);
#endif
}

int vqeaf_qeapp_package_install(const char *path,
                                vqeaf_qeapp_package_result_t *result)
{
    result_init(result);
    return install_common(path, NULL, result);
}

int vqeaf_qeapp_package_install_verified(const char *path,
                                         const uint8_t *expected_hash,
                                         vqeaf_qeapp_package_result_t *result)
{
    result_init(result);
    if(expected_hash == NULL)
        return fail(result, VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT,
                    "A verified package hash is required.");
    return install_common(path, expected_hash, result);
}

int32_t vqeaf_qeapp_package_find(const char *id)
{
    if(id == NULL || id[0] == '\0') return -1;
    for(size_t i = 0; i < vqeaf_qeapp_package_count(); i++) {
        const vqeaf_qeapp_t *app = vqeaf_qeapp_at((int)i);
        if(app != NULL && strcmp(app->id, id) == 0) return (int32_t)i;
    }
    return -1;
}

size_t vqeaf_qeapp_package_count(void)
{
    int count = vqeaf_qeapp_count();
    return count > 0 ? (size_t)count : 0u;
}

int vqeaf_qeapp_package_get(size_t index,
                            vqeaf_qeapp_package_info_t *info,
                            vqeaf_qeapp_package_result_t *result)
{
    const vqeaf_qeapp_t *app;

    result_init(result);
    if(info == NULL)
        return fail(result, VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT,
                    "Package info output is required.");
    if(index >= vqeaf_qeapp_package_count())
        return fail(result, VQEAF_QEAPP_PACKAGE_NOT_FOUND,
                    "Installed package index is out of range.");
    app = vqeaf_qeapp_at((int)index);
    if(app == NULL)
        return fail(result, VQEAF_QEAPP_PACKAGE_INTERNAL_ERROR,
                    "Installed package registry changed during the read.");
    copy_info(info, NULL, app);
    return finish(result, VQEAF_QEAPP_PACKAGE_OK, "Installed package read.",
                  -1, -1);
}

int vqeaf_qeapp_package_uninstall(const char *id,
                                  vqeaf_qeapp_package_result_t *result)
{
#ifndef __linux__
    (void)id;
    result_init(result);
    return fail(result, VQEAF_QEAPP_PACKAGE_UNSUPPORTED,
                "Host renderer cannot uninstall QEAPP packages.");
#else
    int32_t found;
    char error[VQEAF_QEAPP_PACKAGE_RESULT_MESSAGE] = "";
    int persisted;
    int status;

    result_init(result);
    if(id == NULL || id[0] == '\0')
        return fail(result, VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT,
                    "Package id is required.");
    found = vqeaf_qeapp_package_find(id);
    if(found < 0)
        return fail(result, VQEAF_QEAPP_PACKAGE_NOT_FOUND,
                    "Package is not installed.");
    if(vqeaf_qeapp_uninstall((int)found) != 0) {
        status = vqeaf_qeapp_last_status();
        if(status <= VQEAF_QEAPP_PACKAGE_OK)
            status = VQEAF_QEAPP_PACKAGE_INTERNAL_ERROR;
        return fail(result, status, error[0] ? error :
                    "Package uninstall failed.");
    }
    persisted = vqeaf_qeapp_persisted();
    status = persisted ? VQEAF_QEAPP_PACKAGE_OK :
                         VQEAF_QEAPP_PACKAGE_OK_SESSION_ONLY;
    snprintf(error, sizeof(error), persisted ?
             "Uninstalled and saved to p77." :
             "Uninstalled for this boot; p77 catalog persistence failed.");
    return finish(result, status, error, persisted, -1);
#endif
}

const char *vqeaf_qeapp_package_status_name(int status)
{
    switch(status) {
    case VQEAF_QEAPP_PACKAGE_OK: return "OK";
    case VQEAF_QEAPP_PACKAGE_OK_SESSION_ONLY: return "OK_SESSION_ONLY";
    case VQEAF_QEAPP_PACKAGE_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case VQEAF_QEAPP_PACKAGE_IO_ERROR: return "IO_ERROR";
    case VQEAF_QEAPP_PACKAGE_INVALID_PACKAGE: return "INVALID_PACKAGE";
    case VQEAF_QEAPP_PACKAGE_SIGNATURE_ERROR: return "SIGNATURE_ERROR";
    case VQEAF_QEAPP_PACKAGE_UNSUPPORTED: return "UNSUPPORTED";
    case VQEAF_QEAPP_PACKAGE_CAPACITY: return "CAPACITY";
    case VQEAF_QEAPP_PACKAGE_NOT_FOUND: return "NOT_FOUND";
    case VQEAF_QEAPP_PACKAGE_API_VERSION_ERROR: return "API_VERSION_ERROR";
    case VQEAF_QEAPP_PACKAGE_INTERNAL_ERROR: return "INTERNAL_ERROR";
    default: return "UNKNOWN";
    }
}
