"""Compile one freestanding Rust crate into the existing QEAPP .qen container."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


TARGETS = {
    "x86_64": "x86_64-unknown-none",
    "arm64": "aarch64-unknown-none",
}
REQUIRED_EXPORTS = {"create", "start", "stop", "destroy"}


def find_rustc():
    configured = os.environ.get("RUSTC")
    if configured and Path(configured).is_file():
        return configured
    found = shutil.which("rustc")
    if found:
        return found
    raise ValueError("rustc not found; install Rust and set RUSTC=/path/to/rustc")


def rust_target(arch):
    try:
        return TARGETS[arch]
    except KeyError as exc:
        raise ValueError("unsupported Rust architecture: %s" % arch) from exc


def target_installed(rustc, target):
    proc = subprocess.run(
        [rustc, "--print", "sysroot"], capture_output=True, text=True,
        encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        return False
    sysroot = Path(proc.stdout.strip())
    return (sysroot / "lib" / "rustlib" / target / "lib").is_dir()


def compile_rust(qen, qenlink, sdk, project, arch, entry, abi_version=5,
                 mode="release", sdk_source=None):
    if abi_version not in (2, 3, 4, 5):
        raise ValueError("unsupported native abi_version: %s" % abi_version)
    if mode not in ("release", "debug"):
        raise ValueError("Rust build mode must be release or debug")
    source_root = project / "src"
    if not (source_root / "main.rs").is_file():
        raise ValueError("Rust native projects require src/main.rs")
    rust_sources = sorted(source_root.rglob("*.rs"))
    if not rust_sources:
        raise ValueError("Rust native project has no Rust sources")
    for source in rust_sources:
        relative = source.relative_to(project).as_posix()
        if ".." in Path(relative).parts:
            raise ValueError("Unsafe Rust source path: %s" % relative)

    if sdk_source is None:
        sdk_source = Path(sdk.__file__).resolve().parents[1] / "rust" / "src"
    sdk_source = Path(sdk_source).resolve()
    if not (sdk_source / "lib.rs").is_file():
        raise ValueError("QEAPP Rust SDK sources not found: %s" % sdk_source)

    rustc = find_rustc()
    target = rust_target(arch)
    if not target_installed(rustc, target):
        raise ValueError(
            "Rust target %s is not installed; run: rustup target add %s"
            % (target, target))

    with tempfile.TemporaryDirectory(prefix="qenrust-") as temp:
        work = Path(temp)
        shutil.copytree(sdk_source, work / "sdk_src")
        shutil.copytree(source_root, work / "app_src")
        wrapper = work / "lib.rs"
        wrapper.write_text(
            "#![no_std]\n"
            "#![no_main]\n\n"
            "#[panic_handler]\n"
            "fn panic(_info: &core::panic::PanicInfo<'_>) -> ! { loop {} }\n\n"
            "#[allow(dead_code)]\n"
            "#[path = \"sdk_src/lib.rs\"]\n"
            "mod qeapp;\n"
            "use qeapp as qeapp_sdk;\n\n"
            "#[path = \"app_src/main.rs\"]\n"
            "mod app;\n\n"
            "#[allow(dead_code)]\n"
            "fn main() {}\n",
            encoding="utf-8")
        obj = work / "qeapp.o"
        command = [
            rustc, "--edition=2021", "--crate-name", "qeapp_module",
            "--crate-type", "bin", "--emit=obj", "--target", target,
            "--cfg", "qeapp_combined_crate", "-C", "panic=abort",
            "-C", "relocation-model=pic",
            "-C", "opt-level=2" if mode == "release" else "opt-level=1",
            "-C", "codegen-units=1", "-C", "debuginfo=0",
        ]
        if arch == "arm64":
            command.extend(["-C", "target-feature=+strict-align"])
        command.extend([str(wrapper), "-o", str(obj)])
        proc = subprocess.run(command, capture_output=True, text=True,
                              encoding="utf-8", errors="replace")
        if proc.returncode != 0:
            raise ValueError("Rust compile failed:\n%s" %
                             (proc.stderr or proc.stdout))
        linked = qenlink.link_objects([obj], arch)

    if not linked["code"]:
        raise ValueError("Rust module has no code section")
    exports = dict(linked["exports"])
    missing = sorted(REQUIRED_EXPORTS - exports.keys())
    if missing:
        raise ValueError("Rust module is missing lifecycle exports: %s" %
                         ", ".join(missing))
    module = qen.build_module(
        linked["code"], linked["rodata"], linked["data"],
        linked["bss_size"], linked["exports"], arch=arch,
        min_api=abi_version, abi_version=abi_version,
        entry_offset=exports["create"])
    errors = qen.validate_module(module, arch)
    if errors:
        raise ValueError("\n".join(errors))
    output = project / entry
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(module)
    return module
