# QEAF Android/Linux Driver Converter

Tool: `tools/qeaf_driver_converter.py`

The converter is a source intake, dependency classifier and port-plan generator. It is deliberately conservative: it does not perform unsafe global substitutions in kernel code, does not convert `.ko` binaries into source, and never reports an unimplemented probe as successful.

## Usage

```powershell
py -3.12 tools\qeaf_driver_converter.py <android-driver-source> --name <qeaf-driver-name> --out build\qeaf-driver-convert
```

Outputs:

- `reference_source/` — byte-preserved input files.
- `port-plan.json` — SHA-256 inventory, API findings and status.
- `PORTING.md` — file/line migration worklist.
- `qeaf_driver_skeleton.c` — fail-closed QEAF probe/remove skeleton.

Statuses: `BLOCKED` means a hard Android-only dependency (for example Binder/HIDL or secure-world ABI) is present; `REQUIRES_PORT` means Linux kernel/Android APIs need an explicit QEAF implementation; `REVIEW_REQUIRED` means manual ABI and hardware review remains mandatory.

## Violet ICNSS/QMI intake performed

Input snapshot: `build/android-driver-input/violet-icnss-core/` containing `icnss.c`, `icnss_qmi.c`, `icnss_private.h`, `icnss_qmi.h`, and the WLFW v01 generated protocol C/header.

Output: `build/qeaf-driver-convert/violet-icnss-core/`.

Result: `REQUIRES_PORT`. The Linux platform-driver, device/PM/IRQ/DMA and logging calls cannot be text-rewritten into a native QEAF driver. The QMI/WLFW protocol structures are retained for protocol comparison, not blindly copied into a new implementation.

## Existing QEAF-native implementation to reconcile

- `kernel/drivers/wifi/wcn3990/qrtr_transport.c` — QRTR transport boundary.
- `kernel/drivers/wifi/wcn3990/qmi_service.c` — QMI framing/TLV/client state.
- `kernel/drivers/wifi/wcn3990/wlfw_client.c` — WLFW service client and message flow.
- `kernel/drivers/wifi/wcn3990/firmware_boot.c` — staged power/firmware/QRTR/WLFW state machine.
- `kernel/drivers/wifi/wcn3990/wcn3990_ce.c` — copy-engine descriptors and lifecycle.
- `kernel/drivers/wifi/wcn3990/violet_platform.c` — Violet platform resource contract.

These are a native QEAF implementation path, not proof that the physical WLAN is working. Do not start WLAN/Modem PD or write persist/NV as part of source conversion. Device acceptance still requires exact board DT resources, matching firmware/BDF/calibration, WLFW publication, stable modem, and a real `wlan0`/scan test.
