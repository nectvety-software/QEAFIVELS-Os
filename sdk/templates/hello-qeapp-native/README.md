# Hello Native QEAPP project

This is the native QEAPP ABI v5 scaffold. `src/main.c` is a real freestanding
Hello app that talks to the OS only through the framework API v5 table.
`qeaf build` compiles it to a `.qen` module; `qeaf simulate` runs it on the host.

From the repository root (`D:/QEAFIVELS-Os`), create a new project:

```text
python sdk/cli/qeaf.py create my-app --native --id org.example.hello --package-name hello-native
```

`--arch` selects `arm64` (default) or `x86_64`. `create` never overwrites an
existing directory. The host simulator executes `x86_64` modules; `arm64`
modules are built and validated for the device runtime (Phase 4).

## Project layout

```text
<project>/
  manifest.json              # format_version 2, runtime "native", abi_version 5
  src/main.c                 # freestanding PIC app (create/start/.../destroy)
  native/<arch>/app.qen      # QEAF native module (64-byte header + payload)
  assets/icon.png
  assets/icon.rgba
  packaging/package.json
  tests/test_project.py
  compiler/qecompile.py      # launcher that resolves the shared SDK compiler
  build.py
  README.md
  build/
    releases/<name>.qeapp
    debug/<name>.qeapp
```

## Module header

`native/<arch>/app.qen` starts with the 64-byte header documented in
`sdk/compiler/qen.py` and `platform/violet/ui/vqeaf_qeapp_native.h`:

- magic `QEAF-QEN1`
- module version 1, ABI version 5
- architecture id (1 = arm64, 2 = x86_64)
- section sizes (code, rodata, data, bss), entry offset
- FNV-1a 32 payload hash
- zeroed reserved bytes

The SDK and the on-device loader reject bad magic, wrong ABI, architecture
mismatch, oversized modules, section overruns, and hash mismatches. When no
compiled module exists, `qeaf build` emits a valid empty stub.

## Status

- Declarative `.qeapp` v1 apps still build and run.
- Native `.qeapp` v5 packages compile to a real `.qen` module and run in the
  host simulator (`qeaf simulate`, Phase 3) with fake display/input/storage.
  The SDK still validates and runs frozen v2/v3/v4 packages for older apps.
- On-device native execution (mapping into the phone canvas) is Phase 4.
