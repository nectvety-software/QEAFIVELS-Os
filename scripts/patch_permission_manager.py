from pathlib import Path
root=Path(r"D:\QEAFIVELS-Os")
# Public API
p=root/"platform/violet/ui/vqeaf_qeapp.h"
s=p.read_text(encoding="utf-8")
needle="int vqeaf_qeapp_uninstall(int index);"
s=s.replace(needle,needle+"\n/* User grants are a subset of manifest-requested permissions and are persisted. */\nint vqeaf_qeapp_set_permission(int index, uint32_t bit, int granted);\nuint32_t vqeaf_qeapp_effective_permissions(int index);")
p.write_text(s,encoding="utf-8")

# Core grant mutation and fail-closed default
p=root/"platform/violet/ui/vqeaf_qeapp.c"
s=p.read_text(encoding="utf-8")
s=s.replace("g_apps[slot].granted_permissions = (1u << 0);","g_apps[slot].granted_permissions = 0u;")
insert=r'''
int vqeaf_qeapp_set_permission(int index, uint32_t bit, int granted)
{
    const uint32_t known = (1u << 5) - 1u;
    if(index < 0 || index >= g_app_count || bit == 0u ||
       (bit & (bit - 1u)) != 0u || (bit & known) == 0u)
        return -1;
    if((g_apps[index].permissions & bit) == 0u)
        return -1; /* never grant a capability absent from the signed manifest */
    if(granted)
        g_apps[index].granted_permissions |= bit;
    else
        g_apps[index].granted_permissions &= ~bit;
#ifdef __linux__
    persist_apps();
#endif
    return 0;
}

uint32_t vqeaf_qeapp_effective_permissions(int index)
{
    if(index < 0 || index >= g_app_count) return 0u;
    return g_apps[index].permissions & g_apps[index].granted_permissions;
}

'''
marker="int vqeaf_qeapp_selected(void)"
if marker not in s: raise SystemExit("core insertion marker missing")
s=s.replace(marker,insert+marker)
p.write_text(s,encoding="utf-8")

# Native code is blocked until privilege separation is implemented.
p=root/"platform/violet/ui/vqeaf_qeapp_device.c"
s=p.read_text(encoding="utf-8")
needle='''    if(package == NULL || size == 0) {
        snprintf(error, error_cap, "device runtime: empty package");
        return -1;
    }
'''
repl=needle+'''    /* SECURITY GATE: fork currently inherits the shell UID (historically
     * UID 0). API-table checks alone cannot constrain direct syscalls.
     * Refuse native execution until a tested unprivileged UID + syscall and
     * filesystem sandbox is installed before entering module code. */
    snprintf(error, error_cap,
             "native QEAPP blocked: privilege separation/seccomp sandbox not ready");
    snprintf(report, report_cap, "blocked: native sandbox unavailable");
    return -1;
'''
if needle not in s: raise SystemExit("native launch guard point missing")
s=s.replace(needle,repl,1)
p.write_text(s,encoding="utf-8")

# Permission Manager UI on app details
p=root/"platform/violet/ui/vqeaf_pages.c"
s=p.read_text(encoding="utf-8")
marker="static void build_qeapp(lv_obj_t *content)"
callback=r'''
static void qeapp_permission_toggle_cb(lv_event_t *event)
{
    uintptr_t packed = (uintptr_t)lv_event_get_user_data(event);
    int index = (int)((packed >> 8) & 0xffu);
    uint32_t bit = (uint32_t)(packed & 0xffu);
    const vqeaf_qeapp_t *app = vqeaf_qeapp_at(index);
    int grant;
    if(app == NULL || bit == 0u) return;
    grant = (vqeaf_qeapp_effective_permissions(index) & bit) == 0u;
    if(vqeaf_qeapp_set_permission(index, bit, grant) == 0) {
        vqeaf_ui_notify("Permission Manager",
                        grant ? "Đã cấp quyền" : "Đã thu hồi quyền",
                        VQEAF_SYM_CHECK, 0x69D6A5);
        vqeaf_ui_show(VQEAF_PAGE_QEAPP);
    }
}

'''
if marker not in s: raise SystemExit("page callback marker missing")
s=s.replace(marker,callback+marker,1)
# append permission controls after facts details block, before native lifecycle comment
needle='''    qeapp_fact(facts, "Dữ liệu riêng", perm_buf);

    /* Last on-device run of this module.'''
repl='''    qeapp_fact(facts, "Dữ liệu riêng", perm_buf);

    /* Permission Manager: requested capabilities are read-only manifest data;
     * each grant is a persisted user decision and can be revoked here. */
    {
        lv_obj_t *manager = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(manager, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(manager, 8, 0);
        vqeaf_label(manager, "Quản lý quyền", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
        vqeaf_label(manager, "Quyền chưa cấp mặc định bị từ chối. Quyền mới "
                    "sau cập nhật không tự động được bật.",
                    VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
        static const struct { uint32_t bit; const char *name; } controls[] = {
            { 1u << 0, "Hiển thị giao diện" },
            { 1u << 1, "Đọc tệp" },
            { 1u << 2, "Ghi dữ liệu riêng" },
            { 1u << 3, "Nhận input" },
            { 1u << 4, "Đọc media" },
        };
        for(size_t pi = 0; pi < sizeof(controls)/sizeof(controls[0]); pi++) {
            uint32_t bit = controls[pi].bit;
            if((app->permissions & bit) == 0u) continue;
            lv_obj_t *line = vqeaf_obj(manager);
            lv_obj_set_size(line, lv_pct(100), 58);
            lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(line, LV_FLEX_ALIGN_SPACE_BETWEEN,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_t *label = vqeaf_label(line, controls[pi].name,
                                         VQEAF_FONT_CAPTION, VQEAF_C_TEXT);
            lv_obj_set_flex_grow(label, 1);
            int enabled = (vqeaf_qeapp_effective_permissions(index) & bit) != 0u;
            lv_obj_t *action = vqeaf_obj(line);
            lv_obj_set_size(action, 132, 48);
            lv_obj_set_style_bg_color(action,
                enabled ? lv_color_hex(0x263F38) : VQEAF_C_SURFACE_3, 0);
            lv_obj_set_style_bg_opa(action, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(action, 24, 0);
            vqeaf_label(action, enabled ? "Đang cấp · Thu hồi" : "Đã chặn · Cấp",
                        VQEAF_FONT_CAPTION, enabled ? lv_color_hex(0x8DE0B6) :
                                                       VQEAF_C_TEXT);
            lv_obj_add_flag(action, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(action, qeapp_permission_toggle_cb,
                                LV_EVENT_SHORT_CLICKED,
                                (void *)(uintptr_t)(((uint32_t)index << 8) | bit));
        }
    }

    /* Last on-device run of this module.'''
if needle not in s: raise SystemExit("permission UI insertion point missing")
s=s.replace(needle,repl,1)
p.write_text(s,encoding="utf-8")
print("patched permission manager API/UI and fail-closed native launch gate")
