from pathlib import Path
root=Path(r"D:\QEAFIVELS-Os")
ui=root/"platform/violet/ui/vqeaf_ui.h"
s=ui.read_text(encoding="utf-8")
old="""    VQEAF_PAGE_APPS_EDIT,
    VQEAF_PAGE_COUNT
"""
new="""    VQEAF_PAGE_APPS_EDIT,
    /* Native QEAPP catalogue and installed-app management. */
    VQEAF_PAGE_QEAPP_STORE,
    VQEAF_PAGE_COUNT
"""
assert old in s
ui.write_text(s.replace(old,new,1),encoding="utf-8")

p=root/"platform/violet/ui/vqeaf_pages.c"
s=p.read_text(encoding="utf-8")
# Store opens from both the app drawer and dock.
s=s.replace('VQEAF_APP_ICON_STORE,','VQEAF_APP_ICON_STORE,',1)
s=s.replace('VQEAF_PAGE_COUNT,   VQEAF_APP_ICON_STORE','VQEAF_PAGE_QEAPP_STORE, VQEAF_APP_ICON_STORE')
s=s.replace('VQEAF_PAGE_COUNT, VQEAF_APP_ICON_STORE','VQEAF_PAGE_QEAPP_STORE, VQEAF_APP_ICON_STORE')
# Route to a dedicated native store page.
needle='    case VQEAF_PAGE_APPS_EDIT: build_apps_edit(content); break;\n'
assert needle in s
s=s.replace(needle,needle+'    case VQEAF_PAGE_QEAPP_STORE: build_qeapp_store(content); break;\n',1)
needle='    case VQEAF_PAGE_APPS_EDIT: return "Sửa màn hình chính";\n'
assert needle in s
s=s.replace(needle,needle+'    case VQEAF_PAGE_QEAPP_STORE: return "Chợ QEAPP";\n',1)
# Add detail navigation callback and page after uninstall callback.
needle="""static void qeapp_uninstall_cb(lv_event_t *event)
{
    int index = vqeaf_qeapp_selected();
    const vqeaf_qeapp_t *app = vqeaf_qeapp_at(index);
    char name[64] = "Ứng dụng";
    (void)event;
    if(app != NULL) snprintf(name, sizeof(name), "%s", app->name);
    vqeaf_qeapp_uninstall(index);
    vqeaf_ui_notify(name, "Đã gỡ khỏi thiết bị", VQEAF_SYM_DELETE, 0xFF8A7A);
    vqeaf_ui_show(VQEAF_PAGE_APPS);
}
"""
assert needle in s
addition=r'''
static void qeapp_store_detail_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    (void)event;
    if(vqeaf_qeapp_at(index) == NULL) return;
    vqeaf_qeapp_select(index);
    vqeaf_ui_show(VQEAF_PAGE_QEAPP);
}

/* The Store is a native catalogue/manager. Network discovery is intentionally
 * not fabricated: install/update candidates are selected from local .qeapp
 * files by the existing signed-package installer. */
static void build_qeapp_store(lv_obj_t *content)
{
    lv_obj_t *root = page_root(content);
    int count = vqeaf_qeapp_count();
    char summary[96];

    detail_header(root, "Chợ ứng dụng QEAPP");
    lv_obj_t *hero = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(hero, 10, 0);
    vqeaf_label(hero, "Kho ứng dụng native của QEAFIVELS",
                VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    vqeaf_label(hero,
        "Cài đặt gói .qeapp đã ký, xem phiên bản, quyền khai báo, cập nhật "
        "hoặc gỡ ứng dụng. Định dạng gói và trình quản lý đều là của QEAFIVELS.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_DIM);
    snprintf(summary, sizeof(summary), "%d / %d ứng dụng đã cài",
             count, QEAPP_MAX);
    vqeaf_label(hero, summary, VQEAF_FONT_BODY, VQEAF_C_ACCENT);

    lv_obj_t *install = vqeaf_m3_button(root, "Cài hoặc cập nhật từ tệp .qeapp",
                                        VQEAF_M3_FILLED);
    lv_obj_add_event_cb(install, nav_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)VQEAF_PAGE_INSTALLER);

    vqeaf_label(root, "Thư viện trên thiết bị", VQEAF_FONT_TITLE, VQEAF_C_TEXT);
    if(count == 0) {
        lv_obj_t *empty = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
        vqeaf_label(empty, "Chưa có ứng dụng QEAPP",
                    VQEAF_FONT_BODY, VQEAF_C_TEXT_DIM);
        vqeaf_label(empty, "Chép gói .qeapp vào QEAFRESCUE/Install/Apps "
                    "rồi chọn Cài hoặc cập nhật.",
                    VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
        return;
    }

    for(int i = 0; i < count; i++) {
        const vqeaf_qeapp_t *app = vqeaf_qeapp_at(i);
        if(app == NULL) continue;
        lv_obj_t *card = vqeaf_card(root, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(card, 14, 0);
        const lv_image_dsc_t *icon = vqeaf_qeapp_icon(i);
        if(icon == NULL) icon = vqeaf_app_icon(VQEAF_APP_ICON_QEAPP);
        if(vqeaf_icon_img(card, icon, 72) == NULL)
            vqeaf_tile(card, VQEAF_SYM_APPS, VQEAF_C_SURFACE_3,
                       VQEAF_C_ACCENT, 72, 22);
        lv_obj_t *details = vqeaf_column(card);
        lv_obj_set_flex_grow(details, 1);
        lv_obj_set_style_pad_row(details, 4, 0);
        lv_obj_t *name = vqeaf_label(details, app->name, VQEAF_FONT_BODY,
                                     VQEAF_C_TEXT);
        lv_obj_set_width(name, lv_pct(100));
        snprintf(summary, sizeof(summary), "Phiên bản %s · %s",
                 app->version, app->native ? "QEN native arm64" : "QEAPP");
        lv_obj_t *version = vqeaf_label(details, summary, VQEAF_FONT_CAPTION,
                                        VQEAF_C_TEXT_DIM);
        lv_obj_set_width(version, lv_pct(100));
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, qeapp_store_detail_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }

    lv_obj_t *note = vqeaf_label(root,
        "Cập nhật: chọn gói .qeapp mới trong Trình cài đặt. Hệ thống xác minh "
        "chữ ký và manifest rồi thay phiên bản theo cùng manifest.id; dữ liệu "
        "riêng được giữ lại. Gỡ ứng dụng trong trang chi tiết.",
        VQEAF_FONT_CAPTION, VQEAF_C_TEXT_MUTE);
    lv_obj_set_width(note, lv_pct(100));
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
}
'''
s=s.replace(needle,needle+addition,1)
p.write_text(s,encoding="utf-8")
print("Patched native QEAPP Store page, navigation and catalogue list.")
