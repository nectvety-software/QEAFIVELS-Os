# Hello QEAPP example

This app demonstrates the structured scaffold, packaging-name management,
and app-local tests. Older flat runtime examples are not this scaffold.

## Layout

```text
hello-qeapp/
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
    releases/hello-qeapp.qeapp
    debug/hello-qeapp.qeapp
```

The manifest points to `src/main.qe` and `assets/icon.rgba`. The compiler
concatenates sorted `.qe` sources under `libs/` after the entry. The PNG is
an editable icon source; the runtime icon is raw 64x64 BGRA, 16384 bytes.
The app-local compiler is only a launcher for the shared
`sdk/compiler/qecompile.py`; `build.py` delegates to that launcher.

## Build from the repository root

Run from `D:/QEAFIVELS-Os`:

```text
python sdk/cli/qeaf.py build examples/hello-qeapp --config all
python sdk/cli/qeaf.py validate examples/hello-qeapp/build/releases/hello-qeapp.qeapp
python sdk/cli/qeaf.py validate examples/hello-qeapp/build/debug/hello-qeapp.qeapp
```

`package` uses the same signed pipeline as `build`:

```text
python sdk/cli/qeaf.py package examples/hello-qeapp --config release
```

CLI commands accept `--config release|debug|all`, defaulting to `release`.
To create a separate app rather than overwrite this example, run from the
repository root:

```text
python sdk/cli/qeaf.py create my-app --id org.example.myapp --package-name my-app
```

Both options are optional; defaults derive from the directory basename.
`create` refuses an existing project directory.

## Build and test from the app root

Run from `D:/QEAFIVELS-Os/examples/hello-qeapp`:

```text
python build.py --config all
python -m unittest discover -s tests -v
```

App-local `build.py` defaults to `all`. Outputs stay inside this app:

```text
build/releases/hello-qeapp.qeapp
build/debug/hello-qeapp.qeapp
```

Tests build in temporary directories and check manifests, scripts,
release/debug archives, signatures, package renaming, and invalid names.
They do not test physical hardware.

## Rename the package, not the app

`packaging/package.json` contains:

```json
{"format":"qeapp-packaging","format_version":1,"name":"hello-qeapp"}
```

Change its `name` to change the output basename, without `.qeapp`. For
example, `hello-demo` produces `build/releases/hello-demo.qeapp` and
`build/debug/hello-demo.qeapp`. Keep `manifest.id` (runtime identity) and
`manifest.name` (display name) unchanged when only renaming artifacts.

Names match `[a-z0-9]+(?:[-_][a-z0-9]+)*`, have at most 64 characters, and
cannot be Windows reserved names such as `con` or `nul`. Uppercase, spaces,
paths, extensions, and leading/trailing or repeated separators are invalid.

## Outside the repository: Windows PowerShell

If this app is copied to `D:\Apps\hello-qeapp`, select the SDK explicitly:

```powershell
$env:VQEAF_SDK = "D:\QEAFIVELS-Os\sdk\cli\qeaf.py"
Set-Location "D:\Apps\hello-qeapp"
python .\build.py --config all
python -m unittest discover -s tests -v
```

For builds only, the alternative from that app root is:

```powershell
python .\build.py --config all --sdk "D:\QEAFIVELS-Os\sdk\cli\qeaf.py"
```

External tests use `VQEAF_SDK`. Otherwise the launcher searches ancestor
directories for the SDK repository.

## Archive contents and limits

Release packs the compiled entry, assets, manifest, and `SIGNATURE`, not
packaging metadata, tests, compiler launchers, or build scripts. Debug adds
`debug/source-map.json` and original sources, but no debugger support.
Both use ZIP_STORED, deterministic ZIP timestamps, and development-only
HMAC signatures; no separate sign step is needed. Limits are 32 entries,
512 KiB per package, and 511 bytes of printed text including newlines.
Metadata/runtime ABI stays v1. These instructions are host-side only; they
make no flashing or hardware-support claims.
