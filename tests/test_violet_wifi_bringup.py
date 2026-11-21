import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HOST = ROOT / "platform/violet/host_services.c"
RMTFS = ROOT / "platform/violet/hosttools/src/rmtfs"
PD_LISTEN = ROOT / "platform/violet/hosttools/src/wlfwd/pd-listen.c"
BUILD = ROOT / "scripts/build_violet_hosttools.py"
DEVNODES = ROOT / "platform/violet/devnodes.c"


class VioletWifiBringupContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.host = HOST.read_text(encoding="utf-8")
        cls.sharedmem = (RMTFS / "sharedmem.c").read_text(encoding="utf-8")
        cls.storage = (RMTFS / "storage.c").read_text(encoding="utf-8")
        cls.listener = PD_LISTEN.read_text(encoding="utf-8")
        cls.builder = BUILD.read_text(encoding="utf-8")
        cls.devnodes = DEVNODES.read_text(encoding="utf-8")

    def test_rmtfs_uses_kernel_uio_map_not_guessed_address(self):
        self.assertIn('"/dev/qcom_rmtfs_uio%d"', self.sharedmem)
        self.assertIn('"maps/map0/addr"', self.sharedmem)
        self.assertIn('"maps/map0/size"', self.sharedmem)
        self.assertIn("mmap(0, (size_t)rmem->size", self.sharedmem)
        self.assertIn("rmem->address > UINT64_MAX - rmem->size", self.sharedmem)
        self.assertIn("RMTFS UIO ready:", self.sharedmem)

    def test_rmtfs_read_only_uses_ram_shadow_for_modem_writes(self):
        self.assertIn("storage_populate_shadow_buf(rmtfd, fspath)", self.storage)
        self.assertIn("if (!storage_read_only)", self.storage)
        self.assertIn("memcpy((char*)rmtfd->shadow_buf + offset, buf, nbyte)", self.storage)
        self.assertIn("return pwrite(rmtfd->fd, buf, nbyte, offset)", self.storage)

    def test_uio_node_is_published_only_for_named_rmtfs_device(self):
        self.assertIn('strcmp(name, "rmtfs")', self.host)
        self.assertIn('"%s/maps/map0/addr"', self.host)
        self.assertIn('"%s/maps/map0/size"', self.host)
        self.assertIn('"/dev/qcom_rmtfs_uio1"', self.host)

    def test_servreg_watcher_discovers_real_qrtr_endpoint(self):
        self.assertIn("qrtr_new_lookup(sock, SERVREG_SERVICE, SERVREG_VERSION, 0)", self.listener)
        self.assertIn("pkt.type == QRTR_TYPE_NEW_SERVER", self.listener)
        self.assertIn("pkt.service != SERVREG_SERVICE", self.listener)
        self.assertIn("SERVREG_REGISTER_LISTENER_REQ", self.listener)
        self.assertIn('"msm/modem/wlan_pd"', self.host)

    def test_pid1_starts_pdr_watcher_after_pd_mapper(self):
        mapper = self.host.index("(void)launch(svc_pd, kPdArgs)")
        watcher = self.host.index("(void)launch(svc_pd_listen, kPdListenArgs)")
        tftp = self.host.index("(void)launch(svc_tftp, kTftpArgs)")
        self.assertLess(mapper, watcher)
        self.assertLess(watcher, tftp)

    def test_no_fake_wlfw_server_or_qmi_bypass(self):
        self.assertNotIn("qrtr_new_server", self.listener)
        self.assertNotIn("QRTR_TYPE_NEW_SERVER,", self.listener)
        self.assertIn("launch_qrtr_lookup()", self.host)

    def test_calibration_is_copied_only_from_real_persist_to_tmpfs(self):
        self.assertIn("persist_path_is_local(source)", self.host)
        self.assertIn('"/tmp/wlfw-state/%s"', self.host)
        self.assertIn("wlfw_cal_%02u.bin", self.host)

    def test_pd_listen_links_real_qrtr_library(self):
        self.assertIn('"pd-listen": [os.path.join(SRC, "wlfwd", "pd-listen.c")] + QRTR_LIB', self.builder)

    def test_v4l_subdev_nodes_survive_the_drm_connector_filter(self):
        self.assertIn('strcmp(sysdir, "/sys/class/drm") == 0', self.devnodes)
        self.assertIn('scan_class("/sys/class/video4linux", "v4l-subdev", "/dev", S_IFCHR)',
                      self.devnodes)


if __name__ == "__main__":
    unittest.main()

