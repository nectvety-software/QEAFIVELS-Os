# QEAFIVELS Violet — Wi-Fi connection implementation

Date: 2026-11-27

## Implemented in the current source tree

- Settings Wi-Fi uses the real wpa_supplicant Unix control interface at /run/wpa_supplicant/wlan0; it does not invoke a shell.
- Scan uses SCAN / SCAN_RESULTS; results are parsed into BSSID, SSID, signal strength and security type.
- Connect creates an in-memory network profile, encodes SSID as hex, and configures open (NONE), WPA/WPA2-Personal (WPA-PSK), WPA3-Personal (SAE + PMF) or OWE (with PMF).
- WEP and enterprise EAP are identified but rejected explicitly rather than being misconfigured as open networks.
- Passwords are never logged or written to a persistent config. Secret buffers are cleared after control commands; update_config=0 keeps profiles in RAM for the session.
- STATUS is authoritative: the UI reports connected only when wpa_state=COMPLETED, and shows SSID, BSSID and any IP address. Settings also has a disconnect action.
- PID 1 starts the vendor supplicant only after a real /sys/class/net/wlan0 appears. The service config and control socket directories are under /run; no partition is written.

## Current limitations / hardware gate

This implementation is not yet proof of a working radio. The latest device evidence in VIOLET_WIFI_BRINGUP.md still shows WLAN PD msm/modem/wlan_pd rejected by SERVREG (result=1 error=45), restart rejected (error=9), WLFW QMI 0x45 absent, and /sys/class/net/wlan0 absent.

Until the modem publishes WLFW and qcacld creates wlan0, the supplicant worker deliberately waits and the UI reports the missing interface/service. The current image also has no verified DHCP client; association may succeed before IPv4 is configured. DHCP integration and physical validation are the next milestones.

## Build/test evidence (host only)

- python scripts/build_violet.py: PASS; vqeaf-init linked at 12,723,272 bytes (292 objects).
- python -m unittest discover -s tests -p '*wifi*.py' -v: 21 tests PASS after the final source changes.
- These are source/build checks only. No new phone boot, scan, association, DHCP lease or internet connectivity has been verified in this session.
- No partition was flashed or modified.
