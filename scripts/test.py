import json, os, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT/"sdk"/"cli"/"qeaf.py"
subprocess.run(["ctest","--test-dir",str(ROOT/"build"),"--output-on-failure"],check=True)
subprocess.run([sys.executable,"-m","unittest","discover","-s",str(ROOT/"tests"),"-v"],check=True)
with tempfile.TemporaryDirectory(prefix="vqeaf-sdk-test-") as td:
    work=Path(td)
    subprocess.run([sys.executable,str(CLI),"create","DemoApp"],cwd=work,check=True)
    project=work/"DemoApp"
    subprocess.run([sys.executable,str(CLI),"build","--config","all"],cwd=project,check=True)
    name=json.loads((project/"packaging"/"package.json").read_text(encoding="utf-8"))["name"]
    for folder in ("releases", "debug"):
        package=project/"build"/folder/(name+".qeapp")
        subprocess.run([sys.executable,str(CLI),"validate",str(package)],check=True)
    env=dict(os.environ, VQEAF_SDK=str(CLI))
    subprocess.run([sys.executable,"-m","unittest","discover","-s","tests","-v"],
                   cwd=project,env=env,check=True)
for name in ("vqeaf-x86_64-uefi.img","vqeaf-x86_64-recovery.img"):
    image=ROOT/"dist"/name
    if not image.is_file(): raise SystemExit("missing image: "+str(image))
    raw=image.read_bytes()
    assert len(raw)==2880*512 and raw[510:512]==b"\x55\xAA" and raw[54:62]==b"FAT12   "
print("FAT12 EFI and Recovery image structure checks passed.")
subprocess.run([sys.executable,str(ROOT/"scripts"/"validate_bootimg.py"),str(ROOT/"dist"/"vqeaf-x86_64-uefi.img")],check=True)
subprocess.run([sys.executable,str(ROOT/"scripts"/"validate_bootimg.py"),str(ROOT/"dist"/"vqeaf-x86_64-recovery.img")],check=True)
print("Boot image structural validation passed.")
subprocess.run([sys.executable,str(ROOT/"scripts"/"run_qemu.py")],check=True)
subprocess.run([sys.executable,str(ROOT/"scripts"/"capture_screen.py")],check=True)
subprocess.run([sys.executable,str(ROOT/"scripts"/"render_boot_splash.py")],check=True)
subprocess.run([sys.executable,str(ROOT/"scripts"/"test_recovery_ui.py")],check=True)
subprocess.run([sys.executable,str(ROOT/"scripts"/"test_input_power.py")],check=True)
print("Host, SDK, QEMU, framebuffer, recovery UI and input/power tests passed.")
