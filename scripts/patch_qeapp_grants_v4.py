from pathlib import Path
root=Path(r"D:\QEAFIVELS-Os")
p=root/"platform/violet/ui/vqeaf_qeapp.h"
s=p.read_text(encoding="utf-8")
s=s.replace("    uint32_t permissions;     /* qeaf_permission_t bits from the manifest */", "    uint32_t permissions;     /* immutable requested mask from manifest */\n    uint32_t granted_permissions; /* persisted user grants, subset of requested */",1)
p.write_text(s,encoding="utf-8")

p=root/"platform/violet/ui/vqeaf_qeapp.c"
s=p.read_text(encoding="utf-8")
s=s.replace("        entries[n].app_data = g_apps[i].data;\n        entries[n].app_data_size = g_apps[i].data_size;", "        entries[n].app_data = g_apps[i].data;\n        entries[n].app_data_size = g_apps[i].data_size;\n        entries[n].granted_permissions = g_apps[i].granted_permissions;")
s=s.replace("    if(is_new) {\n        /* New install: take over any data blob already stored for this app", "    if(is_new) {\n        /* New install: take over any data blob already stored for this app")
# inject default safe grants and load prior catalog grants for restore
needle='''        g_apps[slot].data = NULL;
        g_apps[slot].data_size = 0;
        if(appstore_read_data(base, &g_apps[slot].data,
                              &g_apps[slot].data_size) != 0) {'''
repl='''        g_apps[slot].data = NULL;
        g_apps[slot].data_size = 0;
        /* v2/v3 migration returns zero grants. UI display is the only
         * non-sensitive baseline capability; all optional capabilities
         * remain denied until explicitly granted by the user. */
        g_apps[slot].granted_permissions = (1u << 0);
        (void)appstore_read_grants(base, &g_apps[slot].granted_permissions);
        if(appstore_read_data(base, &g_apps[slot].data,
                              &g_apps[slot].data_size) != 0) {'''
if needle not in s: raise SystemExit("restore insertion point missing")
s=s.replace(needle,repl,1)
s=s.replace("    g_apps[slot].native = meta.native;\n    g_apps[slot].permissions = meta.permissions;", "    g_apps[slot].native = meta.native;\n    g_apps[slot].permissions = meta.permissions;\n    /* Updates never silently grant newly requested capabilities. */\n    g_apps[slot].granted_permissions &= meta.permissions;\n    g_apps[slot].granted_permissions |= (meta.permissions & (1u << 0));")
p.write_text(s,encoding="utf-8")
print("patched app records with persisted grant mask and conservative update intersection")
