/* SPDX-License-Identifier: MIT */
/* framework/security/qeaf_broker.c - QEAPP permission broker + quotas. */
#include "qeaf_broker.h"

#include <stdio.h>
#include <string.h>

static void audit(qeaf_broker_t *b, const char *msg)
{
    if(b->audit_count < QEAF_BROKER_MAX_AUDIT)
        snprintf(b->audit[b->audit_count++], QEAF_BROKER_AUDIT_LEN, "%s", msg);
}

static void deny(qeaf_broker_t *b, const char *msg)
{
    b->deny_count++;
    audit(b, msg);
}

void qeaf_broker_init(qeaf_broker_t *b, uint32_t declared, const char *scope,
                      size_t memory_budget, uint32_t call_budget_ms)
{
    memset(b, 0, sizeof(*b));
    b->declared = declared;
    b->granted = declared;
    if(scope != NULL)
        snprintf(b->scope, sizeof(b->scope), "%s", scope);
    b->memory_budget = memory_budget ? memory_budget : QEAF_QUOTA_MEMORY_BYTES;
    b->call_budget_ms = call_budget_ms ? call_budget_ms : QEAF_QUOTA_CALL_MS;
}

int qeaf_broker_check(const qeaf_broker_t *b, uint32_t perm)
{
    return (perm != 0u && (b->granted & perm) == perm) ? 1 : 0;
}

int qeaf_broker_request(qeaf_broker_t *b, uint32_t perm)
{
    char msg[QEAF_BROKER_AUDIT_LEN];
    if(perm == 0u) {
        deny(b, "deny request unknown-permission");
        return 0;
    }
    if((b->declared & perm) != perm) {
        snprintf(msg, sizeof(msg), "deny request undeclared 0x%08x", perm);
        deny(b, msg);
        return 0;
    }
    b->granted |= perm;
    snprintf(msg, sizeof(msg), "grant 0x%08x", perm);
    audit(b, msg);
    return 1;
}

void qeaf_broker_revoke(qeaf_broker_t *b, uint32_t perm)
{
    char msg[QEAF_BROKER_AUDIT_LEN];
    b->granted &= ~perm;
    snprintf(msg, sizeof(msg), "revoke 0x%08x", perm);
    audit(b, msg);
}

/* A ".." component anywhere in the path is a traversal attempt. */
static int has_traversal(const char *path)
{
    const char *p = path;
    while(*p) {
        if(p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0'))
            return 1;
        const char *slash = strchr(p, '/');
        if(slash == NULL) break;
        p = slash + 1;
    }
    return 0;
}

static int is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int qeaf_broker_check_path(qeaf_broker_t *b, const char *path, int want_write)
{
    if(path == NULL || path[0] == '\0') {
        deny(b, "deny path empty");
        return 0;
    }
    if(want_write && !qeaf_broker_check(b, QEAF_PERM_STORAGE_WRITE_APP)) {
        deny(b, "deny path write-permission");
        return 0;
    }
    if(path[0] == '/' || path[0] == '\\') {
        deny(b, "deny path absolute");
        return 0;
    }
    if(is_alpha(path[0]) && path[1] == ':') {
        deny(b, "deny path drive");
        return 0;
    }
    if(strchr(path, '\\') != NULL) {
        deny(b, "deny path backslash");
        return 0;
    }
    if(has_traversal(path)) {
        deny(b, "deny path traversal");
        return 0;
    }
    if(b->scope[0] != '\0') {
        size_t n = strlen(b->scope);
        if(strncmp(path, b->scope, n) != 0) {
            deny(b, "deny path out-of-scope");
            return 0;
        }
    }
    return 1;
}

int qeaf_broker_mem_acquire(qeaf_broker_t *b, size_t bytes)
{
    if(bytes > b->memory_budget - b->memory_used) {
        deny(b, "deny memory quota");
        return 0;
    }
    b->memory_used += bytes;
    return 1;
}

void qeaf_broker_mem_release(qeaf_broker_t *b, size_t bytes)
{
    b->memory_used = bytes > b->memory_used ? 0u : b->memory_used - bytes;
}

size_t qeaf_broker_audit_count(const qeaf_broker_t *b)
{
    return b ? b->audit_count : 0u;
}

const char *qeaf_broker_audit_at(const qeaf_broker_t *b, size_t index)
{
    if(b == NULL || index >= b->audit_count) return NULL;
    return b->audit[index];
}

uint32_t qeaf_broker_deny_count(const qeaf_broker_t *b)
{
    return b ? b->deny_count : 0u;
}

size_t qeaf_broker_memory_used(const qeaf_broker_t *b)
{
    return b ? b->memory_used : 0u;
}

uint32_t qeaf_broker_granted(const qeaf_broker_t *b)
{
    return b ? b->granted : 0u;
}