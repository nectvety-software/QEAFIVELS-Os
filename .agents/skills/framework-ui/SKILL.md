# Skill: VQEAF Framework and Interface

## Framework modules
Application lifecycle, event loop, graphics, input, touch gestures, windows, compositor, widgets, layout, typography, animation, accessibility, notifications, audio, storage, network and theme APIs.

## Design system
Original VQEAF identity: graphite surfaces, restrained teal/cyan accent, readable typography, rounded geometry, consistent spacing, adaptive layouts, accessible contrast and low-power transitions. Do not clone vendor mobile-platform or iOS screens.

## UI rules
- Render service state; do not invent device state.
- Every control must invoke a real service/API or be visibly disabled with an explanation.
- Support keyboard, touch and accessibility focus.
- Separate screen blanking from system suspend and power-off.
- Persist settings via Settings Service, not UI-local files.
- Avoid animations that prevent idle timeout or drain battery.

## Screen timeout behavior
Options are exactly 15 seconds, 1 minute, 5 minutes, 30 minutes and Never. Use a monotonic clock, reset only on genuine user activity, honor explicit wake locks and emergency/call policy, blank display and restore lock screen on wake. Add fake-clock tests.

## Responsive behavior
Support phone portrait first, landscape where useful, variable density, safe areas, notches and large text. Degrade gracefully on low memory and low refresh hardware.

## Violet shell gestures (implemented)
`platform/violet/ui/vqeaf_ui.c` plus `vqeaf_pages.c/.h`:
- Shell gestures only on system chrome/pages (Lock/Home/Apps/Control/Settings/Recents). App content keeps its own swipes; overlays (IME, USB dialog, app-info sheet) block shell gestures via `vqeaf_pages_app_info_visible()`.
- Bottom edge: swipe up = Home, hold then swipe up = Recents. Top edge: swipe down = Control. Side edge: swipe = Back; hold + swipe = switch recent tasks; upper side = Control.
- `LV_OBJ_FLAG_GESTURE_BUBBLE` on status bar/dock/pill identifies gesture origin; a 10 ms timer tracks the press edge (`input_track_cb`) because LVGL exposes only the current point on GESTURE.
- Virtual Java-style keys (Back/Home/Recents) toggle from Control Center, stored as `nav.virtual_keys` (session-only on violet).

## PIN lock (implemented, session-only)
`vqeaf_lock_unlock()` in `vqeaf_pages.c` is the single gate for both swipe and tap on the lock screen: with no PIN it opens Home, with a PIN it opens `VQEAF_PAGE_PIN` (numeric keypad). PIN lives in the Settings service (`lock.pin`); violet has no prefs partition, so reboot clears it. Security page reports fingerprint / pocket-touch-lock / haptic as unsupported instead of faking them.

## Soft keyboard with real emoji (`vqeaf_ime.c`)
Independent top-layer IME window with weighted rows, responsive metrics and normal/split/one-hand/floating/extract modes. Emoji: `IME_EMOJI` / `IME_EMOJI_MODE` actions, 50 emoji entries in `emoji_table`, three emoji rows plus a control row; the 😀 toggle sits next to `?123` (also in symbol layout). Emoji are inserted as real 4-byte UTF-8 via `lv_textarea_add_text` — never placeholders.
Font `platform/violet/ui/fonts/vqeaf_emoji_28.c` is generated from **Segoe UI Emoji** (`scripts/generate_emoji_font.ps1`): 88 glyphs, U+2705..U+1F963, two cmap blocks so uint16 offsets do not overflow. Build lists must include this `.c` in `build_violet.py`, `build_mt689x.py` and `scripts/render_phone_ui.py`; `vqeaf_fonts.h` declares `vqeaf_emoji_28`. Gate: `python scripts/test_ime.py` (covers emoji insert/backspace/ABC return) must be ALL OK.
