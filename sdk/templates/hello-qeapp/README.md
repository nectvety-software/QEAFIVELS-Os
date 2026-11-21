# Hello QEAPP project

This is the generated app scaffold, not an older flat runtime example.
From the repository root (`D:/QEAFIVELS-Os`), create a new project:

```text
python sdk/cli/qeaf.py create my-app --id org.example.hello --package-name hello-qeapp
```

`--id` and `--package-name` are optional; defaults derive from the project
directory basename. `create` never overwrites an existing directory.

## Project layout

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

`manifest.json` uses `entry: "src/main.qe"` and `icon: "assets/icon.rgba"`.
The PNG is the editable icon source; the runtime icon is raw 64x64 BGRA
(16384 bytes, no header). `src/main.qe` is the entry script. Sorted `.qe`
library sources under `libs/` are concatenated after it. The declarative
language supports `app NAME`, `print "text"`, and `#` comments; it does not
execute arbitrary native code. Printed text is limited to 511 bytes total,
including newlines.

The app's `compiler/qecompile.py` launches the shared
`sdk/compiler/qecompile.py`; `build.py` delegates to that launcher. Keep the
SDK available rather than copying its compiler into the app.

## Names and packaging

The template's `packaging/package.json` is:

```json
{"format":"qeapp-packaging","format_version":1,"name":"hello-qeapp"}
```

Creation sets its name from `--package-name` or the directory-derived
default. Change this file to rename output artifacts, without the `.qeapp`
extension. `manifest.id` is runtime identity; `manifest.name` is the display
name; `packaging.name` is only the artifact basename. Renaming an artifact
must not change the app identity.

Names match `[a-z0-9]+(?:[-_][a-z0-9]+)*`, have at most 64 characters, and
must not be Windows reserved names such as `con` or `nul`. Spaces, uppercase,
paths, extensions, and leading/trailing or repeated separators are invalid.

## Build and test from the app root

Run in your generated project directory (for the command above,
`D:/QEAFIVELS-Os/my-app`):

```text
python build.py --config all
python -m unittest discover -s tests -v
```

`build.py` accepts `--config release|debug|all` and defaults to `all`.
For the packaging name above, outputs are:

```text
build/releases/hello-qeapp.qeapp
build/debug/hello-qeapp.qeapp
```

Tests build in temporary directories and check manifest and script validity,
release/debug contents, signatures, package renaming, and invalid names.

Alternatively, from the repository root, use the SDK CLI:

```text
python sdk/cli/qeaf.py build my-app --config all
python sdk/cli/qeaf.py package my-app --config release
python sdk/cli/qeaf.py validate my-app/build/releases/hello-qeapp.qeapp
```

CLI `build` and `package` are the same signed pipeline and default to
`release`. They write inside the app's `build/`, not beside the project.

## External projects: Windows PowerShell

For a project already created at `D:\Apps\my-app`:

```powershell
$env:VQEAF_SDK = "D:\QEAFIVELS-Os\sdk\cli\qeaf.py"
Set-Location "D:\Apps\my-app"
python .\build.py --config all
python -m unittest discover -s tests -v
```

A build can instead specify the SDK explicitly from the app root:

```powershell
python .\build.py --config all --sdk "D:\QEAFIVELS-Os\sdk\cli\qeaf.py"
```

External tests use `VQEAF_SDK`. Without an explicit path or this variable,
the launcher finds the SDK in an ancestor repository.

## What gets packaged

Release includes the compiled entry, assets, manifest, and `SIGNATURE`.
Packaging metadata, tests, compiler launchers, and build scripts are not
included. Debug adds `debug/source-map.json` and original sources; there is
no debugger support. Packages use ZIP_STORED, deterministic ZIP timestamps,
and development-only HMAC signatures, with limits of 32 entries and 512 KiB.
Metadata/runtime ABI remains v1. These host-side instructions do not claim
hardware support and do not flash or install anything on a device.
