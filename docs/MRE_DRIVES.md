# `drives/` contracts, driver core registration and the SDK census

Status: host-gated and AArch64-compiled. **Nothing in this change has run on a
handset**, and the production Violet path still lets Linux own every device.

## What was integrated

`drives/` shipped 16 driver sources with only three of their contract headers
present (the MRE device/driver/firmware managers, plus the Violet registry that
was already wired in). The sources referenced contracts that did not exist in
this tree: `mre_reg_io.h`, `mre_dsi_packet.h`, `mre_panel_nt36672a.h`,
`mre_dsi_host_sm6150.h`, `mre_spmi.h`, `mre_pmic_arb.h`,
`mre_pmic_regulator.h`, `mre_geni_i2c.h`, `mre_touch.h`.

Each header was written from what its driver actually does — field order that
positional descriptor initializers depend on, the ops shapes, the mask
semantics, the error codes — not from guessed hardware values. The only numbers
added are values already present in the drive sources or in the MIPI DSI packet
types those sources use (0x05/0x15/0x39/0x06/0x21/0x22/0x1C).

| Layer | File | Role |
| --- | --- | --- |
| Register window | `drives/display/dsi/mre_reg_io.h/.c` | Bounded 32-bit window: bounds, read-back verified writes, bounded poll |
| DSI transaction | `drives/display/dsi/mre_dsi_packet.h` | Packet encoding, panel mode validation, timing maths (pure) |
| Panel data | `drives/display/dsi/mre_panel_nt36672a.h` | Descriptor, reset steps, backlight range, the panel-ID gate |
| DSI host | `drives/display/dsi/mre_dsi_host_sm6150.h` | Interface only — **blocked**, see below |
| Display clock | `drives/display/clock/mre_sm6150_dispcc.h` (pre-existing) | RCG/branch programming with policy gates |
| SPMI | `drives/bus/spmi/mre_spmi.h` | Byte transport; writes need a verified bus |
| PMIC arbiter | `drives/bus/spmi/mre_pmic_arb.h` | APID map, FDT parse, command/status, write authorization |
| Regulator | `drives/power/pmic/mre_pmic_regulator.h` | Descriptor-driven enable/voltage with rollback |
| GENI I2C | `drives/bus/i2c/mre_geni_i2c.h` | FIFO-mode master; attach needs two platform facts |
| Touch | `drives/input/touchscreen/mre_touch.h` | Frame → transform → queued input events |
| Core bridge | `drives/core/drive_catalog.h/.c` | Registers the set in `system/core/driver.c` |
| SDK surface | `framework/application_api/qeaf_drives_api.h`, `framework/core/qeaf_drives_api.c` | Versioned, pointer-free driver census (`qeaf_drives_api_v1`) |

## Driver core (the "lõi")

`system/core/driver.c` already owns registration, instances, states and
`vqeaf_driver_report()`. `drives/core/drive_catalog.c` is the single place that
joins the MRE drives to it, so they appear in the same registry as everything
else instead of a second bookkeeping layer.

The catalog registers six descriptors, one per drive that has a device-shaped
contract:

```
qcom,spmi-pmic-arb         -> mre_pmic_arb_probe()
qcom,geni-i2c              -> mre_geni_i2c_attach()
qcom,spmi-pmic-regulator   -> mre_pmic_regulator_get_enabled()
qcom,sm6150-dispcc         -> mre_sm6150_dispcc_read_clock()
mre,touchscreen            -> mre_touch_init()
novatek,nt36672a-shenchao  -> descriptor validation, then DEFER
```

Semantics, chosen to stay fail-closed:

- A probe runs only after the platform states the device's resources were
  verified. Without that it returns `VQEAF_DRIVER_DEFER`, so the instance is
  **DEFERRED** — not FAILED (nothing proved the hardware broken) and never
  READY. `vqeaf_driver_retry_deferred()` re-probes it once the resources exist.
- With verified resources the probe performs one real read-only transaction of
  that drive. Only a zero return is READY. A drive that answers
  `MRE_E_PERMISSION` (not yet authorized) defers; an unknown parent select or a
  failed transaction is a real failure.
- The nt36672a panel always defers: its own table records no ID command, so
  nothing can prove the fitted panel matches the table, and its init sequence
  must never be authorized by a guess.
- Binding the same device record twice is refused by the core (`-4`), so a bus
  rescan cannot multiply instances.

## SDK API

`qeaf_drives_api.h` freezes a v1 table — `size`, `abi_version`, `count`,
`info`, `report`. It is a census, not a handle: no pointer, instance id or
register leaves the core, and no call in it can change a driver's state.
`info()` returns a fixed-width, NUL-terminated, truncated record
(`name[48]`, `state[16]`, class, last error, attempts); `count()` and `info()`
agree on the meaning of "index n" by skipping REMOVED instances.

The table is independent of the QEAPP app-ABI chain (`qeaf_api.h` v2..v5), so
adding it cannot change an existing module's layout. **Not wired:** the Violet
app runtime does not yet hand this table to a `.qen` module through an import;
apps get it when that import is added, and that step is not claimed here.

## Where the code is built

| Build | What happens |
| --- | --- |
| `CMakeLists.txt` | `vqeaf_drives` static library (16 TUs incl. the real FDT parser and `system/core/driver.c`) with public include paths |
| Host gates | `vqeaf_mre_drives_smoke`, `vqeaf_drive_catalog_smoke` (added to `VQEAF_TEST_TARGETS` and `ctest`) |
| `scripts/build_violet.py` | Compiles the same set for `aarch64-linux-musl` into `build/violet/libvqeaf-drives.a` — **not linked into `/init`** |

Why the archive and not `/init`: on the production Violet path Linux owns
probe, MMIO, IRQ, DMA and the clock tree, and PID 1 must not map a controller
(the same boundary that keeps `slim_ngd_bringup.c` out of the image). The
archive is the deliverable a freestanding build consumes; compiling it on the
real target is what proves these contracts are freestanding-clean AArch64 code.

## Blocked, and why

| File | Blocker |
| --- | --- |
| `drives/display/dsi/mre_dsi_host_sm6150.c` | Programs the downstream `dsi.xml.h` register map, which is not in this repository. Inventing those offsets would write a real controller, so the register definitions are absent and the TU is in no build list. |
| `drives/display/dsi/mre_dsi_phy14_sm6150.c` | Same, for `dsi_phy_14nm.xml.h`. |
| `drives/display/dsi/mre_panel_identity.c` | Needs the host controller's DCS read. Its state machine is host-gated: the test supplies a fake read, so the ID gate is proven without the controller. |
| `mre_panel_identify_and_select()` | No compiled implementation yet, for the same reason. |

Adding either register map needs its vendor source recorded next to the values,
then the two TUs can join `vqeaf_drives` and the catalog.

## Evidence from this change

- `cmake --build build-native`: exit 0, `vqeaf_drives` and both new tests link
  with `-Wall -Wextra` and no new warning.
- `ctest --test-dir build-native`: **31/31 PASS**, including
  `vqeaf_mre_drives_smoke` (register window, DSI packets/timing, panel ID gate,
  DISP_CC, DSI register backend rollback, GENI I2C, SPMI + regulator rollback,
  PMIC arbiter, touch IRQ/transform/queue) and `vqeaf_drive_catalog_smoke`
  (DEFERRED→READY retry, duplicate refusal, panel never READY, SDK census).
- The arbiter parser ran against the in-tree Violet ABL device tree
  (`build/violet/violet-sm6150-abl.dtb`), so its windows come from the board:

  ```
  core=0xc440000/4352 chnls=0xc600000/33554432
  obsrvr=0xe600000/1048576 cnfg=0xc40a000/155648 ee=0 ch=0
  ```

- `python scripts/build_violet.py`: `/init` **12,166,504 bytes, 287 objects**
  (unchanged count: no MMIO driver entered PID 1), SHA-256
  `0c40bee0d4c1183c682d3079c37c569117207c0f2e7cc5b1ff41b702705a2c8c`;
  `libvqeaf-drives.a` **17 objects, 302,830 bytes**, 17 AArch64 ELF members
  (`e_machine 0x00b7`), SHA-256
  `5603d80cfeeb712920537f8b643d59c34df036ce584ec55963c91b421bf4e3ed`.

## Not claimed

No device was attached, no image was built or flashed, and no driver ran on the
handset. Nothing here changes the Wi-Fi/Bluetooth/camera blockers: those still
depend on Linux providers, firmware and calibration. The register values that
do appear (SPMI/PMIC encoding, DISP_CC offsets, GENI I2C offsets) came with the
drive sources; they are still unmeasured on this handset.
