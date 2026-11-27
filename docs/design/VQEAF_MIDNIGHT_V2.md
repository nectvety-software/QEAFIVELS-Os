# VQEAF Midnight V2 — Design System

## 1. Identity
- OS: **QEAFIVELS OS**
- Interface: **VQEAF Midnight V2**
- Slogan: **Your Freedom, Our OS**
- Logo: geometric stylised **V** with cyan → electric blue → violet gradient.
- Independent from Google: no GMS, no Play Store, no Firebase/FCM, no Google
  Search/Assistant/Gmail/Maps/Photos, no third-party fake logos.
- Approved service names: QEAF App Store, QEAF Search, QEAF Assistant,
  QEAF File Manager, QEAF Update, QEAF Account, QEAF Notification.

## 2. Colour tokens
| Token | Hex |
| --- | --- |
| Cobalt Wallpaper Top | `#0A3EB4` |
| Cobalt Wallpaper Bottom | `#4F9BFF` |
| Cobalt Deep | `#071D4A` |
| Surface | `#123C7A` |
| Surface Raised | `#1B4D96` |
| Cobalt Accent | `#5EA0FF` |
| Cyan Highlight | `#78D9FF` |
| Primary Text | `#FFFFFF` |
| Secondary Text | `#D4E6FF` |
| Light Background | `#EAF3FF` |
| Light Text | `#0B2447` |
| Glass Surface | white/blue at low opacity, gradient and hairline border |

Rules: cobalt-blue wallpaper and dark blue surfaces first; glass panels use
low-alpha white/blue over the procedural cobalt gradient. The renderer has no
backdrop blur, so glass is alpha plus gradient, border and shadow rather than a
claim of refraction. Rounded corners and soft shadows are consistent across
components; Material You can re-tint every role from one seed.

## 3. Typography
Ubuntu with full Vietnamese diacritics. Scale:
- Clock: 96 px bold (existing shell/PIN clock contract)
- Lock display clock: 160 px bold in portrait, with 56 / 34 / 28 px
  user-selectable alternatives and a 56 px landscape fallback
- Title: 34 px bold
- Body: 24 px regular
- Caption: 20 px regular
- Micro label: 16 px

## 4. Screens in the presentation board
1. **Home Launcher** — procedural cobalt gradient wallpaper, a glass live system
   widget with uptime/RAM/storage, four-column portrait app grid (eight compact
   columns in landscape), page dots, search pill and a rounded glass four-app
   dock. The reference's screen-time widget is intentionally not reproduced: no
   per-app foreground-time provider exists, so the shell never fabricates usage
   statistics.
2. **App Drawer** — translucent cobalt surface, QEAF Search bar, fixed
   four-column portrait / eight-column landscape icon grid, paging, and the
   installed QEAPP section. Unsupported entries stay visible with dimmed icons,
   an unavailable badge and an explanatory snackbar.
3. **Application Folder** — frosted glass panel, title, colourful app icons,
   expand/collapse, add-app button.
4. **Quick Settings** — compact glass pull-down panel; Wi-Fi, Bluetooth,
   Airplane Mode, Flashlight, brightness, volume, Battery Saver, Dark Mode,
   Rotation; each tile reports its real provider state or is visibly unavailable.
   Rounded-square quick tiles, glass brightness/volume cards and a conditional
   media card follow the cobalt reference without third-party branding.
5. **Notification Center** — glass cards, System / Apps / Security filters,
   timestamps, manage/clear actions. No Firebase/FCM.
6. **Clock & Widgets** — digital clock, world clock, alarms, stopwatch, timer,
   weather and battery widgets.
7. **System Settings** — Search Settings, Network & Connectivity, Display &
   Wallpaper, Sound & Vibration, Security & Privacy, Applications, Storage,
   Battery, System Update, About QEAFIVELS OS.
8. **About QEAFIVELS OS** — V logo, OS/interface names, slogan, version/build.

## 5. Implementation notes (repo reality)
- Current shell: `platform/violet/ui/` (LVGL 9), already has lock/home/apps/
  control/settings/recents/content pages, IME, themes, wallpaper, emoji font.
- Existing palette file: `platform/violet/ui/vqeaf_theme.c/.h`.
- Existing wallpaper: `vqeaf_wallpaper()` in `platform/violet/ui/vqeaf_theme.c`
  builds an original procedural cobalt gradient from native LVGL shapes. The
  generated `wallpaper.c` photo remains an unused historical asset.
- To implement Midnight V2 in code, update the palette and component styles in
  `vqeaf_theme.c`, not a parallel UI framework. Keep widgets and pages intact.
- Hardware states must remain read-only from real providers; UI never invents
  battery, Wi-Fi, signal, fingerprint, or GNSS state.
- Lock composition: Vietnamese date above the large translucent display clock,
  one glass battery/Wi-Fi/storage strip, the latest real notification card, a
  central unlock ring, and bottom torch/camera shortcuts. Missing provider data
  stays visibly unknown. Unsupported shortcuts remain visible with an explanation
  instead of pretending to work.
- Launcher composition: Home holds 16 real registry entries per portrait page
  (two pages for 30 entries), while the Apps drawer keeps eight entries per page
  (four pages). The default dock is Browser, Camera, Gallery and Music; drag or
  the info sheet can replace any slot in stock and Fluent themes. Storage and
  About route to their existing real pages. Unsupported entries remain visible
  with dimmed artwork and an explanatory unavailable state.
- System app artwork is an original 29-icon, 32×32 RGB565 pixel-art set created
  with VPEPixel. Editable `.vpe` files in `assets/icons/vqeaf_os_pixel/` are the
  source of truth; `scripts/make_vqeaf_pixel_icons.py` exports the embedded LVGL
  descriptors. VPEPixel is used as an editor/codec only; none of its sample art
  or third-party branding is reused. Invalid artwork IDs retain the glyph
  fallback.
- Security surfaces use the same cobalt/blue glass cards and Vietnamese labels.
  The Security hub offers a passive
  Wi-Fi audit and an offline CVE advisory browser; detail pages reuse the standard
  back header and severity colours. Missing providers remain visibly unavailable.
  These pages contain no attack controls, exploit payloads or third-party product
  branding; advertised encryption and a curated catalog are never presented as a
  complete security verdict.
- System bitmap app icons use a fixed 10 px outer corner radius in
  `vqeaf_icon_img()`, so Home, Apps and dock share the same shape. Pixel art keeps
  nearest-neighbor scaling and its original colours.
- Pixel artwork scales with nearest-neighbor filtering to preserve hard edges;
  QEAPP-provided ARGB8888 icons keep antialiasing. The shell never fabricates
  notification counts or other state for visual effect.

## 6. Presentation board artifact
The historical board `docs/design/VQEAF_MIDNIGHT_V2_BOARD.png` documents the
older navy/violet direction. The current Cobalt Glass shell is verified by the
host renders in `build/screens/phone-ui/`; those host images are not physical
device evidence.
