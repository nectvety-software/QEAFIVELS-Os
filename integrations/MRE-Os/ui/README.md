# MRE OS User Interface

## Home shell foundation

The first native UI module is a freestanding C home-screen renderer for the Violet portrait display. It draws into a caller-owned RGB565 framebuffer and does not allocate memory, start Android services, or depend on Android/Linux userspace.

Current home view:
- MRE OS identity and clock/date fields supplied by the system.
- Device-status panel. Connectivity labels are driven only by supplied state; unknown hardware is shown as not verified.
- Four app launch tiles: Phone, Messages, Files and Settings.
- Bottom gesture/navigation affordance.
- Hit testing for the four app tiles and top quick-settings region.

## Interfaces

- `ui/include/mre_ui.h`: UI state, UTF-8 font callback, render and hit-test API.
- `ui/core/mre_ui.c`: RGB565 drawing and responsive 1080x2340 logical layout.
- The caller owns the framebuffer and must provide a valid buffer/stride.
- The font service owns UTF-8 decoding and glyph rasterization. No font asset is bundled yet.
- A Violet display driver must discover the real panel, DSI host, lane count, reset, regulators and mode before presenting frames.

## Validation

- Host UI test covers rendering entry, callback dispatch, touch hit-testing and invalid arguments.
- AArch64 object compilation validates the freestanding UI module.
- These checks do not validate the panel, DSI link, touch controller, refresh rate or backlight on a handset.

## Next UI integration

1. Implement a verified Violet MIPI-DSI panel driver using the live DTB and panel ID.
2. Add a font provider with Vietnamese UTF-8 glyph coverage (Ubuntu font family is a candidate after licensing and asset packaging checks).
3. Connect touch input through the MRE input HAL and dispatch hit-test actions to the application manager.
4. Add lock screen, notification shade, quick settings, settings app and system update/recovery screens.
5. Keep display and touch hardware status explicitly UNKNOWN until measured on the phone.
