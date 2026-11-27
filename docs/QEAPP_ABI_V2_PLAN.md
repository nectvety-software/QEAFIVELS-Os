# QEAPP ABI v2 development plan

## Objective

Add native application support to `.qeapp` while keeping declarative `.qeapp`
v1 working. The first milestone is a safe, minimal native "Hello" app running
inside the violet phone shell.

## Phase 0 - specification freeze

- Freeze `docs/QEAPP_NATIVE_ABI_V2.md`.
- Add manifest schema fields for `format_version: 2`, `abi_version`,
  `runtime: "native"`, `entry`, and `permissions`.
- Define `.qen` module header and section layout.
- Define framework API v2 and lifecycle.
- Acceptance: spec review, no implementation yet.

## Phase 1 - validator and SDK scaffolding

- [x] Add `qeaf validate` checks for v2 manifests.
- [x] Add `qeaf create --native`.
- [x] Add native project template under `sdk/templates/hello-qeapp-native`.
- [x] Add compiler stub that emits a placeholder `.qen` module.
- [x] Acceptance: SDK can create and validate a native package, but cannot run it.

Implemented in `sdk/cli/qeaf.py`, `sdk/compiler/qecompile.py`,
`sdk/compiler/qen.py`, and `sdk/templates/hello-qeapp-native/`.

## Phase 2 - no-op loader

- [x] Add `platform/violet/ui/vqeaf_qeapp_native.h/.c`.
- [x] Implement package open, manifest parse, signature verify, module hash verify.
- [x] Reject invalid ABI, architecture, permissions, and oversized modules.
- [x] Do not map or execute code.
- [x] Acceptance: host tests prove validation and rejection.

Package inspection lives in `vqeaf_qeapp_inspect_native()` (reusing the
existing ZIP/signature code), and module validation lives in
`vqeaf_qeapp_native_validate_module()`. Host evidence:

- `python scripts/test_qeapp_runtime.py` exercises a signed native package and
  a re-signed package with a tampered module hash.
- `python scripts/test_qeapp_native_loader.py` runs 19 accept/reject cases
  against the compiled validator.
- `python -m unittest tests.test_qeapp_native` covers manifest, package,
  scaffold, and hash rejection.

## Phase 3 - simulator

- [x] Implement a host simulator for framework API v2.
- [x] Run native modules in the simulator with fake display/input/storage.
- [x] Add deterministic tests for lifecycle and resource cleanup.
- [x] Acceptance: native "Hello" runs in simulator.

Implemented in `framework/simulator/qeaf_sim.h/.c` (core), `qeaf_sim_main.c`
(CLI), `sdk/simulator/qesim.py` (shared build), and the `qeaf simulate` command.
The simulator loads a signed package through the real device inspector
(`vqeaf_qeapp_inspect_native`), validates the `.qen`, maps code/rodata/data/bss
into an executable arena, resolves the import/export tables, and drives
create/start/pause/resume/stop/destroy against a fake 720x1560 XRGB8888 display
(ASCII rasterized via `vqeaf/font8x8.h`), scripted input, and in-memory storage
with permission checks. x86_64 modules execute on the host through the SysV
`QEAF_APP_CALL` convention; arm64 is built and structurally validated but is
rejected on the host (device execution is Phase 4). Host evidence:

- `python scripts/test_qeapp_simulator.py` builds real PIC modules from
  freestanding C fixtures, signs them, and runs 61 deterministic checks
  (lifecycle order, full-run trace, repeatability, storage read/free, permission
  denial, leak detection, structural rejections, framebuffer pixels) plus the
  CLI acceptance run. All pass; Hello framebuffer hash `0x6cc394ad`, 0 leaks.
- `python -m unittest tests.test_qeapp_native` adds qen import/export table and
  PIC-linker contract tests for both architectures (21 tests).
- `python scripts/test_qeapp_runtime.py`, `scripts/test_qeapp_native_loader.py`
  and `scripts/render_phone_ui.py` remain green (v1 regression).

## Phase 4 - minimal native runtime

- [x] Implement module loading into a bounded memory arena.
- [x] Resolve imports to framework API.
- [x] Call create/start and render a native frame into the 720x1560 canvas.
- [x] Add crash guard and resource release.
- [x] Acceptance: native "Hello" runs on violet hardware without crashing.
      Closed on a physical Redmi Note 7 Pro (violet) on 2026-11-12 with the
      arm64 module executing in a forked child; QEMU virt and host evidence
      remain as cross-checks (same framebuffer hash `0x6cc394ad`).

Implemented in `framework/simulator/qeaf_sim.c` (2 MiB `QEAF_SIM_MAX_ARENA`
bound enforced before mapping), `platform/violet/ui/vqeaf_qeapp_runtime.h/.c`
(guarded child-process launch, 5000 ms timeout, crash/hang containment, canvas
return, deterministic resource release), and `tests/fixtures/qeapp-aarch64/`
(freestanding AArch64 harness). The guarded runtime is compiled and linked into
the Violet Linux-core aarch64-musl image via `scripts/build_violet.py`
(cross-build compile proof; physical execution evidence is recorded separately).

Evidence (all gates exit 0):

- `python scripts/test_qeapp_runtime_guard.py` - 25 checks: clean run renders the
  720x1560 XRGB8888 canvas; a crashing child (`0xc0000005`) is contained and the
  shell survives; a hanging child is killed at 5000 ms; an over-bound arena
  (3 MiB bss) is rejected end-to-end; the supervisor recovers with no leak.
- `python scripts/test_qeapp_aarch64.py` - the real SDK-built arm64 Hello module
  boots on QEMU virt, runs create/start/pause/resume/stop/destroy, and renders
  framebuffer hash `0x6cc394ad`, identical to the x86_64 host simulator hash
  (cross-arch determinism), 0 outstanding allocations. Repeat run stable.
- `python scripts/build_violet.py` - links `build/violet/vqeaf-init` (8,418,032
  bytes, 247 objects), verified AArch64 ELF (machine `0xb7`); the
  `vqeaf_qeapp_native_launch` symbol is present in the image.
- Regression: `scripts/test_qeapp_simulator.py` (61 checks),
  `scripts/test_qeapp_native_loader.py` (19 cases), `scripts/test_qeapp_runtime.py`,
  `python -m unittest discover -s tests` (62 tests) and
  `scripts/render_phone_ui.py` (60 PNGs, 15 pages x 2 profiles x 2 orientations)
  all remain green.

Verification boundary: arm64 execution is proven on physical violet hardware
through the Linux-core PID 1 path (verb `qeapp` over the QEAFRESCUE channel,
guarded child, page `QEAF_PAGE_QEAPP_NATIVE`). The freestanding reference
kernel `kernel/arch/aarch64` is a separate QEMU-only track. Installed native
apps now have a real "Chạy trên thiết bị" button in the shell
(cài qua Tệp → QEAFRESCUE → Cài), and the rescue verb `qeapp <file>` remains for
diagnostics. An arm64 module must be loaded at
a 4096-byte-aligned base (ADRP page granularity); see the ABI spec. Phase 5
(permissions and quotas) is next.

## Phase 5 - permissions and quotas

- [x] Add permission broker.
- [x] Enforce memory, CPU time, and file path scopes.
- [x] Add timeout for native calls.
- [x] Acceptance: permission denial and quota tests pass.

Implemented in `framework/security/qeaf_broker.h/.c` (portable C11 broker, no
OS/hardware dependencies) wired into the host simulator
(`framework/simulator/qeaf_sim.c`). The manifest declares a permission ceiling;
the broker grants only within that ceiling (`check`/`request`/`revoke`), audits
every decision into a 64-entry ring and counts denials. It enforces a storage
path scope (rejects traversal `..`, absolute `/`/`\`, drive `C:` and backslash
paths, and out-of-scope prefixes; a write requires `storage.write.app`) and a
dynamic-memory quota (default 1 MiB of outstanding `read_file` buffers, charged
on acquire and returned on free). Each native lifecycle call is timed against a
per-call CPU budget (default 1000 ms) using a monotonic clock; a finite slow call
is detected after it returns, while an infinite hang never returns and is killed
by the Phase 4 supervisor's whole-run timeout. The CLI gained
`--scope`/`--mem-bytes`/`--call-ms` and prints the audit trail, deny count,
memory used and per-call timeout. The frozen API v2 table is unchanged.

Evidence (all gates exit 0):

- `python scripts/test_qeapp_broker.py` - builds and runs two C gates plus CLI
  acceptance: `tests/vqeaf_broker_test.c` (68 checks: init/defaults,
  check/request/revoke, least-privilege ceiling, path safety, scope containment
  incl. prefix-substring escape, write gate, memory quota, audit/deny) and
  `tests/vqeaf_sim_quota_test.c` (53 checks through real signed modules:
  in-scope read, out-of-scope denial, traversal/absolute/drive/backslash denial,
  memory-quota denial, escalation denial, permission-bit denial, per-call timeout
  via a finite slow fixture at a 5 ms budget, fast-call positive control). CLI
  acceptance: hello within budget, unsafe paths audited (4 denials), escalation
  audited (3 denials), slow call timed out (measured 82 ms > 5 ms). All pass.
- `python scripts/build_violet.py` - the broker compiles for the device target
  `aarch64-linux-musl` and links into `build/violet/vqeaf-init` (8,428,648 bytes,
  248 objects), verified AArch64 ELF. Compile proof only: the broker is not yet
  wired into the device runtime's enforcement path.
- Regression: `scripts/test_qeapp_simulator.py` (61 checks),
  `scripts/test_qeapp_runtime_guard.py` (25 checks),
  `scripts/test_qeapp_aarch64.py` (arm64 Hello hash `0x6cc394ad`),
  `scripts/test_qeapp_native_loader.py`, `scripts/test_qeapp_runtime.py`,
  `python -m unittest discover -s tests` (62 tests) and
  `scripts/render_phone_ui.py` (60 PNGs) all remain green.

Host build note: the simulator's `sysv_abi` API callbacks call Win64 libc, whose
callees do not respect SysV's 128-byte red zone. `sdk/simulator/qesim.py` now
compiles with `-mno-red-zone` so a callback local kept in the red zone cannot be
clobbered mid-call (this was a latent, layout-sensitive crash exposed by the
extra broker locals; fixed at the root, not masked).

Verification boundary: broker/quota enforcement is proven on the host simulator
(x86_64) and compiled for aarch64; device-runtime broker enforcement is not yet
wired. The Phase 4 guarded runner is connected to the real Linux-core PID 1
launcher and has physical violet execution evidence; the freestanding
`kernel/arch/aarch64` track remains a separate reference. Phase 6 (app
ecosystem) is next.

## Phase 6 - app ecosystem

- Port simple declarative-to-native examples.
- Add native app store entries in the launcher.
- Add lifecycle integration with recents/task switcher.
- Acceptance: at least one native utility app runs on device.

### Portable example: CodeEditor

`examples/codeeditor-qeapp` ports the MRE VXP CodeEditor core (buffer, undo,
selection, clipboard, tokenizer, document store and screen state machine)
behind a UI/storage HAL. Its host tests pass and its release/debug packages
validate, but `native/arm64/app.qen` remains the compiler's empty stub until
Phase 4 exists. See `examples/codeeditor-qeapp/docs/PORTING.md`.

## Test plan

- Host unit tests for manifest, module hash, import resolution.
- Simulator tests for lifecycle and cleanup.
- On-device smoke test for native Hello.
- Regression: all existing `.qeapp` v1 tests remain green.

## Safety constraints

- Never execute unvalidated native code.
- Never grant direct hardware or kernel access.
- Never ship private signing keys.
- Preserve the no-fake-data rule in UI and diagnostics.
