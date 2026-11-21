# Terminal QEAPP

Native QEAPP for VQEAF OS: a dark terminal dashboard drawn into the 720x1560
app canvas with ABI v5 pixel blits and the weighted text API. It is not a
shell: the runtime forwards no input, so the prompt is explicitly view-only.

```powershell
python sdk\cli\qeaf.py build examples\terminal-qeapp --config release
python sdk\cli\qeaf.py validate examples\terminal-qeapp\build\releases\terminal-qeapp.qeapp
```

Copy the package to the QEAFRESCUE volume, install it (Trình cài đặt or
`install TERMINAL.QEAPP`), then open **Terminal** in Ứng dụng → Đã cài and press
**Chạy trên thiết bị**. The rescue verb `qeapp TERMINAL.QEAPP` runs the same
package without the UI.

## What is on screen

The dashboard reports values measured at run time through the API table:

- API table size and ABI version, read from the table passed to `create()`;
- `request_permission()` results for display, input, storage and media access;
- a `read_file()` probe that reports the runtime's real refusal (the device
  runtime grants no storage yet).

Nothing is a hardcoded status, and no file list is invented.

## Limits, stated plainly

- **No input.** The device runtime
  (`platform/violet/ui/vqeaf_qeapp_device.c`) never calls `on_input`, so the
  prompt cannot be typed into on the phone. The app draws a boot report and a
  prompt; it does not pretend to accept keys.
- **Small glyphs.** Text uses the runtime's 8x8 ASCII face, while panels and
  accents use the ABI pixel blitter.
- **ASCII only.** The device raster maps every byte >= 128 to `?`, so the UI
  text is ASCII on purpose.

## arm64 build notes (learned on hardware)

- `qenlink` resolves only PC-relative references: pointer tables and
  `static const` arrays of pointers are rejected, and decimal output is built by
  repeated subtraction instead of `%`/`/`.
- The AArch64 QEMU gate boots with the MMU off, where memory is Device type and
  an **unaligned access faults**. clang vectorises byte fills into 8-byte
  stores, so the buffer loops here write through `volatile` and the string
  buffer is `aligned(16)`; without that the module dies with an alignment Data
  Abort mid-`start()` (the failure looked layout-dependent because it depended
  on where the buffer landed in `.bss`).
