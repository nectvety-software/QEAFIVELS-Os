# CodeEditor QEAPP port

The MRE VXP app at `C:\Users\doxuanhop\Documents\VXP Projects\CodeEditor` is a
240x320 plain-text source editor. This project ports its portable core to a
native QEAPP v2 project under `examples/codeeditor-qeapp`.

## Status

| Area | State |
| --- | --- |
| Buffer, selection, undo, clipboard | Ported, host tested |
| Syntax tokenizer (C, HTML, CSS, JS, Python) | Ported, host tested |
| Document store (14 x 1280 bytes, FAT-style names) | Ported, host tested |
| App state machine and screen painters | Ported, host tested through a UI HAL |
| Manifest, package, signature, validation | Working (QEAPP v2) |
| Native module (arm64 + x86_64) | Compiled from `src/`; runs in host simulator |
| Storage on ABI v2 | Read-only: one real file, writes refused |
| MRE VM UI, pen events, multi-tap keypad | Replaced by a UI/input HAL |
| Pen-drag range selection | Not ported; select-all and clipboard work |
| Preference persistence (`save.bin`) | Not ported; settings are in-memory |
| Folders, encodings, localization, project tree | Not ported |

The packaged `native/arm64/app.qen` is a **real compiled module** (54906 bytes):
it exports `create/start/pause/resume/stop/destroy/on_input` and imports only
`qeaf_app_api_v2`. It runs end-to-end in the host simulator (x86_64 build)
with zero outstanding allocations and a non-zero framebuffer hash. Device
execution on violet hardware is not claimed: physical board access and the
independent kernel are still pending (Phase 4).

## Mapping

| MRE original | QEAPP port |
| --- | --- |
| `src/code_store.h` | `src/qeaf_code_store.h/.c` |
| `src/code_input.h` (editing half) | `src/qeaf_code_buffer.h/.c` |
| `src/code_hl.h` | `src/qeaf_code_hl.h/.c` |
| `src/code_ui.h` (state + painters) | `src/qeaf_code_app.h/.c` |
| `src/retro_ui.h` (drawing) | `qeaf_code_ui_t` in `qeaf_code_platform.h` |
| `vm_file_*` | `qeaf_store_io_t` (list/read/write/mkdir/remove) |
| `vm_graphic_*`, `VxpRender2D` | `qeaf_code_ui_t` callbacks |
| `handle_keyevt` / `handle_penevt` | `qeaf_app_key()` / `qeaf_app_text()` |
| `save.bin` preference cache | `qeaf_store_t` fields (`auto_indent`, `indent_width`, `dark_mode`, `syntax_hl`, `line_nums`) |

The port keeps the original limits: `QEAF_CODE_LEN 1280`, `QEAF_DOC_MAX 14`,
undo depth 5, and the extension order C/H/TXT/HTML/CSS/JS/PY. Screens are the
same set: splash, list, options, view, edit, edit menu, delete confirm,
settings, name entry, symbol palette, find, details and about.

## Host proof

`tests/host_test.c` supplies an in-memory storage HAL and a capturing UI HAL,
then exercises:

* insert, backspace, undo, select-all, copy, cut, paste, find, auto-indent,
  indent/outdent and line/column movement;
* keyword, string, comment, number, tag, attribute, property and preprocessor
  roles for all five languages;
* add, update, rescan, rename, duplicate, export and delete in the store;
* boot to splash and list, new file, typing, save, undo, find, symbol insert,
  settings toggle, read view, delete confirmation and read-only refusal.

Run it with:

```powershell
python scripts\test_codeeditor_port.py
```

The script compiles the portable sources with a host C compiler, runs the
tests, checks that no MRE/LVGL symbols leaked into `src`, and rebuilds
and validates the release package. The native module proof is
`scripts/test_codeeditor_native.py` (arm64 structure plus an x86_64
simulator run with a real seeded file and an empty folder).

The project's own tests cover packaging:

```powershell
python -m unittest discover -s examples\codeeditor-qeapp\tests -p "test_project.py" -v
```

## Build

```powershell
python sdk\cli\qeaf.py build examples\codeeditor-qeapp --config all
python sdk\cli\qeaf.py validate examples\codeeditor-qeapp\build\releases\codeeditor-qeapp.qeapp
```

Outputs:

* `build/releases/codeeditor-qeapp.qeapp`
* `build/debug/codeeditor-qeapp.qeapp`

## Next steps

1. ABI v2 still lacks `write_file`, rectangle fill and directory listing; the
   module is therefore honest about being read-only and text-only. Add those
   calls in a future ABI revision, then bind the existing HAL mutators.
2. Physical violet execution is a separate gate (Phase 4): no board or
   independent kernel is available yet.
