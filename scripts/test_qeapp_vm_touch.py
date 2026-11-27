#!/usr/bin/env python3
"""Prove the QEAPP VM window turns mouse input into touch events.

Builds the real input_probe fixture, launches qeaf-sim --ui with the violet
profile, posts Win32 mouse messages to its client area, and checks that the
app received canvas coordinates 360,780 (the centre of 720x1560).
"""
import ctypes
import ctypes.wintypes as wt
import importlib.util
import json
import subprocess
import sys
import tempfile
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIM = ROOT / "build" / "qeapp-simulator" / "qeaf-sim.exe"
FIXTURE = ROOT / "tests" / "fixtures" / "qeapp-sim" / "input_probe.c"
ICON = ROOT / "sdk" / "templates" / "hello-qeapp-native" / "assets" / "icon.rgba"
WM_LBUTTONDOWN, WM_LBUTTONUP, WM_CLOSE = 0x0201, 0x0202, 0x0010


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def make_package(temp):
    qeaf = load("qeaf", ROOT / "sdk" / "cli" / "qeaf.py")
    qen = load("qen", ROOT / "sdk" / "compiler" / "qen.py")
    compiler = load("qecompile", ROOT / "sdk" / "compiler" / "qecompile.py")
    module = compiler.compile_native(
        qen, Path(temp), "x86_64", [FIXTURE], "native/x86_64/app.qen", abi_version=2)
    manifest = {
        "format": "qeapp", "format_version": 2, "abi_version": 2,
        "runtime": "native", "id": "qeapp-vm-touch", "name": "VM Touch Probe",
        "version": "1.0.0", "min_os": "0.3.0", "entry": "native/x86_64/app.qen",
        "icon": "assets/icon.rgba", "architecture": ["x86_64"],
        "permissions": ["ui.display", "input.receive"], "dependencies": [],
    }
    package = Path(temp) / "vm-touch.qeapp"
    with zipfile.ZipFile(package, "w", zipfile.ZIP_STORED) as archive:
        archive.writestr("manifest.json", json.dumps(manifest))
        archive.writestr(manifest["entry"], module)
        archive.writestr(manifest["icon"], ICON.read_bytes())
    qeaf.sign_package(package)
    return package


def find_window():
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    user32.FindWindowW.argtypes = (wt.LPCWSTR, wt.LPCWSTR)
    user32.FindWindowW.restype = wt.HWND
    for _ in range(100):
        hwnd = user32.FindWindowW("VQEAFSimWindow", None)
        if hwnd:
            return user32, int(hwnd)
        time.sleep(0.1)
    raise SystemExit("simulator UI window never appeared")


def main():
    if not SIM.is_file():
        raise SystemExit("build qeaf-sim.exe first")
    with tempfile.TemporaryDirectory(prefix="vm-touch-") as temp:
        package = make_package(temp)
        proc = subprocess.Popen(
            [str(SIM), str(package), "--device", "violet", "--ui"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            encoding="utf-8", errors="replace",
        )
        try:
            user32, hwnd = find_window()
            rect = wt.RECT()
            user32.GetClientRect.argtypes = (wt.HWND, ctypes.POINTER(wt.RECT))
            if not user32.GetClientRect(hwnd, ctypes.byref(rect)):
                raise SystemExit("GetClientRect failed")
            client_w = max(1, rect.right - rect.left)
            client_h = max(1, rect.bottom - rect.top)
            x = max(1, client_w // 2)
            y = max(1, client_h // 2)
            expected_x = x * 720 // client_w
            expected_y = y * 1560 // client_h
            lparam = (y << 16) | x
            user32.PostMessageW.argtypes = (wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM)
            user32.PostMessageW(hwnd, WM_LBUTTONDOWN, 1, lparam)
            time.sleep(0.2)
            user32.PostMessageW(hwnd, WM_LBUTTONUP, 0, lparam)
            time.sleep(0.2)
            user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
            out, err = proc.communicate(timeout=30)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.communicate()
    log = out + err
    sys.stdout.write(log)
    if proc.returncode != 0:
        raise SystemExit("simulator UI exited nonzero: %s" % proc.returncode)
    for token in (f"WINDOW: touch {expected_x},{expected_y} pressed=1",
                  f"WINDOW: touch {expected_x},{expected_y} pressed=0",
                  f"input x={expected_x}", f"input y={expected_y}"):
        if token not in log:
            raise SystemExit("missing touch proof: " + token)
    print(f"VM_TOUCH_OK canvas=720x1560 centre={expected_x},{expected_y}")


if __name__ == "__main__":
    main()
