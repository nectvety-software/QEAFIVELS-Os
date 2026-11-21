"""QEAPP permission-boundary regression tests.

These tests verify source-level wiring. They do not emulate Linux credentials,
seccomp, filesystem namespaces or a running phone kernel.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
DEVICE = (ROOT / "platform/violet/ui/vqeaf_qeapp_device.c").read_text(encoding="utf-8")
DEVICE_H = (ROOT / "platform/violet/ui/vqeaf_qeapp_device.h").read_text(encoding="utf-8")
PAGES = (ROOT / "platform/violet/ui/vqeaf_pages.c").read_text(encoding="utf-8")
QEAPP = (ROOT / "platform/violet/ui/vqeaf_qeapp.c").read_text(encoding="utf-8")


class QeappSecurityContractTests(unittest.TestCase):
    def test_native_launch_receives_effective_grants(self):
        self.assertIn("uint32_t effective_permissions", DEVICE_H)
        self.assertIn("vqeaf_qeapp_effective_permissions(vqeaf_qeapp_selected())", PAGES)
        self.assertIn("g_permissions = info.permissions & effective_permissions", DEVICE)

    def test_api_display_and_permission_query_use_effective_mask(self):
        self.assertIn("g_permissions & QEAF_PERM_UI_DISPLAY", DEVICE)
        self.assertIn("(g_permissions & bit) != 0u", DEVICE)

    def test_new_install_does_not_auto_grant_display(self):
        self.assertIn("g_apps[slot].granted_permissions = 0u", QEAPP)
        self.assertNotIn("granted_permissions |= (meta.permissions & (1u << 0))", QEAPP)

    def test_catalog_grants_are_bound_to_manifest_identity(self):
        self.assertIn('strcmp(old_meta.id, meta.id) == 0', QEAPP)
        self.assertIn("if(same_identity)", QEAPP)

    def test_native_execution_goes_through_the_process_sandbox(self):
        # The runner no longer blanket-refuses native code: the child applies
        # the sandbox before device_exec() and stays blocked if any layer is
        # refused.
        self.assertNotIn("native QEAPP blocked", DEVICE)
        launch = DEVICE[DEVICE.index("int vqeaf_qeapp_device_launch"):]
        self.assertIn("vqeaf_qeapp_sandbox_apply", launch)
        self.assertLess(launch.index("vqeaf_qeapp_sandbox_apply"),
                        launch.index("device_exec(package"))

    def test_sandbox_layers_are_all_present_and_ordered(self):
        box = (ROOT / "platform/violet/ui/vqeaf_qeapp_sandbox.c").read_text(encoding="utf-8")
        for layer in ("PR_SET_NO_NEW_PRIVS", "setgroups(", "setgid(", "setuid(",
                      "SECCOMP_MODE_FILTER", "SECCOMP_RET_ERRNO"):
            self.assertIn(layer, box)
        # The filter goes in last, after the identity change and the fd sweep,
        # because the setup calls above are not on its allowlist.
        apply_body = box[box.index("int vqeaf_qeapp_sandbox_apply"):]
        self.assertLess(apply_body.index("setuid((uid_t)VQEAF_QEAPP_SANDBOX_UID)"),
                        apply_body.index("sandbox_filter(error, cap)"))
        self.assertLess(apply_body.index("RLIMIT_NOFILE"),
                        apply_body.index("sandbox_filter(error, cap)"))
        self.assertIn("vqeaf_qeapp_sandbox.c",
                      (ROOT / "scripts/build_violet.py").read_text(encoding="utf-8"))

    def test_native_run_button_is_wired_to_the_runner(self):
        self.assertIn("Chạy ứng dụng", PAGES)
        self.assertNotIn("Native tạm khóa", PAGES)


if __name__ == "__main__":
    unittest.main()
