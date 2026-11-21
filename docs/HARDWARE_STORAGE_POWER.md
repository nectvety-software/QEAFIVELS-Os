# VQEAF Hardware Abstraction & Storage Validation

## Hardware abstraction boundary
- The driver registry owns driver ABI, probe/remove lifecycle and instance state.
- Device discovery reports firmware/bus observations. A device is READY only after a matching driver probe succeeds; PRESENT means enumerated but no usable driver is bound.
- The block layer is the storage transport contract: logical sector size/count plus bounded read/write callbacks. Read-only devices omit write_sectors and writes fail explicitly.
- The x86_64 QEMU image now attaches a real (emulated) controller: the legacy VirtIO PCI block driver in `kernel/drivers/virtio_blk.c`. It probes `1AF4:1001`, negotiates a 256-entry virtqueue, services INTx, and registers a bounded block device. `storage.block` / `storage.virtio_blk` / `storage.fat16` become READY only after the driver probe and a successful FAT16 mount. This is still a QEMU-emulated controller, not phone storage hardware; RAM-disk callbacks remain test fixtures. Do not label phone storage READY until a target-specific controller driver has probed successfully.
- A second emulated controller is bound: the SATA/AHCI driver in `kernel/drivers/ahci.c` (QEMU ICH9, PCI `00:1F.2`). PCI scan covers all functions; the driver enables GHC.AE without a global HBA reset, detects ports by SSTS DET==3, and drives up to four ports with per-port command list/FIS/command-table pages. IDENTIFY and READ/WRITE DMA EXT are supported. Command-header DW0 uses the AHCI/EDK2 layout (CFL bits 0-4, write bit 6, PRDTL bits 16-31).
- QEMU attaches two SATA disks: the boot ESP and a 16 MiB MBR+FAT16 test disk. The ESP has no FAT16 partition and is skipped; the test disk is mounted read-only at `/storage`, `/storage/AHCI.TXT` is read, a last-sector scratch write/readback/restore is verified, the registered AHCI block device rejects writes, and unmount/remount is verified. `scripts/run_qemu.py` gates normal-boot assertions on `STORAGE: AHCI phase complete`.
- The block registrations are intentionally read-only in this phase. The drivers' write paths exist and are verified on scratch sectors, but no partition writer, update engine or rollback metadata exists yet, so the filesystems must not be modified.

## VFS / FAT16 validation
- Mount namespace is bounded and resolves the longest matching mount prefix.
- /datax must not match /data; /data/../escape is rejected.
- FAT16 is mounted through the storage service on a registered block device. Tests resolve /data/HELLO.TXT, walk the returned relative path, unmount, and verify the path becomes unavailable.
- Mounting an already-mounted path is rejected; a nested mount takes precedence and can be independently unmounted.
- FAT16 is still limited: 8.3 names and bounded node pool; not a general production filesystem. Writes are constrained and need additional crash-consistency testing before storing critical settings.

## Display default (portrait 9:16)
- The shipped reference geometry is portrait 9:16, like the QeafdropOs project default. `bootloader/core/display_mode.c` (shared by the Boot Manager and Recovery) prefers an exact 9:16 GOP mode, then programs a Bochs VBE 720x1280x32 mode on the QEMU linear framebuffer, then falls back to the firmware mode with a centred 9:16 shell column.
- Bochs VBE is the only path that works on this OVMF/QEMU reference: `-vga std` advertises 26 modes from 320x200 to 2560x1600, all landscape, and `virtio-vga,xres=720,yres=1280,edid=on` still reports 1280x800. The module therefore writes VBE_DISPI registers 0x1CE/0x1CF only after matching dispi ID 0xB0C0..0xB0C6, and reads XRES/YRES/BPP/ENABLE back before handing 720x1280, stride 720, BGR at 0x80000000 to the kernel.
- All QEMU screenshot gates now require the real 720x1280 framebuffer (`capture_screen.py`, `test_input_power.py`, `test_recovery_ui.py`); `run_qemu.py` requires `DISPLAY: portrait 9:16` in the normal boot and `portrait base=` in Recovery.
- Boundary: this is a QEMU VGA hardware mode, not a phone panel driver. There is no backlight, panel power rail, touch digitizer, rotation sensor or auto-rotate setting. Firmware without a portrait GOP mode and without Bochs VBE keeps its landscape mode and gets the centred 9:16 column only.

## Screen suspend / wake validation
- The power manager does not advance timeout while suspended.
- Resume must clear suspended state and record fresh user activity before timeout resumes.
- Host tests cover ACTIVE -> DISPLAY_OFF, suspend freeze, resume/activity wake, and a subsequent timeout cycle.
- End-to-end QEMU proof (`scripts/test_input_power.py`): the guest polls COM1, so the test attaches a serial socket chardev and sends real bytes. After unlock it navigates the real phone shell, selects "Tat man hinh" in quick settings, and logs `PHONE: sleep` and `POWER: display=off`; a screendump confirms a fully black framebuffer (non-black bytes=0). Any key then logs `POWER: wake from display-off` / `POWER: display=active` and the shell is restored. Long Power still opens the drawn menu overlay over the shell, wasd/e move and confirm Sleep, and Power Off halts QEMU.
- `power_menu.png`, `screen_asleep.png` and `screen_awake.png` are saved under `build/screens` together with the phone shell captures.

## Phone shell input map
- Lock: E/Enter unlocks; L locks from any screen.
- Home: W/A/S/D move the launcher grid, E/Enter opens the selected tile, B/Backspace returns to the lock screen, Q opens quick settings.
- Quick settings: W/S move, E/Enter activates (keep awake, timeout, display off, lock, power menu); Wi-Fi/Bluetooth rows are disabled because no radio driver exists.
- Apps: B/Backspace returns to the launcher; Settings' screen-timeout and keep-awake rows are live.
- Power: P is a power tap, Shift+P is a long press (opens the power menu); volume up/down still map to the legacy volume keys. The power menu consumes volume keys while open, exactly like hardware volume rockers.
- Everything above is a serial stand-in for a keypad. There is no touch digitizer, PMIC power key or backlight driver.
- The current QEMU serial path is a test input source; physical wake GPIO/PMIC and panel backlight drivers are not implemented.

## System Update screen contract (next UI milestone)
The recovery UI will provide a dedicated Installing System Update screen with:
1. Package discovery and compatibility check.
2. Signature/hash verification.
3. Battery/power and free-space preflight.
4. Partition/slot selection and staged write.
5. Post-write hash verification.
6. Commit + reboot, or rollback on any failure.

Progress must be based on completed work units, not a fake timer. Until a signed update package, partition writer and rollback metadata exist, Apply Update remains non-destructive and reports that the updater is unavailable. Wipe Data/Cache remain disabled.

Implemented today: the renderer (`system/ui/update_screen.c`) draws the four contract stages, percent, and blocked/failed states; the recovery menu and the native shell key `i` show it with `available=0` because no updater service exists. There is no live install progress, because nothing installs yet.

## Validation command
    python scripts/build.py
    python scripts/test.py
    # focused runs
    python scripts/test_recovery_ui.py
    python scripts/test_input_power.py

Screenshots are written to `build/screens`; QEMU serial logs to `build/*-serial.log` and `build/qemu-*.log`.
