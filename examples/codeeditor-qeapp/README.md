# CodeEditor QEAPP

Native QEAPP v2 port of the MRE VXP CodeEditor app (240x320 plain-text source
editor).

```powershell
python scripts\test_codeeditor_port.py
python scripts\test_codeeditor_native.py
python sdk\cli\qeaf.py build examples\codeeditor-qeapp --config all
python sdk\cli\qeaf.py validate examples\codeeditor-qeapp\build\releases\codeeditor-qeapp.qeapp
```

- Port mapping and status: [`docs/PORTING.md`](docs/PORTING.md)
- Portable core: `src/qeaf_code_*.c/.h` plus `src/qeaf_code_libc.c`
- Native adapter: `src/main.c` (ABI v2)
- Host proof: `tests/host_test.c` (in-memory storage + capturing UI HAL)
- Simulator proof: `tests/sim_driver.c` (real file read + empty folder)
- Packaging proof: `tests/test_project.py`

The arm64 module is a real compiled `.qen` (not the compiler stub). Storage is
read-only and rendering is text-only because ABI v2 has no write/rect calls.