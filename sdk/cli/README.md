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

## Rust native apps

Install a stable Rust toolchain and both bare-metal targets used by the native
compiler:

```text
rustup target add x86_64-unknown-none aarch64-unknown-none
python sdk/cli/qeaf.py doctor
```

`doctor` reports the selected `rustc`, the two Rust targets, and whether each is
installed. `RUSTC` may name an explicit compiler executable. Cargo is optional;
the package compiler invokes `rustc` directly and does not require crates.io.
The API's direct host tests can be run with
`cargo test --manifest-path sdk/rust/Cargo.toml`.

Create, build, inspect, and run an x86_64 Rust app from the repository root:

```text
python sdk/cli/qeaf.py create my-rust-app --rust --arch x86_64 --id org.example.rustapp --package-name hello-rust
python sdk/cli/qeaf.py build my-rust-app --config release
python sdk/cli/qeaf.py validate my-rust-app/build/releases/hello-rust.qeapp
python sdk/cli/qeaf.py preview my-rust-app/build/releases/hello-rust.qeapp
python sdk/cli/qeaf.py simulate my-rust-app/build/releases/hello-rust.qeapp
```

Omit `--arch` or use `--arch arm64` to target Violet. Build an arm64 package with
the same `build` and `validate` commands after changing the scaffold/manifest
architecture; the host simulator executes x86_64 only. Use `--config all` to
produce both `build/releases/<name>.qeapp` and `build/debug/<name>.qeapp`.
From a generated project root, `python build.py --config all` is equivalent.

The generated project contains a private copy of the canonical
[`sdk/rust`](../rust/src/lib.rs) API, so external projects do not depend on the
repository at build time. `Cargo.toml` is valid metadata for editors and Cargo
tools, but `cargo build` is not the `.qeapp` packaging path. The app entry is
`src/main.rs`; it uses `#![no_std]` outside the compiler's combined crate,
`panic = "abort"`, `QeappApp`, and `qeapp_app!(YourApp)` to export the C ABI
lifecycle functions.

The compiler in [`sdk/compiler/qenrust.py`](../compiler/qenrust.py) builds one
combined `no_std` binary crate: canonical SDK sources become a private `qeapp`
module and `src/main.rs` becomes a private `app` module. It emits one PIC ELF
object for `x86_64-unknown-none` or `aarch64-unknown-none`, checks the lifecycle
exports, and passes the object to [`qenlink.py`](../compiler/qenlink.py). The
result is the same validated `.qen` used by C apps; there is no Rust runtime,
dynamic loader, libc, allocator, threading, TLS, unwinding, or direct OS syscall
layer. Undefined native symbols and unsupported relocations are build errors.

The default template selects ABI v5 (`ApiV5`). ABI v2-v4 remain supported when
the application type and manifest agree. The frozen table sizes on both 64-bit
targets are 48, 64, 72, and 80 bytes respectively. All framework calls remain
unsafe C-ABI calls through the validated API table, and permissions still come
from `manifest.json`.

### Rust icon assets

Keep the manifest icon at `assets/icon.rgba`: exactly 64x64 BGRA bytes. To update
it from a PNG without introducing dark transparent-edge fringes, run the shipped
converter and rebuild:

```text
python scripts/png_to_qeapp_icon.py my-rust-app/assets/icon.png my-rust-app/assets/icon.rgba
python sdk/cli/qeaf.py build my-rust-app --config release
```

The converter preserves alpha and propagates color from opaque pixels into
transparent and partially transparent edge pixels. Package bytes remain the
source of app identity; display-only normalization happens in the launcher for
legacy packages.

## Inspect without installing

```text
python sdk/cli/qeaf.py preview my-app/build/releases/hello-qeapp.qeapp
```

`preview` validates a local signed `.qeapp` and prints deterministic JSON.
It accepts declarative v1 and structurally validates native v2 modules without
executing them. Success includes `package_sha256` for the exact package bytes,
`signature_verified: true`,
`signature_scheme: "development-hmac-sha256"`, and
`publisher_authenticated: false`. Failure prints `ERROR [<CODE>]: <message>`
and exits 1.

The reusable API is:

```python
from sdk.qeapp import QEAPPError, preview_package

try:
    info = preview_package("my-app/build/releases/hello-qeapp.qeapp")
except QEAPPError as error:
    print(error.code, error.message)
```

This SDK is inspection-only. It does not install packages, modify device
state, or provide a fake desktop package manager. On-device mutations use the
public C API in `system/core/include/vqeaf/qeapp_package.h`; `OK` means a
verified durable p77 save, while `OK_SESSION_ONLY` means RAM-only success.

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
and 511 bytes of printed text including newlines. A declared runtime icon must
be `assets/icon.rgba` with exactly 64×64×4 bytes. Native v2 manifests declare
exactly one architecture; the scaffold's `.qen` stub uses the manifest ABI and
defaults `min_api` to that ABI. Metadata/runtime ABI stays
v1. Signing uses a shared development HMAC key, not production trust.

See [QEAPP format v1](../../docs/QEAPP_FORMAT.md). These are host-side build
and test instructions, not flashing instructions or hardware-support claims.
