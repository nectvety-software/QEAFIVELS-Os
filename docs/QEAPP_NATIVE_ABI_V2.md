# QEAPP Native ABI (v2 base, current default v5)

> The container, manifest and loader rules below are the v2 base that later
> revisions build on. Validation and the loader still accept `abi_version`
> 2, 3, 4 and 5; the SDK scaffold and the compiler now default to **v5**.
> Example manifests in this document show `abi_version: 2` for brevity and are
> still valid.

## Goal

Make `.qeapp` the native application format of QEAFIVELS OS, while keeping
declarative `.qeapp` v1 apps working. A v2 package may contain native code
for a documented, versioned QEAFIVELS ABI. It is not an APK and does not use vendor mobile-platform APIs, ART, JNI, or AOSP packaging.

## Non-goals for v1 runtime

- No arbitrary native code in `.qeapp` v1.
- No direct hardware, kernel pointer, or privileged node access.
- No compatibility with vendor APK/AAB formats or Linux ELF desktop assumptions.

## Container

Option A (recommended): keep `.qeapp` as a ZIP_STORED package with a
manifest and a native module:

```text
manifest.json
native/<arch>/app.qen   # QEAFIVELS native module, not raw OS ELF
assets/...
SIGNATURE
```

Option B: a dedicated `QEAPP2` binary container with sections:

```text
header: magic, format_version, abi_version, arch, entry_offset, ...
manifest section
code section (relocatable QEAFIVELS module)
data section
resource section
signature section
```

The first implementation should prefer Option A because the existing SDK,
ZIP reader, signature verifier, and file manager already understand ZIP_STORED.

## Manifest additions

```json
{
  "format": "qeapp",
  "format_version": 2,
  "abi_version": 2,
  "runtime": "native",
  "entry": "native/arm64/app.qen",
  "architecture": ["arm64"],
  "permissions": ["storage.read", "ui.display"],
  "resources": ["assets/icon.rgba"]
}
```

`format_version: 1` remains declarative and must still run.

## Native module format (`app.qen`)

Do not load raw OS ELF directly. Use a small relocatable module format owned
by QEAFIVELS:

- magic `QEAF-QEN1`
- target architecture and ABI version
- section table: code, rodata, data, bss size, relocs, exports, imports
- entry symbol index
- required framework API version
- module hash

The loader verifies the module before mapping it. Raw ELF may be an internal
build artifact, but the shipped `.qeapp` should contain the QEAF module.

### Concrete header (implemented)

The 64-byte little-endian header is implemented once in
`sdk/compiler/qen.py` and once in
`platform/violet/ui/vqeaf_qeapp_native.c`:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 9 | magic `QEAF-QEN1` |
| 9 | 1 | module version (1) |
| 10 | 1 | ABI version (2/3/4/5) |
| 11 | 1 | architecture (1 = arm64, 2 = x86_64) |
| 12 | 1 | flags (0) |
| 13 | 2 | header size (64) |
| 15 | 2 | section count (<= 16) |
| 17 | 4 | module size (total bytes) |
| 21 | 4 | code size |
| 25 | 4 | rodata size |
| 29 | 4 | data size |
| 33 | 4 | bss size |
| 37 | 4 | entry offset |
| 41 | 4 | imports size |
| 45 | 4 | exports size |
| 49 | 4 | FNV-1a 32 hash of the payload |
| 53 | 2 | minimum framework API |
| 55 | 9 | reserved, must be zero |

Validation rejects: wrong magic, wrong module/ABI version, unknown or
mismatched architecture, nonzero flags, wrong header size, too many sections,
module size that disagrees with the file length, section sizes beyond the
payload, entry offset outside the code section, hash mismatch, nonzero
reserved bytes, and modules over 8 MiB.

`qeaf build` emits a valid empty stub when a project has no compiled module.
The stub validates and packages, but has an empty code section, so a future
runtime must refuse to start it.

### Payload and tables (implemented)

The payload follows the 64-byte header in this order:

```text
code | rodata | data | imports | exports
```

`bss` is not stored; the loader zero-fills it after `data`. Section bases are
padded by the linker to a common alignment so every category meets the strict
alignment of any other.

- **imports**: a sequence of NUL-terminated ASCII names. A module declares
  exactly one framework import matching its ABI byte: `qeaf_app_api_v2` (v2),
  `qeaf_app_api_v3` (v3), `qeaf_app_api_v4` (v4) or `qeaf_app_api_v5` (v5).
  Imports are a capability check, not address binding: the API table itself is
  passed to `create()`.
- **exports**: a sequence of records, each a NUL-terminated ASCII name followed
  by a 4-byte little-endian offset into the code section. Required exports:
  `create`, `start`, `stop`, `destroy`. Optional: `pause`, `resume`, `on_input`.

Both tables are bounds-checked and structure-checked by the SDK
(`qen.validate_module` / `qen.read_tables`) and by the device validator
(`vqeaf_qeapp_native_validate_module`): NUL termination, printable ASCII names,
non-empty names, complete export records, and export offsets inside the code
section.

Modules are produced by `sdk/compiler/qenlink.py`, a small ELF64 PIC static
linker. It resolves PC-relative relocations at build time (base-independent)
and rejects absolute/GOT relocations and undefined symbols, so a module needs
no runtime relocation and no libc. x86_64 accepts PC32/PLT32; aarch64 accepts
the ADRP/ADD/LDST-lo12, literal-load and branch PIC families.

### Load-base alignment (arm64)

x86_64 PC-relative references are byte-exact, so an x86_64 module may be mapped
at any base. aarch64 uses `ADRP`+`ADD`, which is **page-granular**: the linker
resolves page deltas against a page-aligned code base (offset 0). An arm64
module must therefore be loaded at a **4096-byte-aligned base**, or every
rodata/data pointer shifts by the base's offset within its page (observed as
empty strings and a wrong framebuffer). The Phase 4 AArch64 harness aligns its
arena to 4096 for this reason; any future device loader must do the same.

## Framework API

Reuse and extend `framework/application_api/qeaf_api.h`:

```c
typedef struct {
    uint32_t size;
    uint32_t abi_version;
    void (*log_utf8)(const char *message);
    int (*draw_text)(int x, int y, const char *utf8);
    int (*request_permission)(const char *permission);
    int (*read_file)(const char *path, void **out, size_t *size);
    void (*free)(void *ptr);
    /* future: timers, input, audio, storage, network */
} qeaf_app_api_v2;
```

Apps call only through this table. They never receive kernel pointers,
hardware registers, or libc entry points.

Every function pointer in the table (and every module export) uses the
`QEAF_APP_CALL` convention from `qeaf_api.h`: SysV AMD64 on x86_64 even when the
host is Windows, AAPCS64 on aarch64. This keeps the module and the host in
agreement regardless of the host compiler's default ABI.

## Framework API v3 (media revision, implemented)

ABI v3 is a strict superset of v2, added so a `.qeapp` can act as a media
app (see `docs/QEAF_VIDEO_FORMAT.md`). The first six fields of
`qeaf_app_api_v3` are byte-identical to `qeaf_app_api_v2`; the frozen v2
table is unchanged and v2 modules keep working as before.

```c
typedef int (QEAF_APP_CALL *qeaf_blit_pixels_fn)(int x, int y, int w, int h,
                                                 int stride,
                                                 const uint16_t *rgb565);
typedef int (QEAF_APP_CALL *qeaf_read_file_part_fn)(const char *path,
                                                    uint32_t offset,
                                                    size_t size, void *out);
typedef struct {
    /* v2 prefix (frozen). */
    uint32_t size;
    uint32_t abi_version;
    void (QEAF_APP_CALL *log_utf8)(const char *message);
    int  (QEAF_APP_CALL *draw_text)(int x, int y, const char *utf8);
    int  (QEAF_APP_CALL *request_permission)(const char *permission);
    int  (QEAF_APP_CALL *read_file)(const char *path, void **out, size_t *size);
    void (QEAF_APP_CALL *free)(void *ptr);
    /* v3 additions. */
    qeaf_blit_pixels_fn blit_pixels;
    qeaf_read_file_part_fn read_file_part;
} qeaf_app_api_v3;
```

- `blit_pixels` copies RGB565 rows into the app framebuffer with clipping;
  requires `ui.display`.
- `read_file_part` reads a byte range into a caller-owned buffer (the
  runtime's transient copy is charged against the memory quota for the
  duration of the call); requires `storage.read`. This is how a media app
  streams a QVF without loading the file into the 1 MiB quota.

A v3 module declares `abi_version: 3` in its manifest, sets the `.qen`
header's ABI byte to 3 and `min_api` to 3, and imports
`qeaf_app_api_v3` (never both v2 and v3). v2 packages, the v2 table and all
v2 validation rules stay frozen.

## Framework API v4 and v5 (weight revisions, implemented)

Both revisions are strict supersets that keep every earlier prefix
byte-identical, so a newer table can be handed to an older prefix reader and
the frozen v2/v3 tables never change. They expose the shell's two weight
systems to native apps (see `platform/violet/ui/vqeaf_theme.h`):

- **v4 — typography weight.** `qeaf_app_api_v4` appends one field to the v3
  prefix:

  ```c
  #define QEAF_TEXT_WEIGHT_REGULAR 0u
  #define QEAF_TEXT_WEIGHT_BOLD    1u
  typedef int (QEAF_APP_CALL *qeaf_draw_text_weighted_fn)(int x, int y,
                                                          const char *utf8,
                                                          uint32_t weight);
  ```

  `draw_text_weighted` draws `utf8` in the given weight; the bold face is a
  real heavier raster (the 8x8 bitmap is dilated one column right), not a
  colour change. Requires `ui.display`; an unknown weight is rejected with
  `-1`. A v4 module sets ABI byte 4, `min_api` 4 and imports
  `qeaf_app_api_v4`.

- **v5 — layout weight.** `qeaf_app_api_v5` appends one field to the v4
  prefix:

  ```c
  #define QEAF_LAYOUT_AXIS_HORIZONTAL 0u
  #define QEAF_LAYOUT_AXIS_VERTICAL   1u
  typedef struct { int32_t x, y, w, h; } qeaf_rect_t;
  typedef int (QEAF_APP_CALL *qeaf_layout_split_weighted_fn)(
      int x, int y, int w, int h, uint32_t axis, const uint32_t *weights,
      uint32_t n, uint32_t gap, qeaf_rect_t *out);
  ```

  `layout_split_weighted` splits `(x,y,w,h)` into `n` consecutive sub-rects
  along `axis`, sized in ratio to `weights[i]` with `gap` pixels between
  neighbours — the same proportional semantics as the shell's
  `vqeaf_weight` / `weighted_row` / `weighted_column` (LVGL flex-grow). After
  reserving `gap*(n-1)` pixels the free span is divided by total weight; the
  integer remainder is handed one pixel at a time to the leading rects so the
  sizes always sum to the free span exactly. It is **pure computation**: no
  permission and no display side effect, so it succeeds even for an app
  without `ui.display`. Returns `-1` on NULL weights/out, `n==0`, unknown
  axis, negative w/h, gap overflow or zero total weight. The algorithm lives
  once as `qeaf_layout_split_rect` (a `static inline` in `qeaf_api.h`) and is
  shared verbatim by the host simulator, the device runtime and the arm64
  harness, so all three agree pixel-for-pixel. A v5 module sets ABI byte 5,
  `min_api` 5 and imports `qeaf_app_api_v5`.

Validation accepts ABI `{2,3,4,5}` everywhere (SDK `qen.py`/`qecompile.py`,
`qeaf validate`, the device validator `vqeaf_qeapp_native.c`, the host
simulator and the device runtime); `min_api` must equal the ABI byte and a
module must import exactly the one matching framework name. v2/v3 packages
and their frozen tables are untouched.

Evidence: `tests/fixtures/qeapp-sim/weight.c` (v4) and `layout.c` (v5) are
built into signed packages by `scripts/test_qeapp_simulator.py` and asserted
by `tests/vqeaf_sim_test.c` (weighted ink strictly greater than regular;
exact proportional rects `152/304/152` and `94/94/93`, six invalid-split
probes rejected, layout works without `ui.display`). `scripts/test_qeapp_aarch64.py`
runs both fixtures on QEMU virt arm64 and checks the framebuffer hash equals
the x86_64 host hash (cross-arch determinism). This is emulated evidence, not
physical violet hardware.

## Lifecycle

```text
create(api) -> handle
start(handle)
pause(handle)
resume(handle)
stop(handle)
destroy(handle)
```

The OS owns the loop. An app may request redraws and input events, but does
not own the process scheduler or display.

## Process model

Phase 4 isolates the module in a **child process**, not the shell's address
space: the supervisor spawns the runner, waits with a hard timeout, and reads
back a report plus the rendered frame. A crash or hang is contained and the
shell survives (see "Guarded runtime" below). A later stage must still provide:

- per-app heap and stack inside the child
- capability-checked file paths
- timeout and memory quotas (Phase 5)
- crash isolation and restart (isolation done; restart policy later)

## Loader steps

1. Read `.qeapp` from QEAFRESCUE or installed storage.
2. Verify ZIP_STORED structure and signature.
3. Parse manifest and check `format_version`, `abi_version`, architecture.
4. Locate the native module and verify its hash and imports.
5. Check permissions against the OS permission broker.
6. Allocate app memory and resolve imports to the framework API.
7. Create lifecycle handle.
8. Render the app into the phone canvas (720x1560 portrait, 1560x720 landscape).
9. On exit, release all resources deterministically.

## Permissions

Initial permission set:

- `ui.display`
- `storage.read`
- `storage.write.app`
- `input.receive`
- `media.read`
- `network.client` (only when a real network backend exists)

Permissions are declared in the manifest and enforced by the OS. The app cannot
bypass them through native code.

## SDK changes

- `qeaf create` gains `--native` to scaffold a native project.
- `qeaf build` compiles C/C++ to the QEAF module format for the selected arch.
- `qeaf validate` checks manifest, module hash, imports, permissions, and size.
- `qeaf sign` signs the whole package including the native module.
- Simulator runs the module against the framework API.

## Security notes

- Native modules are not trusted by default.
- No direct syscall, libc, or kernel access.
- All file paths are validated and scoped.
- Memory and CPU quotas are enforced.
- A crashing module must not crash the shell.

## Migration

- v1 declarative apps continue to run.
- v2 native apps require a newer OS version declared in `min_os`.
- The file manager can install both v1 and v2 packages.
- The launcher shows the app type (`QEAPP` or `QEAPP native`).

## Host simulator (Phase 3, implemented)

`framework/simulator/qeaf_sim.c` runs a validated module on the host with fake
devices; `qeaf simulate <package.qeapp>` is the CLI entry point. It is a test
harness, not the device runtime:

- Loads the package through the real device inspector and module validator, so
  host and device agree on what is acceptable.
- Maps `code|rodata|data|bss` into one executable arena (VirtualAlloc / mmap),
  zero-fills bss, and resolves the export table to function pointers. No
  runtime relocation is needed (the linker already resolved PC-relative refs).
- Executes x86_64 modules only; an arm64 package is rejected with a clear
  reason. arm64 code generation is structurally validated but its execution is
  a Phase 4 device gate.
- Fake display: a 720x1560 XRGB8888 raster; `draw_text` rasterizes ASCII via
  `vqeaf/font8x8.h` and is gated on `ui.display`. A deterministic FNV-1a
  framebuffer hash makes renders repeatable and comparable.
- Fake input: scripted `qeaf_input_event_t` delivered to `on_input` between
  start and pause, gated on `input.receive`.
- Fake storage: in-memory files; `read_file` is gated on `storage.read` and
  returns a tracked allocation that the app must release with `free`.
- Lifecycle state machine rejects out-of-order calls; `qeaf_sim_run` drives the
  full create/start/pause/resume/stop/destroy sequence and fails if any
  allocation is still outstanding after destroy (leak detection).

Deterministic coverage lives in `tests/vqeaf_sim_test.c` driven by
`scripts/test_qeapp_simulator.py` (61 checks): lifecycle order, full-run trace,
repeatability, storage read/free, permission denial, leak detection, structural
rejections (empty stub, unknown import, missing export, tampered signature,
wrong arch) and rasterized framebuffer pixels.

## Guarded runtime (Phase 4, implemented)

`platform/violet/ui/vqeaf_qeapp_runtime.c` exposes the device-facing launch
entry `vqeaf_qeapp_native_launch(runner, package, canvas, report, error, cap)`.
The shell never runs a module in its own address space:

- **Bounded arena.** The runner refuses any module whose
  `code+rodata+data+bss` exceeds `QEAF_SIM_MAX_ARENA` (2 MiB) *before* mapping,
  so an oversized module is rejected, not allocated.
- **Crash guard.** The supervisor spawns the runner as a child (Win32
  `CreateProcessA` / POSIX `fork`+`execl`), captures its stdout over a pipe, and
  waits with a hard `VQEAF_NATIVE_TIMEOUT_MS` (5000 ms). A child exception
  (NTSTATUS >= `0xC0000000`) or signal is reported as `crashed`; a hang is
  killed (`TerminateProcess` / `SIGKILL`) and reported as `timed_out`. In every
  failure path the supervisor reaps the child, removes the temp framebuffer and
  returns `-1` with a reason; the shell keeps running and can launch again.
- **Canvas.** On a clean run the child's raw 720x1560 XRGB8888 frame is loaded
  into an `lv_image_dsc_t` (stride 2880, `data_size` 720*1560*4) the shell can
  blit. The report carries the framebuffer hash, lifecycle trace and outstanding
  allocation count.
- **Resource release.** The temp framebuffer is removed on every path; the child
  is always reaped; leak detection fails the run if any `read_file` buffer is
  still outstanding after `destroy`.

Host evidence: `scripts/test_qeapp_runtime_guard.py` (25 checks) builds real
Hello/crash/hang/over-bound fixtures and proves a clean run renders the canvas,
a crashing child (`0xc0000005`) is contained, a hanging child is killed at the
timeout, an over-bound arena is rejected end-to-end, and the supervisor recovers
with no resource leak across runs.

## AArch64 execution (Phase 4, emulated + physical device)

`tests/fixtures/qeapp-aarch64/` is a freestanding AArch64 harness that runs the
**real SDK-built arm64 Hello module** under `qemu-system-aarch64 -machine virt`
(`scripts/test_qeapp_aarch64.py`). It is emulated AArch64 evidence, not physical
violet hardware and not the full violet shell:

- The gate compiles `sdk/templates/hello-qeapp-native` to arm64 PIC, validates
  the `.qen`, embeds it in the harness, and boots it on QEMU virt (PL011 UART).
- Boot enables FP/SIMD (`CPACR_EL1.FPEN`) and is built with `-mstrict-align`;
  the arena is 4096-byte aligned (see "Load-base alignment").
- The harness parses the module, copies sections into the bounded arena,
  resolves exports, and drives create/start/pause/resume/stop/destroy against a
  fake API v2 (log -> UART, draw_text -> static 720x1560 raster), then powers off
  via PSCI.
- Acceptance: the arm64 framebuffer hash equals the x86_64 host simulator hash
  for the same app (`0x6cc394ad`), proving the arm64 code executes and renders
  the identical frame (cross-arch determinism), with 0 outstanding allocations.

Physical violet execution (2026-11-12): `platform/violet/ui/vqeaf_qeapp_device.c`
runs the same signed arm64 package on a Redmi Note 7 Pro. The rescue verb
`qeapp <file>` reads the package from QEAFRESCUE, validates it with the device
inspector, and executes it in a forked child with a 5000 ms hard timeout. The
child renders into a `MAP_SHARED` 720x1560 XRGB8888 frame that the shell shows on
`VQEAF_PAGE_QEAPP_NATIVE`. Measured on hardware: full lifecycle, hash
`0x6cc394ad` (same as host/QEMU), 0 outstanding allocations; a faulting module
reports `crashed (signal 11); shell survived`; a hanging module is killed at the
timeout. A native package installed from Tệp → QEAFRESCUE → Cài gets a real
"Chạy trên thiết bị" button; the rescue verb `qeapp <file>` stays for
diagnostics. The runtime is still the Linux-musl transport prototype, so this
is not an independent-OS claim.

Verification boundary: the QEMU harness above proves the arm64 module and the
AAPCS64/PIC contract under emulation; the physical-device run proves the same
package executes on violet hardware through the prototype shell. The independent
OS kernel (`kernel/arch/aarch64`) is not yet implemented.

## Permission broker and quotas (Phase 5, implemented)

`framework/security/qeaf_broker.h/.c` is a portable C11 broker (no OS, libc-
nonportable, or hardware dependencies) shared by the host simulator and, later,
the device runtime. The manifest permission mask is a **ceiling**: the broker
grants only within it and can never exceed it.

- Decisions: `check` (silent query), `request` (grants within the ceiling,
  audits), `revoke` (removes a grant, audits). A request for an undeclared or
  unknown permission is denied and audited. Every denial increments `deny_count`;
  every decision is recorded in a 64-entry audit ring (`QEAF_BROKER_MAX_AUDIT`).
- Path scope: `check_path` rejects empty paths, traversal (`..` components),
  absolute paths (`/` or `\`), drive-qualified paths (`C:`) and backslash
  separators, and any path outside the configured scope prefix. A write
  (`want_write`) additionally requires `storage.write.app`. An empty scope means
  safety-only checks (no containment), preserving the Phase 3 fixtures.
- Memory quota: `mem_acquire`/`mem_release` charge and return outstanding dynamic
  bytes against a budget (default `QEAF_QUOTA_MEMORY_BYTES` = 1 MiB). The
  simulator charges each `read_file` buffer and returns it on `free`; an
  over-budget read is denied and audited before any allocation.
- Per-call CPU timeout: the simulator times each native lifecycle call
  (create/start/pause/resume/stop/destroy and `on_input`) against
  `call_budget_ms` (default `QEAF_QUOTA_CALL_MS` = 1000 ms) using a monotonic
  clock (QPC on Windows, `clock_gettime` on POSIX). A finite but slow call is
  detected after it returns and fails the run; an infinite hang never returns and
  is instead killed by the Phase 4 supervisor's whole-run timeout
  (`QEAF_QUOTA_RUN_MS` = 5000 ms). Freestanding modules have no clock access, so
  the host measures wall-clock around the synchronous call.

The frozen API v2 table is unchanged; the write gate is exposed through
`check_path(want_write)` and is forward-compatible with a future `write_file`.
The CLI (`qeaf simulate`) accepts `--scope`, `--mem-bytes` and `--call-ms` and
prints the audit trail, deny count, memory used and per-call timeout.

Host build constraint: the `sysv_abi` API callbacks call Win64 libc, whose
callees do not respect SysV's 128-byte red zone below `rsp`. The simulator build
(`sdk/simulator/qesim.py`) compiles with `-mno-red-zone` so callback locals are
never kept in the red zone where a Win64 callee could clobber them.

Evidence: `scripts/test_qeapp_broker.py` runs 68 broker unit checks and 53
simulator integration checks plus CLI acceptance (all exit 0). The broker also
compiles for `aarch64-linux-musl` and links into the violet image (compile proof
only; device-runtime enforcement is not yet wired).

## Implementation stages

1. Write this ABI and module format specification.
2. Add a no-op native module loader that validates but does not execute.
3. Add a simulator for the framework API.
4. Add a minimal native "Hello" app.
5. Add lifecycle and resource cleanup.
6. Add permissions and quotas.
7. Port simple apps only after stages 1-6 pass.

## Current status (Phase 1-4)

- Manifest validation accepts `format_version: 2` with `runtime: "native"`,
  `abi_version` 2-5 (default scaffold **5**), a relative `.qen` entry, and
  declared permissions.
- `qeaf create --native` scaffolds a native project; `qeaf build` compiles
  freestanding PIC C into a real `.qen` via `qenlink.py`, or emits a valid empty
  stub when no source is present.
- `vqeaf_qeapp_inspect_native()` verifies package structure, signature,
  manifest fields, architecture, permissions, and module structure on the device.
- `vqeaf_qeapp_native_validate_module()` checks the header, sizes, hash,
  import/export tables, and architecture; it never maps, relocates, or calls code.
- The host simulator (Phase 3) executes x86_64 modules end-to-end against fake
  display/input/storage with leak detection; `qeaf simulate` runs it. Native
  Hello completes the full lifecycle with 0 outstanding allocations.
- The guarded runtime (Phase 4) launches modules in a time-bounded child with a
  2 MiB arena bound, crash/hang containment, canvas return and deterministic
  resource release; the shell survives a crashing or hanging module.
- The real arm64 Hello module (Phase 4) executes under QEMU virt AArch64 and
  renders the same framebuffer hash as the x86_64 host (`0x6cc394ad`), 0 leaks.
- Verification boundary: x86_64 execution is proven on the host; arm64 execution
  is proven under **emulation** (QEMU virt) with cross-arch hash equality. No
  physical violet hardware evidence is claimed (no authorized board), and the
  independent OS kernel is not yet implemented. The violet aarch64 cross-build
  links the guarded runtime as a compile proof only. Phase 5 (permissions and
  quotas) is next.
