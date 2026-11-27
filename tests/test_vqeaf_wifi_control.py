import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
WIFI = ROOT / "platform/violet/ui/vqeaf_wifi.c"
WIFI_H = ROOT / "platform/violet/ui/vqeaf_wifi.h"
PAGES = ROOT / "platform/violet/ui/vqeaf_pages.c"
HOST = ROOT / "platform/violet/host_services.c"


class VioletWifiControlContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.wifi = WIFI.read_text(encoding="utf-8")
        cls.header = WIFI_H.read_text(encoding="utf-8")
        cls.pages = PAGES.read_text(encoding="utf-8")
        cls.host = HOST.read_text(encoding="utf-8")

    def test_uses_real_wpa_control_socket_without_shell(self):
        self.assertIn('WPA_CTRL_PATH "/run/wpa_supplicant/wlan0"', self.wifi)
        self.assertIn("socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0)", self.wifi)
        self.assertNotIn("system(", self.wifi)
        self.assertNotIn("popen(", self.wifi)

    def test_scan_and_status_use_supplicant_protocol(self):
        self.assertIn('wifi_ctrl_ok(fd, "SCAN")', self.wifi)
        self.assertIn('"SCAN_RESULTS"', self.wifi)
        self.assertIn('"STATUS"', self.wifi)
        self.assertIn('strcmp(status->state, "COMPLETED") == 0', self.wifi)

    def test_wpa2_sae_and_owe_profiles_are_distinct(self):
        self.assertIn('key_mgmt = "WPA-PSK"', self.wifi)
        self.assertIn('key_mgmt = "SAE"', self.wifi)
        self.assertIn('key_mgmt = "OWE"', self.wifi)
        self.assertIn("VQEAF_WIFI_ENC_EAP", self.wifi)
        self.assertIn("case VQEAF_WIFI_ENC_EAP:", self.wifi)

    def test_secret_is_not_written_to_logs_or_persistent_config(self):
        self.assertIn("memset(secret, 0, sizeof(secret))", self.wifi)
        self.assertIn("memset(command, 0, sizeof(command))", self.wifi)
        self.assertIn("update_config=0", self.host)
        self.assertNotIn('vio_log(".*password', self.wifi)

    def test_ui_passes_network_security_and_shows_real_status(self):
        self.assertIn("g_wifi_pick.enc", self.pages)
        self.assertIn("g_wifi_nets[index].enc", self.pages)
        self.assertIn("vqeaf_wifi_get_status(&g_wifi_status)", self.pages)
        self.assertIn('"Địa chỉ IP"', self.pages)
        self.assertIn("wifi_disconnect_cb", self.pages)

    def test_vendor_supplicant_starts_only_after_real_wlan0(self):
        self.assertIn('"/vendor/bin/hw/wpa_supplicant"', self.host)
        self.assertIn('path_exists("/sys/class/net/wlan0")', self.host)
        self.assertIn('"/run/qeaf/wpa_supplicant.conf"', self.host)
        self.assertIn("start_wpa_worker();", self.host)


if __name__ == "__main__":
    unittest.main()
