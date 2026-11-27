import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class LinuxCoreBoundaryTests(unittest.TestCase):
    def test_violet_init_excludes_freestanding_radio_drivers(self):
        build = (ROOT / "scripts" / "build_violet.py").read_text(encoding="utf-8")
        for path in (
            "kernel/drivers/bluetooth/wcn3990_bt.c",
            "kernel/drivers/bluetooth/wcn3990_slim.c",
            "kernel/drivers/slimbus/slim_qcom_ngd.c",
            "slim_ngd_bringup.c",
        ):
            self.assertNotIn('"%s"' % path, build)
            self.assertNotIn("'%s'" % path, build)

    def test_linux_bluetooth_report_has_no_userspace_mmio_path(self):
        source = (ROOT / "platform" / "violet" / "bt_bringup.c").read_text(
            encoding="utf-8")
        self.assertNotIn("/dev/mem", source)
        self.assertNotIn("mmap(", source)
        self.assertNotIn("vio_bt_native_ops", source)
        self.assertIn("userspace-mmio=disabled", source)

    def test_rescue_radio_verbs_do_not_start_reference_drivers(self):
        rescue = (ROOT / "platform" / "violet" / "channel" / "fs" / "rescue.c").read_text(
            encoding="utf-8")
        self.assertNotIn("slim_ngd_bringup.h", rescue)
        self.assertNotIn("vio_slim_ngd_probe", rescue)
        self.assertNotIn("vio_bt_attempt", rescue)
        self.assertIn("bt up: disabled on the Linux-core PID 1", rescue)


if __name__ == "__main__":
    unittest.main()
