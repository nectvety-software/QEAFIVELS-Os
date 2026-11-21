from pathlib import Path
root=Path(r"D:\QEAFIVELS-Os")
p=root/"platform/violet/ui/vqeaf_pages.c"
s=p.read_text(encoding="utf-8")
s=s.replace('''        vqeaf_label(run, "Chạy trên thiết bị", VQEAF_FONT_BODY,
                    lv_color_hex(0x06120F));
        vqeaf_on_click(run, native_run_cb, NULL);''','''        vqeaf_label(run, "Native tạm khóa · chờ sandbox", VQEAF_FONT_BODY,
                    lv_color_hex(0x06120F));
        lv_obj_clear_flag(run, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(run, VQEAF_C_SURFACE_3, 0);''')
p.write_text(s,encoding="utf-8")

p=root/"docs/QEAPP_SECURITY_AND_REPOSITORY.md"
with p.open("a",encoding="utf-8") as f:
    f.write("""
 
## Permission Manager implementation — 2026-11-20

- App detail page now includes a Vietnamese Permission Manager section.
- Each requested capability has a grant/revoke action. The UI does not offer
  capabilities absent from the signed manifest.
- \`vqeaf_qeapp_set_permission()\` validates a single known permission bit,
  checks it is requested, changes the per-app grant mask and persists via
  appstore v4. \`vqeaf_qeapp_effective_permissions()\` returns the intersection
  of requested and granted masks.
- Fresh installs and legacy v2/v3 migrations start with zero optional grants.
  Updates intersect old grants with the new request set; new permissions are
  never silently enabled.
- Native execution is now fail-closed: launch returns an explicit sandbox
  unavailable error before fork/module execution. The UI marks native apps as
  temporarily blocked. This prevents a false security claim while child
  processes still inherit the shell UID.
- Existing framework API permission checks still need to be wired to the
  effective grant mask when native sandboxing is implemented. Native direct
  syscalls remain an untrusted-code boundary; no native app is executed.
- Build: \`python scripts/build_violet.py\` PASS (9,614,712 bytes, 265 objects).
- Tests: \`python -m unittest tests.test_qeapp tests.test_qeapp_native -v\`
  PASS (63 tests). These existing tests do not exercise device UI interaction,
  appstore v4 grant persistence on real p77 media, or privilege separation.
""")
print("updated native launch status and security documentation")
