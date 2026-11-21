# QEAF SDK CLI

## Create and build

Run from the repository root (`D:/QEAFIVELS-Os`):

```text
python sdk/cli/qeaf.py create my-app --id org.example.hello --package-name hello-qeapp
python sdk/cli/qeaf.py build my-app --config all
python sdk/cli/qeaf.py validate my-app/build/releases/hello-qeapp.qeapp
python sdk/cli/qeaf.py validate my-app/build/debug/hello-qeapp.qeapp
```

`create <project-path> [--id org.example.app] [--package-name app-name]`
creates a new scaffold only; it refuses an existing directory. Defaults are
derived from the project directory basename. Older flat runtime examples are
not the new scaffold; see [the template](../templates/hello-qeapp/README.md).

`build <project-path>` and `package <project-path>` use the same signed
compiler pipeline. Both accept `--config release|debug|all`, defaulting to
`release`. For example, from the repository root:

```text
python sdk/cli/qeaf.py package my-app --config release
```

Outputs stay inside the project, never beside it:

- `build/releases/<name>.qeapp`
- `build/debug/<name>.qeapp`

The shared compiler is `sdk/compiler/qecompile.py`. Each generated project's
`compiler/qecompile.py` is an SDK launcher, not a compiler copy; `build.py`
delegates to it. Builds already use ZIP_STORED and add the development HMAC
signature. No separate `--stored` or `sign` step is needed.

## Package name management

`packaging/package.json` contains only this versioned packaging metadata
(the example above selects `hello-qeapp`):

```json
{"format":"qeapp-packaging","format_version":1,"name":"hello-qeapp"}
```

- `manifest.id`: runtime identity, such as `org.example.hello`.
- `manifest.name`: display name.
- `packaging.name`: output basename, without `.qeapp`.

Edit only `packaging/package.json` to rename future build outputs without
changing app identity. Names must match `[a-z0-9]+(?:[-_][a-z0-9]+)*`, be at
most 64 characters, and not be Windows reserved names (for example `con`
or `nul`). Uppercase, spaces, path separators, extensions, and empty names
are invalid. Renaming the project directory does not replace this metadata.

## App-local builds and tests

From the generated app root (`D:/QEAFIVELS-Os/my-app`):

```text
python build.py --config all
python -m unittest discover -s tests -v
```

App-local `build.py` defaults to `all`, unlike the CLI's `release` default.
Tests build in temporary directories and check manifests, scripts, release
and debug packages, signatures, package renaming, and invalid names.

For an external project, use Windows PowerShell (choose an existing parent
for the new project directory):

```powershell
Set-Location "D:\QEAFIVELS-Os"
python .\sdk\cli\qeaf.py create "D:\Apps\my-app" --id org.example.hello --package-name hello-qeapp
$env:VQEAF_SDK = "D:\QEAFIVELS-Os\sdk\cli\qeaf.py"
Set-Location "D:\Apps\my-app"
python .\build.py --config all
python -m unittest discover -s tests -v
```

For a build alone, an explicit SDK path is an alternative:

```powershell
python .\build.py --config all --sdk "D:\QEAFIVELS-Os\sdk\cli\qeaf.py"
```

Keep `VQEAF_SDK` set for external app tests. Without an explicit SDK path or
that variable, the launcher searches ancestor directories for the SDK repo.

## Package boundaries

Release packs the compiled entry, assets, manifest, and `SIGNATURE`, not
packaging metadata, tests, compiler launchers, or build scripts. Sorted
`.qe` sources under `libs/` are concatenated after the entry. Debug also includes
`debug/source-map.json` and original sources; it does not enable a debugger.
ZIP timestamps are deterministic. Limits are 32 entries, 512 KiB per package,
and 511 bytes of printed text including newlines. Metadata/runtime ABI stays
v1. Signing uses a shared development HMAC key, not production trust.

See [QEAPP format v1](../../docs/QEAPP_FORMAT.md). These are host-side build
and test instructions, not flashing instructions or hardware-support claims.
