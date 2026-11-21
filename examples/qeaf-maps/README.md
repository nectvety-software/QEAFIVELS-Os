# QEAF Maps — Native QEAPP example

A native QEAPP v2 package using framework API v3. The sample draws an
offline, illustrative neighborhood map inspired by the supplied Maps
screenshot: pale street blocks, park area, red place pins, labels and a
bottom SHOW LIST panel.

## What this sample demonstrates

- Native ARM64 .qen module and QEAPP lifecycle (create/start/pause/resume/stop/destroy).
- Display output through qeaf_app_api_v3.blit_pixels (RGB565) and draw_text.
- No libc, raw syscalls, direct framebuffer access, API của vendor or APK.
- A deterministic static map canvas suitable for the current QEAPP rendering API.

## Explicit limitations

This is a visual sample, not a production navigation application. It contains
illustrative roads and sample POIs only. It does not currently provide GPS,
live location, touch interaction, pan/zoom, map tiles, geocoding, routing,
traffic, online search or a live POI database. The locate icon is decorative
and must not be interpreted as a GPS fix.

The QEAPP API currently exposes display and file operations, not a location,
network-map or pointer-event service. Those should be added as explicit,
permission-checked framework capabilities before implementing live map
features. The manifest requests only ui.display.

## Build

From D:\QEAFIVELS-Os:

    python sdk\cli\qeaf.py build examples\qeaf-maps --config all
    python sdk\cli\qeaf.py validate examples\qeaf-maps\build\releases\qeaf-maps.qeapp
    python sdk\cli\qeaf.py simulate examples\qeaf-maps\build\releases\qeaf-maps.qeapp

The ARM64 module is built for QEAF's native device runtime. A successful host
package build/validation is not proof of execution on a Redmi Note 7 Pro.
qeaf simulate uses the host simulator; it is not a phone boot/flash test.

## Suggested follow-on framework work

1. Add location.current and location.precise capabilities with runtime grant checks, revocation and audit events.
2. Add a pointer/touch event callback to a versioned framework ABI.
3. Add bounded map tile storage and a provider interface; keep network access behind an explicit network.connect grant and TLS validation.
4. Add route/geocoding providers only after location and network APIs enforce effective grants at the device boundary.
5. Keep native process privilege separation, seccomp and filesystem/device isolation as release blockers until verified on the target kernel.
