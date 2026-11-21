# Prompt: Implement QEAPP Native ABI v2 (minimal safe loader)

You are working in `D:\QEAFIVELS-Os`. Implement the first safe stages of
QEAPP Native ABI v2 without breaking declarative `.qeapp` v1 apps.

## Read first

- `docs/QEAPP_NATIVE_ABI_V2.md`
- `docs/QEAPP_ABI_V2_PLAN.md`
- `framework/application_api/qeaf_api.h`
- `framework/application_api/qeaf_native_abi.h`
- `platform/violet/ui/vqeaf_qeapp.c`
- `platform/violet/ui/vqeaf_qeapp.h`
- `sdk/cli/qeaf.py`
- `sdk/compiler/qecompile.py`

## Task

Implement Phase 1 and Phase 2 only:

1. Add `format_version: 2`, `abi_version`, `runtime: "native"`, `entry`,
   and `permissions` to manifest validation.
2. Add a native project template.
3. Add a no-op native loader that validates packages and modules but does not
   execute code.
4. Add host tests for rejection of invalid native packages.

## Constraints

- Do not execute native code.
- Do not change `.qeapp` v1 behavior.
- Do not bundle firmware or secrets.
- Do not claim hardware support that is not verified.
- Keep all existing tests green.

## Deliverables

- Updated SDK validation.
- Native template directory.
- `vqeaf_qeapp_native.c/.h` with validation-only loader.
- Host tests.
- Documentation update.

## Acceptance

- `python scripts/test_qeapp_runtime.py` still passes.
- New tests prove invalid native packages are rejected.
- No native code is executed.