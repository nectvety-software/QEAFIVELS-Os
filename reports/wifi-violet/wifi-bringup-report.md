# QEAFIVELS OS — REAL DEVICE Wi-Fi Bring-up Test (Violet)

- Test date: 2026-11-21
- Device: Xiaomi Redmi Note 7 Pro (`violet`), SM6150 / Snapdragon 675
- Kernel: `4.14.215-azure-Pixel+` (stock vendor kernel, monolithic)
- Boot image SHA-256: `494FD87257659D4AE0A197C88301CCD10D4F2A113BC472E644C1D947C8280A79` (user pre-flashed)
- Connection method: **QEAFRESCUE raw FAT volume `F:`** (no ADB, no fastboot; `adb devices -l` empty, `fastboot devices` empty). All data below came from live rescue verbs, not source inspection.
- Raw artifacts: `reports/wifi-violet/*.txt` (device-info, wifi-host, wifi-log, wifi-modem, wifi-service, qrtr-servers, rmtfs-uio, servreg-registration, wlfw-cnss, wlan-interface, modem-stability, modem-stability-2).

## Code follow-up (2026-11-24, not a device retest)

- No new physical Wi-Fi run was performed. The packaged p77 image was not flashed and no
  persist/NV partition was written.
- The early MAC gate in `vio_host_services_start_wlan()` was a QEAF source defect: it suppressed
  the real CNSS attempt. Current order is Binder → real CNSS → MAC/calibration only for the
  custom `wlfwd` fallback. A missing p74 MAC no longer proves that CNSS was attempted.
- Manual CNSS evidence in Phase E still showed no WLFW QMI `0x45`; WLAN-PD failure and the
  independent modem/SoC reset remain unresolved. `wlan0` therefore remains absent.
- Host regression: Violet bring-up contracts 15/15, Wi-Fi Python tests 24/24, native CTest
  18/18, strict/UBSan CE fixtures, depth-bounded/noncontiguous-IRQ DTB regressions,
  AArch64 build/QEMU and package validation/audit all PASS. These results do not change
  the physical FAIL verdicts below.

## Phase A — Device identity
Live: `QEAFIVELS OS 0.3.0; device violet; ... stage=loop; ui=1; drm=1; uptime=180s`.
Kernel from `/proc/version`: see `device-info.txt`. Device reachable only through rescue channel; no UART console used.

## Phase B — Diagnostics collected
All requested native verbs were executed through QEAFRESCUE. `wifi net` is the equivalent of `ip link` (the initramfs has no `ip`/`ps` tooling). Complete raw output is in the referenced files; key lines are quoted below.

## Phase C — RMTFS / UIO / mmap — **PASS**
| Check | Evidence | Verdict |
|---|---|---|
| UIO discovered | `/sys/class/uio: uio0` | PASS |
| Correct device | `/sys/class/uio/uio0/name: rmtfs` | PASS |
| Kernel metadata | `/sys/class/uio/uio0/dev: 504:0`; `maps/map0/addr: 0x00000000f0901000`; `maps/map0/size: 0x0000000000200000` | PASS |
| Device node published | `/dev` listing contains `qcom_rmtfs_uio1` | PASS |
| mmap success | `RMTFS UIO ready: client=1 addr=0xf0901000 size=0x200000 mmap=0x7f94c80000` | PASS |
| RMTFS daemon | `hostsvc rmtfs: running pid=486/488` | PASS |

No guessed address: the mmap line matches sysfs addr/size exactly. EFS/NV was not written.

## EFS / persist calibration — **FAIL**
- `wifi find wlan_mac.bin: not found`
- `wifi find wlfw_cal_00.bin: not found`
- `/mnt/vendor/persist` contains `display bluetooth vpp misc time secnvm tp_test camera ... hlos_rfs rfs ...` but no `wlan/`, no `wlan_mac.bin`.
- `hostsvc wlan: p74 not mounted or factory WLAN MAC absent/invalid; WLAN blocked` (`host_services.c:1035`, `find_factory_mac()` at line 916).

## Phase D — SERVREG / WLAN PD — **FAIL**
- `pd-listen: watching real QRTR service 0x42 for msm/modem/wlan_pd` (only line in `/tmp/hostsvc-pd-listen.log`).
- QRTR servers include `0x42/0x4c@0xa:0x3`, `0x42/0xb4@0x0:0x3` and lookups `0x42/0x0` — service 0x42 exists.
- **No `SERVREG NEW_SERVER` line and no `result=0 error=0` line** was observed. The success format in `pd-listen.c:93` is `pd-listen: <path> via servreg <node>:<port> result=0 error=0`; it never appeared.
- Verdict: listener **not proven registered**. Do not claim success from process being alive.

## Phase E — WLFW 0x45 / CNSS — **FAIL**
- `wifi qlookup`: `lookup 0x45/0x1/0x0 sent` followed by repeated `ERROR libqrtr: recvfrom(): Resource temporarily unavailable`.
- `qrtr-ns servers`: no `svc=0x45` entry; only lookups `0x45/0x0->...`.
- After `wifi up`: `hostsvc cnss-daemon: not started`, `hostsvc binder: absent`. Cause: `vio_host_services_start_wlan()` returns early at the missing factory MAC check before launching CNSS.
- Manual `wifi vendor` (diagnostic only) does mount binderfs (`/dev/binder -> /dev/binderfs/binder`, `vndbinder`, `hwbinder`) and then `cnss-daemon: running pid=541/546`, with `/proc/<pid>/fd/15 -> /dev/binderfs/vndbinder`. Full log is only:
  ```
  linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
  WARNING: linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
  ```
  No `Binder driver could not be opened`, no SIGABRT, no missing service-manager abort; but also no WLFW debug (kernel has no Android logger/logcat). The stock sysroot has no `hwservicemanager`/`servicemanager` binaries; cnss-daemon did **not** request them in this run.

## Phase F — Kernel WLAN interface — **FAIL**
- `wifi net`: `lo, dummy0, bond0, sit0, ip6tnl0, ip_vti0, ip6_vti0, bonding_masters` — **no `wlan0`**.
- `wifi: wlan0 present: no`.
- dmesg: `icnss: Platform driver probed successfully`, `wlan_hdd_state wlan major(500) initialized`, but no firmware-ready / WLFW-ready line.
- No passive scan possible (no interface). No connection attempted (not authorized).

## Phase G — Modem stability — **FAIL**
- `wifi up` boots modem for real: `modem: Brought out of reset`, `modem: Power/Clock ready interrupt received`, `qrtr: Modem QMI Readiness RX cmd:0x2 node[0x0]`, `ssctl_new_server ... modem's SSCTL service`.
- Immediately after: `wifi modem: fd=11 rc=1 thread_done=1`, `hostsvc wlan: p74 not mounted or factory WLAN MAC absent/invalid; WLAN blocked`.
- Fine monitor (`modem-stability-2.txt`) was alive at T+14 s, then the rescue channel disappeared; device rebooted and came back at `stage=loop` with fresh uptime. The previous 40–60 s reset problem **still exists** (observed ~15–20 s after `wifi up` in this run).

## Phase H — Dependency table
| Component | Expected | Actual | Status |
|---|---|---|---|
| Modem | Online | `Power/Clock ready`, QMI readiness cmd 0x2 | PASS (transient) |
| RMTFS UIO | Present | uio0 `rmtfs`, 504:0, 0xf0901000/0x200000 | PASS |
| RMTFS mmap | Success | `mmap=0x7f94c80000` | PASS |
| EFS calibration | Accessible | no `wlan_mac.bin`, no `wlfw_cal_*` | FAIL |
| SERVREG 0x42 | Published | `0x42/0x4c@0xa:0x3` etc. | PASS |
| SERVREG listener | `result=0 error=0` | no NEW_SERVER / no result line | FAIL (not proven) |
| WLFW 0x45 | Published | lookup only, no server | FAIL |
| CNSS | Initialized | not started by bring-up; manual run alive but no WLFW | FAIL |
| Firmware | Loaded | no WLFW/firmware-ready evidence | FAIL |
| wlan0 | Present | absent | FAIL |
| Wi-Fi scan | Success | impossible (no wlan0) | FAIL |

**Earliest failing dependency at test time:** factory persist calibration / WLAN MAC (`p74` has no `wlan_mac.bin` or `wlfw_cal_*`) triggered QEAF's early return before CNSS. That source-order defect is fixed in the 2026-11-24 follow-up, but no device retest exists. The physical blockers still observed or previously measured are missing per-unit persist data, WLAN-PD failure, no WLFW `0x45`, no `wlan0`, and an independent modem/SoC reset.

## Phase I — Fixes
No source change attempted in this test. Fabricating a MAC, generating calibration data, writing EFS/NV, or creating a fake QRTR/WLFW service is forbidden and was not done.

Recommended next action (requires user authorization / real data):
1. Obtain a **real stock persist backup** from this exact device (p74 `wlan_mac.bin` / `wlan/wlan_mac.bin` and `wlfw_cal_*.bin`) or a verified service dump; restore to persist before bring-up.
2. Separately debug the modem reset (watchdog / IPA/EFS) — it is independent of the MAC gate.
3. Add a regression test that asserts real CNSS is attempted before the factory-MAC gate and that missing MAC blocks only the custom WLFW fallback (implemented in `tests/test_violet_wifi_bringup.py`).

## Final acceptance criteria
Not met. Only RMTFS UIO/mmap passed. SERVREG registration, WLFW publication, firmware load, `wlan0` and passive scan all failed with the evidence above.
