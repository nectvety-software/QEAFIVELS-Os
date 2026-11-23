# Firmware Requirements — Violet

No proprietary firmware is bundled by this project.

The inspected device-tree repositories include proprietary-files manifests (1,207 lines in the Xiaomi reference manifest). These manifests reference vendor binaries and firmware, not source code suitable for direct inclusion in MRE OS. Observed categories include ADSP/ACDB data, audio amplifier firmware (tas2563_uCDSP.bin), camera tuning and sensor modules, QMI/QRTR vendor libraries and Android vendor services.

| Component | Target | Required investigation | Current status |
|---|---|---|---|
| Wi-Fi | Qualcomm WCN3990 / QCA WLAN stack | Exact firmware filenames, board/calibration data, CNSS/ICNSS transport, QMI protocol and license | UNKNOWN |
| Bluetooth | Qualcomm combo radio | Controller firmware, UART/IBS transport, coexistence and license | UNKNOWN |
| GPU | Adreno 612 | Firmware interface, version compatibility, secure boot chain and licensing | UNKNOWN |
| Modem | Qualcomm modem subsystem | Signed firmware, QMI/QRTR/IPC contract, secure ownership and license | UNKNOWN |
| Camera | Sensors / ISP | Physical sensor list, EEPROM, tuning data and license | UNKNOWN |
| Audio DSP | Qualcomm audio subsystem | DSP firmware, ACDB, codec/amplifier topology and license | UNKNOWN |

## Wi-Fi reference notes

The three Violet device trees expose a WCNSS_qcom_cfg.ini file. The Xiaomi BoardConfig selects WLAN family qcwcn and qca_cld3. These settings establish the Android vendor integration family, but do not establish the exact MRE transport, firmware build, calibration source or QMI service version.

## Acceptance policy

A firmware item may be enabled only when origin, exact version, SHA-256, device compatibility and license are documented. Redistribution must be explicitly allowed. Otherwise firmware remains external and is not copied into release artifacts.

The firmware manager currently exposes a policy-validation interface. A platform-specific cryptographic hash implementation and trusted storage loader are still required; no checksum is fabricated by the host layer.
