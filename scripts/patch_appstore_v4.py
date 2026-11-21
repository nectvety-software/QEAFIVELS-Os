from pathlib import Path
root=Path(r"D:\QEAFIVELS-Os")
p=root/"platform/violet/appstore.h"
s=p.read_text(encoding="utf-8")
s=s.replace("    size_t app_data_size;\n} appstore_entry_t;", "    size_t app_data_size;\n    /* User-approved permission subset, separate from app data. */\n    uint32_t granted_permissions;\n} appstore_entry_t;")
s=s.replace("int appstore_read_data(const char *name, uint8_t **out, size_t *size);", "int appstore_read_data(const char *name, uint8_t **out, size_t *size);\n/* Reads persisted user grants; legacy v2/v3 records migrate to zero grants. */\nint appstore_read_grants(const char *name, uint32_t *grants);")
p.write_text(s,encoding="utf-8")

p=root/"platform/violet/appstore.c"
s=p.read_text(encoding="utf-8")
s=s.replace('#define APPSTORE_MAGIC "QEAFAPP3"\n#define APPSTORE_VERSION 3u\n/* v2 catalogs (no per-app data) are still read so an upgrade keeps apps. */', '#define APPSTORE_MAGIC "QEAFAPP4"\n#define APPSTORE_VERSION 4u\n#define APPSTORE_MAGIC_V3 "QEAFAPP3"\n#define APPSTORE_VERSION_V3 3u\n/* v2/v3 catalogs remain readable and are migrated on the next save. */')
s=s.replace('    int v2 = memcmp(h->magic, APPSTORE_MAGIC_V2, 8) == 0 &&\n             h->version == APPSTORE_VERSION_V2;\n    if(!v3 && !v2) return 0;', '    int v3_old = memcmp(h->magic, APPSTORE_MAGIC_V3, 8) == 0 &&\n                 h->version == APPSTORE_VERSION_V3;\n    int v2 = memcmp(h->magic, APPSTORE_MAGIC_V2, 8) == 0 &&\n             h->version == APPSTORE_VERSION_V2;\n    if(!v3 && !v3_old && !v2) return 0;')
s=s.replace('        size_t need;\n        int owned = 1;', '        size_t need;\n        int owned = 1;')
s=s.replace('        need = 12 + name_len + package_size + app_data_size;', '        need = 16 + name_len + package_size + app_data_size;')
s=s.replace('        memcpy(payload + used + 8, &(uint32_t){ (uint32_t)app_data_size }, 4);\n        memcpy(payload + used + 12, entries[i].name, name_len);\n        memcpy(payload + used + 12 + name_len, package, package_size);\n        if(app_data_size > 0)\n            memcpy(payload + used + 12 + name_len + package_size,\n                   entries[i].app_data, app_data_size);', '        memcpy(payload + used + 8, &(uint32_t){ (uint32_t)app_data_size }, 4);\n        memcpy(payload + used + 12, &entries[i].granted_permissions, 4);\n        memcpy(payload + used + 16, entries[i].name, name_len);\n        memcpy(payload + used + 16 + name_len, package, package_size);\n        if(app_data_size > 0)\n            memcpy(payload + used + 16 + name_len + package_size,\n                   entries[i].app_data, app_data_size);')
s=s.replace('    uint32_t name_len, pkg_len, data_len;\n    size_t head;', '    uint32_t name_len, pkg_len, data_len, grants;\n    size_t head;')
s=s.replace('    uint32_t name_len, pkg_len, data_len = 0;\n    size_t head = h->version >= APPSTORE_VERSION ? 12u : 8u;', '    uint32_t name_len, pkg_len, data_len = 0, grants = 0;\n    size_t head = h->version >= APPSTORE_VERSION ? 16u\n                : h->version >= APPSTORE_VERSION_V3 ? 12u : 8u;')
s=s.replace('    if(head == 12) memcpy(&data_len, payload + pos + 8, 4);', '    if(head >= 12) memcpy(&data_len, payload + pos + 8, 4);\n    if(head == 16) memcpy(&grants, payload + pos + 12, 4);')
s=s.replace('    rec->data_len = data_len;\n    rec->head = head;', '    rec->data_len = data_len;\n    rec->grants = grants;\n    rec->head = head;')
# append grants reader
s += r'''

int appstore_read_grants(const char *name, uint32_t *grants)
{
    int fd;
    appstore_hdr_t chosen;
    uint8_t *payload = NULL;
    size_t pos = 0;
    const char *base = name;

    if(name == NULL || grants == NULL) return -1;
    *grants = 0u;
    if(name[0] == '/') base = name + 1;
    fd = open(APPSTORE_PATH, O_RDONLY | O_CLOEXEC);
    if(fd < 0) return -1;
    if(load_latest(fd, &chosen, &payload) != 0) {
        close(fd);
        return -1;
    }
    while(pos < chosen.payload_len) {
        appstore_rec_t rec;
        if(record_at(&chosen, payload, pos, &rec) != 0) break;
        if(strlen(base) == rec.name_len &&
           memcmp(payload + pos + rec.head, base, rec.name_len) == 0) {
            *grants = rec.grants;
            close(fd);
            free(payload);
            return 0;
        }
        pos += rec.head + rec.name_len + rec.pkg_len + rec.data_len;
    }
    close(fd);
    free(payload);
    return -1;
}
'''
p.write_text(s,encoding="utf-8")
print("patched appstore v4 schema and legacy readers")
