# QEAFIVELS OS — Native QEAPP Installer

This document describes the Violet implementation of standalone \`.qeapp\`
installation. It is not a vendor APK installer and does not use a vendor PackageManager, ART, Zygote or vendor init.

## User flow

1. Build a package on a development PC with the QEAF SDK.
2. Copy the signed \`.qeapp\` file to the QEAFRESCUE volume, preferably
   \`/Install/Apps/\`.
3. Open **Trình cài đặt** from the QEAF app launcher. The installer scans the
   QEAFRESCUE volume recursively to three directory levels and lists packages.
4. Select **Cài**. The preview verifies the signature and manifest without
   changing the installed app registry.
5. Review app name, ID, version, icon and requested permissions.
6. Confirm **CÀI ĐẶT**. The package is read and verified a second time before
   it is committed.
7. The installed app appears under **Ứng dụng → Đã cài**. Tap its icon to
   launch it; long-press for information and uninstall.

The Files app has a second package-picker entry point. Both entry points call
\`install_prompt_path()\`, so there is one verification and confirmation path.

## Package and runtime

- Container: ZIP_STORED \`.qeapp\`.
- v1: bounded declarative QE language entry.
- v2: QEAF native \`.qen\` module, arm64 on Violet.
- Manifest: ID, display name, version, entry/runtime, architecture and
  permission declarations.
- Embedded icon: \`assets/icon.rgba\`, 64×64 BGRA.
- Integrity: HMAC-SHA256 development signature. Do not treat this as a
  production publisher trust model.
- Install/update identity: \`manifest.id\`; installing another version with
  the same ID replaces the registry entry.
- App registry: currently up to 24 installed entries.
- Per-app data: separate blob, maximum 64 KiB, preserved across update and
  deleted on uninstall.

## Persistence

The rescue volume is recreated at boot, so it is not the installed-app store.
The Violet appstore mirrors the complete package bytes and app-data blob into
two alternating catalog slots in the reserved tail of p77 (32–64 MiB). Each
slot carries a sequence number and CRC. The payload is synced before its
header is committed. Boot restore chooses the newest valid slot and invokes
the normal package verification/installation path for each entry.

The p77 offsets are Violet-specific. Confirm the actual partition size and
that this range is reserved before using the same layout on another device.
The installer must never write into the active recovery image.

## Main implementation

| Responsibility | Source |
| --- | --- |
| ZIP, manifest, signature, preview, install/update, registry | \`platform/violet/ui/vqeaf_qeapp.c\` |
| Native QEN validation | \`platform/violet/ui/vqeaf_qeapp_native.c\` |
| Native arm64 process runner and lifecycle | \`platform/violet/ui/vqeaf_qeapp_device.c\` |
| Installer confirmation, scan, app drawer, file picker | \`platform/violet/ui/vqeaf_pages.c\` |
| Durable package catalog | \`platform/violet/appstore.c\` |
| Boot restore | \`platform/violet/main_violet.c\` |
| Host SDK and package validation | \`sdk/cli/qeaf.py\`, \`sdk/compiler/\` |

## Verification performed

- \`python -m unittest tests.test_qeapp tests.test_qeapp_native -v\`: 63 tests
  passed.
- \`python -u scripts/test_qeapp_runtime.py\`: PASS for signed package install,
  tamper rejection, bounded declarative output, native package inspection,
  invalid native module rejection, preview-without-commit and install-after-
  preview identity/permission consistency.
- \`python scripts/build_violet.py\`: PASS; \`build/violet/vqeaf-init\` linked
  (9,645,784 bytes, 265 objects).

These are host-side package/runtime tests and a successful Violet userspace
link. They do not prove a new image was flashed or that the complete install
flow has been exercised on the handset in this boot.

## Security boundary still required before third-party distribution

The native child process currently shares UID 0 with the shell. The API table
restricts what a well-behaved module is given, but it is not a kernel-enforced
sandbox against hostile native code. Native QEAPPs must be treated as trusted
code until process credentials, syscall filtering and filesystem isolation
are enforced by the kernel/runtime.

The embedded HMAC development key also cannot authenticate independent
publishers. A production package ecosystem requires asymmetric signatures,
publisher key management and an on-device public-key trust store.

## QEAPP Store UI (native Violet shell)

The app drawer and Home dock now open a dedicated **Chợ QEAPP** page. It lists
installed packages from the in-memory registry (restored from p77 at boot),
shows each app icon/name/version/runtime type, and opens the existing QEAPP
detail page. The detail page exposes manifest ID, version, runtime, signature
profile, declared permissions and app-data size, plus the existing uninstall
action. The Store page links to the native Installer for selecting a local
signed .qeapp update; the existing installer verifies the candidate and
replaces by manifest.id, preserving app data.

This is currently a **local package catalogue and manager**, not an online
remote catalogue: the OS browser/network/TLS backend is not available, so no
remote app listings, download counts, ratings or update feed are fabricated.
Permission strings are currently displayed as declarations only. There is no
per-app grant/revoke UI because the native process still runs with UID 0 and
permission bits are not enforced as a kernel security boundary. Do not present
those declarations as enforceable permissions until UID separation, syscall
filtering and filesystem isolation are implemented.
