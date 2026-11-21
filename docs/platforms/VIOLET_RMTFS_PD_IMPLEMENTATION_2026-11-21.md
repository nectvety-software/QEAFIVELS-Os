# Violet Wi-Fi: RMTFS/EFS + WLAN PD/WLFW implementation

Date: 2026-11-21

## Changes in this pass

### RMTFS / shared memory

- Confirmed the stock Violet kernel exposes a UIO device named `rmtfs` under
  `/sys/class/uio`; the QEAF publisher accepts only that exact sysfs name and
  reads the real `dev`, `maps/map0/addr`, and `maps/map0/size` attributes.
- Hardened the vendored RMTFS UIO path: map size must be non-zero and fit
  `size_t`; physical address plus size must not overflow; mmap uses the real
  map0 size and offset zero.
- Added a positive diagnostic emitted only after mmap succeeds:
  `RMTFS UIO ready: client=1 addr=... size=... mmap=...`.
- Retained `rmtfs -P -r -v`. `-r` deliberately prevents writes to modem NV
  partitions: modem-originated writes remain in the daemon's RAM shadow. This
  is a bring-up safety policy, not persistent EFS repair. No partition,
  calibration file, or factory MAC is synthesized or written.

### WLAN PD / SERVREG / WLFW

- Reworked `pd-listen` to support `--watch msm/modem/wlan_pd`.
- The watcher sends a QRTR NEW_LOOKUP for SERVREG service `0x42` with version
  zero (any version), then waits for real QRTR NEW_SERVER/DEL_SERVER events.
- It sends SERVREG_REGISTER_LISTENER_REQ (QMI message 0x20) only to endpoints
  announced by the kernel QRTR name service, validates the matching QMI
  response, and logs result/error.
- PID 1 starts the watcher immediately after `pd-mapper`; it does not publish
  a fake SERVREG or WLFW endpoint and does not send SERVREG_RESTART_PD.
- Existing CNSS/WLFW behavior remains evidence-driven. A live watcher or a
  successful SERVREG listener registration does not mean WLAN PD is online.
  WLFW is still considered available only after a real QRTR NEW_SERVER for
  service 0x45; `wlan0` only after the real network interface exists.

## Safety boundaries

- RMTFS uses actual kernel UIO metadata; no guessed physical address and no
  /dev/mem fallback is introduced by QEAF.
- EFS/NV is read-only at the storage layer. Shadow writes are volatile.
- Calibration is copied from the mounted, verified persist tree to tmpfs only.
- No QMI response, QRTR service, WLFW state, MAC, or calibration is mocked.
- No flash operation was performed by this source change.

## Verification required on the phone

1. `wifi host` reports RMTFS running and `wifi log` contains
   `RMTFS UIO ready` with the real map address/size.
2. `/proc/<rmtfs-pid>/maps` shows the UIO mapping; RMTFS stays alive after
   modem ONLINE.
3. `hostsvc-pd-listen.log` shows a real SERVREG NEW_SERVER and the actual
   QMI 0x20 response. A timeout or non-zero result is a failure.
4. `qrtr-ns servers` contains a real `0x45` NEW_SERVER and qlookup receives
   its endpoint; then the WLFW handshake log must show IND_REGISTER/CAP/BDF/
   CAL_REPORT/MAC before any Wi-Fi-ready claim.
5. `/sys/class/net/wlan0` must exist before reporting the interface.

Host build/test success is not device verification. No ADB device was attached
during this implementation pass, so no live-device result is claimed.
