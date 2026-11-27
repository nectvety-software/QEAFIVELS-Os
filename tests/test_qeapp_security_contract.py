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
UI = (ROOT / "platform/violet/ui/vqeaf_ui.c").read_text(encoding="utf-8")


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
        self.assertIn("names[0] = id;", QEAPP)
        self.assertIn("strcmp(meta.id, id) != 0", QEAPP)
        self.assertLess(QEAPP.index("read_metadata(bytes, bytes_size"),
                        QEAPP.index("strcmp(meta.id, id) != 0"))

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
        self.assertIn("#include <stdint.h>", box)
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

    def test_interactive_session_preserves_only_standard_input(self):
        start = DEVICE[DEVICE.index("int vqeaf_qeapp_device_session_start"):]
        start = start[:start.index("int vqeaf_qeapp_device_session_send")]
        self.assertIn("dup2(input_pipe[0], STDIN_FILENO)", start)
        self.assertIn("dup2(report_pipe[1], STDOUT_FILENO)", start)
        self.assertIn("dup2(report_pipe[1], STDERR_FILENO)", start)
        self.assertIn("vqeaf_qeapp_sandbox_apply", start)
        self.assertIn("F_SETFL", start)
        self.assertIn("O_NONBLOCK", start)
        self.assertNotIn("dup2(input_pipe[0], 3", start)

    def test_session_delivers_input_only_with_effective_permission(self):
        delivery = DEVICE[DEVICE.index("if(g_permissions & QEAF_PERM_INPUT_RECEIVE)"):]
        self.assertLess(delivery.index("QEAF_PERM_INPUT_RECEIVE"),
                        delivery.index("fn_input(handle, &event)"))

    def test_session_poll_is_nonblocking_and_stop_has_a_deadline(self):
        poll = DEVICE[DEVICE.index("int vqeaf_qeapp_device_session_poll"):]
        stop = DEVICE[DEVICE.index("void vqeaf_qeapp_device_session_stop"):]
        self.assertIn("wait_child(g_session_pid, &status, WNOHANG)", poll)
        self.assertIn("VQEAF_QEAPP_SESSION_STOP_MS", stop)
        self.assertIn("kill(g_session_pid, SIGKILL)", stop)

    def test_waitpid_retries_eintr(self):
        self.assertIn("static pid_t wait_child", DEVICE)
        self.assertIn("if(waited < 0 && errno == EINTR) continue;", DEVICE)

    def test_runner_reports_measured_child_identity(self):
        self.assertEqual(DEVICE.count('printf("identity: uid=%lu gid=%lu\\n"'), 2)
        self.assertIn("(unsigned long)getuid(), (unsigned long)getgid()", DEVICE)
        self.assertIn("device_result_identity(result);", DEVICE)
        self.assertIn("device_result_identity_load", DEVICE)
        self.assertNotIn("session_read_identity", DEVICE)

    def test_completion_evidence_is_not_parsed_from_module_stdout(self):
        self.assertIn("device_result_complete(result);", DEVICE)
        self.assertIn("device_result_complete(&control->result);", DEVICE)
        self.assertIn("atomic_load_explicit(&result->completed", DEVICE)
        self.assertNotIn('strstr(out, "lifecycle:")', DEVICE)
        self.assertNotIn('strstr(out, "hash=0x")', DEVICE)

    def test_uninstall_clears_the_unused_registry_tail(self):
        self.assertIn("memset(&g_apps[g_app_count], 0, sizeof(g_apps[g_app_count]));",
                      QEAPP)

    def test_session_only_launch_uses_retained_verified_bytes(self):
        self.assertIn("g_apps[index].package_data != NULL", QEAPP)
        self.assertIn("memcpy(bytes, g_apps[index].package_data", QEAPP)

    def test_native_page_forwards_touch_and_stops_on_exit(self):
        self.assertIn("vqeaf_qeapp_device_session_send(&input)", PAGES)
        self.assertIn("native_session_timer_cb", PAGES)
        self.assertIn("LV_EVENT_PRESS_LOST", PAGES)
        self.assertIn("g_page == VQEAF_PAGE_QEAPP_NATIVE && "
                      "page != VQEAF_PAGE_QEAPP_NATIVE", UI)
        self.assertIn("vqeaf_qeapp_device_session_stop();", UI)


if __name__ == "__main__":
    unittest.main()
