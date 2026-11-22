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
| Midnight Navy | `#070B20` |
| Deep Blue | `#101A3A` |
| Neon Violet | `#8157FF` |
| Electric Blue | `#287BFF` |
| Cyan Glow | `#25D9F2` |
| Primary Text | `#F4F5FF` |
| Secondary Text | `#AAB2D0` |
| Glass Surface | white at low opacity, layered blur |

Rules: dark surfaces first; glass panels use low-alpha white over Deep Blue;
accent glow is restrained, never neon overload. Rounded corners and soft
shadows are consistent across every component.

## 3. Typography
Ubuntu with full Vietnamese diacritics. Scale:
- Clock: 96 px bold
- Title: 34 px bold
- Body: 24 px regular
- Caption: 20 px regular
- Micro label: 16 px

## 4. Screens in the presentation board
1. **Home Launcher** — large clock, Vietnamese date, Midnight Aurora abstract
   wallpaper, weather + battery widgets, rounded-square icons, bottom dock,
   minimal dark status bar.
2. **App Drawer** — translucent deep blue-black, QEAF Search bar, 4-column
   icon grid, categories, smooth open animation.
3. **Application Folder** — frosted glass panel, title, colourful app icons,
   expand/collapse, add-app button.
4. **Quick Settings** — pull-down panel; Wi-Fi, Bluetooth, Airplane Mode,
   Flashlight, brightness, volume, Battery Saver, Dark Mode, Rotation; each
   tile has a clear on/off state.
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
- Existing wallpaper: `platform/violet/ui/wallpaper.c` (real photo, RGB565).
- To implement Midnight V2 in code, update the palette and component styles in
  `vqeaf_theme.c`, not a parallel UI framework. Keep widgets and pages intact.
- Hardware states must remain read-only from real providers; UI never invents
  battery, Wi-Fi, signal, fingerprint, or GNSS state.

## 6. Presentation board artifact
Generated board: `docs/design/VQEAF_MIDNIGHT_V2_BOARD.png` (8 phone mockups,
4×2 grid, dark navy background with neon violet lighting).
