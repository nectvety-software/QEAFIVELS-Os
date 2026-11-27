# QEAPP package formats and installer API

A `.qeapp` is a ZIP_STORED container, not an APK. Format v1 carries a bounded
declarative `.qe` script. Format v2 carries a QEAF-owned `.qen` native module;
host inspection only validates its structure and never maps or executes it.
The generated project layout and packaging metadata do not change either
manifest format.

## Project inputs and names

New projects use this structure:

```text
<project>/
  manifest.json
  src/main.qe
  libs/greeting.qe
  assets/icon.png
  assets/icon.rgba
  packaging/package.json
  tests/test_project.py
  compiler/qecompile.py
  build.py
  README.md
  build/
    releases/<name>.qeapp
    debug/<name>.qeapp
```

`create` makes empty output directories; builds generate the `.qeapp` files.

The required manifest fields remain `format` (`qeapp`), `format_version`
(`1`), `id`, `name`, `version`, `min_os`, `entry`, `architecture`,
`permissions`, and `dependencies`. The scaffold sets `entry` to
`src/main.qe` and `icon` to `assets/icon.rgba`. The runtime icon is raw
64x64 BGRA (16384 bytes); `assets/icon.png` is its editable source. A declared
icon must use the canonical `assets/icon.rgba` path and that entry must exist.
Format v2 requires exactly one declared architecture; Violet accepts arm64.
Older flat runtime examples are not the generated scaffold.

`packaging/package.json` is build-only metadata, with these required fields:

```json
{"format":"qeapp-packaging","format_version":1,"name":"hello-qeapp"}
```

Keep the three names distinct:

| Field | Purpose |
| --- | --- |
| `manifest.id` | Runtime app identity, e.g. `org.example.hello` |
| `manifest.name` | User-visible display name |
| `packaging.name` | Artifact basename, e.g. `hello-qeapp` |

To rename future outputs, edit only `packaging/package.json`. Do not add
`.qeapp` to its name or change runtime identity. Names must match
`[a-z0-9]+(?:[-_][a-z0-9]+)*`, have at most 64 characters, and not be
Windows reserved names (such as `con` or `nul`). This excludes uppercase,
spaces, paths, extensions, and empty segments.

## Compilation and archive contents

`sdk/compiler/qecompile.py` is the shared compiler. A generated project's
`compiler/qecompile.py` is a launcher for that SDK compiler, and `build.py`
delegates to the launcher.

The compiler concatenates the entry followed by sorted `.qe` sources under
`libs/`. The runtime's declarative subset is `app NAME`, `print "text"`,
and `#` comments. The total print budget is 511 bytes including newlines.

Release archives contain:

- `manifest.json`.
- The compiled script at the manifest entry path (`src/main.qe` in the scaffold).
- Assets under `assets/`.
- `SIGNATURE`.

Library text is merged into the entry rather than loaded as separate runtime
modules. Debug archives additionally contain `debug/source-map.json` and
original sources. These are inspection artifacts, not debugger support.
Neither configuration packs `packaging/`, `tests/`, `compiler/`, `build.py`,
or other build-only sources.

All entries use ZIP_STORED (uncompressed), with deterministic ZIP timestamps.
Packages are bounded to 32 entries and 512 KiB; debug data counts toward
these limits too.

## Development signature

Every entry except `SIGNATURE` is covered by HMAC-SHA256 with the development
key `VQEAF-QEAPP-DEV-KEY-2026`. The signed stream starts with the header
`VQEAF-QEAPP-V1\0`, then each entry's name, NUL, little-endian size, and bytes,
in name order. Here `\0` denotes one NUL byte, not two printable bytes.
`SIGNATURE` contains 64 lowercase hexadecimal characters.

Both release and debug builds are signed automatically; debug sources and
the source map are covered too. This is a shared-key development profile,
not a public-key signature or a production distribution trust model.
`qeaf sign <package>` can re-sign an existing archive with this development
key; normal builds already include the signature.

## Runtime installation

On Violet, the native Installer scans QEAFRESCUE for `.qeapp` packages
(recursively, up to three directory levels). Put packages in
`/Install/Apps` or another folder on the QEAFRESCUE volume, open
**Trình cài đặt**, and select **Cài**. Files also exposes a package-picker
entry point. Both routes use the same confirmation sheet:

1. Verify the package signature and parse the manifest without changing the
   installed registry.
2. Show the actual app name, version, ID, icon and requested permissions.
3. On confirmation, call `vqeaf_qeapp_package_install_verified()` with the
   SHA-256 returned by preview. It re-reads and verifies the exact package
   bytes, rejects a substituted package, then installs or updates by
   `manifest.id`.
4. Show the installing state and a success/error result. **Mở** opens the
   installed app.

Installed apps appear under **Ứng dụng → Đã cài** with their embedded icon.
Tap an icon to launch; long-press opens app information and uninstall.
Installing a newer package with the same `manifest.id` updates that app
instead of creating a duplicate. Uninstall removes the package and its
per-app data.

### Public package API v1

The LVGL-independent C contract is
`system/core/include/vqeaf/qeapp_package.h`. It exposes package preview,
install, update-by-ID, enumeration, lookup, and uninstall without exposing the
LVGL registry internals.

- `vqeaf_qeapp_package_preview()` verifies and parses without mutating state;
  `info.package_hash` is the SHA-256 of the exact verified package bytes.
- `vqeaf_qeapp_package_install()` re-verifies the bytes before committing.
  `result.updated` is `0` for a new ID and `1` for an existing-ID update.
- `vqeaf_qeapp_package_install_verified()` accepts only the digest returned by
  a preceding preview and rejects changed bytes before any registry mutation.
- `vqeaf_qeapp_package_find()` returns a zero-based installed index or `-1`.
- `OK` means the mutation succeeded and the durable p77 catalog was written
  and read back successfully. `OK_SESSION_ONLY` means only the in-memory
  mutation succeeded; the UI must not describe it as durable.
- `persisted` is `-1` when not applicable, otherwise `0` or `1`.
  `updated` is `-1` for non-install operations.

The desktop UI renderer has no QEAPP storage backend: preview, install, and
uninstall return `UNSUPPORTED` and leave the seeded registry unchanged. It is
a layout harness, not a desktop package manager. Separate host runtime tests
execute the real verifier with a failing persistence stub and therefore
expect `OK_SESSION_ONLY`, not `OK`.

### Durable install store (Violet)

QEAFRESCUE is RAM-backed, so the installer mirrors signed package bytes and
per-app data into the two-slot app catalog in the reserved tail of
`/dev/mmcblk0p77` (32–64 MiB). New records are keyed by `manifest.id`, not by
the package filename. Each installed record retains the exact verified bytes,
so later data/permission saves do not reread a mutable QEAFRESCUE path and do
not require a FAT volume. Each slot has a sequence number and CRC. A save
writes and syncs the payload before committing its header; the previous valid
slot remains available if power is lost during an update. At boot, QEAF
restores the newest valid catalog and sends every package through the same
signature/manifest validation path again. Legacy filename-keyed records are
accepted only when their signed metadata proves the expected identity; their
data and grants are inherited after validation and the next save migrates the
key to `manifest.id`. Per-app data is capped at 64 KiB; the in-memory app
registry currently exposes up to 24 apps.

A failed save leaves the app usable for the current boot but does not promise
that it will survive reboot. The installer and uninstall notifications report
that session-only outcome separately. Before deployment on a different
partition map, verify that p77 exists and that its 32–64 MiB range is genuinely
reserved for QEAF app storage; do not assume those offsets are portable to
another device or partition layout.

### Runtime and trust limits

QEAPP v1 is a bounded declarative runtime. QEAPP v2 can carry a QEAF-owned
`.qen` native module (currently arm64 on Violet), validated and launched
through the device runner. The child process has a separate address space and
a five-second watchdog, but currently shares the shell's UID 0; the API table
is a capability boundary, not a kernel-enforced security sandbox. Treat native
packages as trusted code.

The current HMAC-SHA256 development signing key is embedded in the image.
It detects accidental/tampered package bytes against the development build,
but it is not a production publisher identity or a secure third-party trust
root. A production app ecosystem still needs asymmetric signatures with a
private signing key kept off-device, plus kernel-enforced process isolation.

## Host SDK inspection

The reusable Python API is read-only:

```python
from sdk.qeapp import QEAPPError, preview_package

try:
    metadata = preview_package("my-app/build/releases/hello-qeapp.qeapp")
except QEAPPError as error:
    print(error.code, error.message)
```

Successful metadata includes the manifest identity, runtime, architecture,
permissions, dependencies, icon size, package path/size, `package_sha256`,
`signature_verified: true`, `signature_scheme: "development-hmac-sha256"`, and
`publisher_authenticated: false`. Inspection validates ZIP structure, CRC,
manifest/script or native-module structure, exact signature, and size limits.
It does not install, modify, launch, or simulate the package.

The equivalent CLI is:

```text
python sdk/cli/qeaf.py preview my-app/build/releases/hello-qeapp.qeapp
```

It prints deterministic JSON on success. Failure prints
`ERROR [<CODE>]: <message>` and exits nonzero. There is intentionally no host
desktop installer command.

## Build and test commands

From the repository root (`D:/QEAFIVELS-Os`):

```text
python sdk/cli/qeaf.py create my-app --id org.example.hello --package-name hello-qeapp
python sdk/cli/qeaf.py build my-app --config all
python sdk/cli/qeaf.py package my-app --config release
python sdk/cli/qeaf.py validate my-app/build/releases/hello-qeapp.qeapp
python sdk/cli/qeaf.py preview my-app/build/releases/hello-qeapp.qeapp
```

`create` refuses existing directories; optional ID and package-name defaults
come from the directory basename. CLI `build` and `package` use the same
signed pipeline. `--config release|debug|all` defaults to `release`.
No separate `--stored` or signing step is required.

Outputs are `<project>/build/releases/<name>.qeapp` and
`<project>/build/debug/<name>.qeapp`, never sibling files of the project.
From the app root (`D:/QEAFIVELS-Os/my-app`):

```text
python build.py --config all
python -m unittest discover -s tests -v
```

App-local builds default to `all`. Tests build in temporary directories and
check manifests, scripts, release/debug packages, signatures, renaming, and
invalid package names.

For an external project already at `D:\Apps\my-app`, use PowerShell:

```powershell
$env:VQEAF_SDK = "D:\QEAFIVELS-Os\sdk\cli\qeaf.py"
Set-Location "D:\Apps\my-app"
python .\build.py --config all
python -m unittest discover -s tests -v
```

A build also accepts `--sdk "D:\QEAFIVELS-Os\sdk\cli\qeaf.py"`; external
tests use `VQEAF_SDK`. Without either override, the launcher searches for
the SDK in an ancestor repository.

Repository-level verification commands:

```text
python -m unittest tests.test_qeapp_sdk tests.test_qeapp_native -v
python scripts/test_qeapp_runtime.py
python scripts/test_violet_shell.py
python scripts/render_phone_ui.py
python scripts/package_violet.py
```

These are host-side packaging, runtime, UI, build, and image-gate
instructions; they do not establish physical installation or perform
flashing.
