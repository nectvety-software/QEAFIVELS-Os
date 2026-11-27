import argparse
import importlib.util
import json
import os
import re
import subprocess
from pathlib import Path, PurePosixPath
import sys
import tempfile
import zipfile

# The CLI prints the output path, which can contain Vietnamese characters
# ("...\VQEAF Projects\Xếp Hình 4x4"). Without this, a cp1252 console makes the
# build fail on UnicodeEncodeError after the package was already written.
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")


def project_file(project, name):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
        raise ValueError(f"Unsafe project path: {name}")
    file = (project / name).resolve()
    if not file.is_relative_to(project) or not file.is_file():
        raise ValueError(f"Missing or external project file: {name}")
    return file


def validate_packaging(config):
    if not isinstance(config, dict):
        raise ValueError("Packaging config must be an object")
    if config.get("format") != "qeapp-packaging" or type(config.get("format_version")) is not int or config["format_version"] != 1:
        raise ValueError("Packaging config must use qeapp-packaging format version 1")
    name = config.get("name")
    if not isinstance(name, str) or len(name) > 64 or not re.fullmatch(r"[a-z0-9]+(?:[-_][a-z0-9]+)*", name):
        raise ValueError("Package name must use lowercase letters, digits, hyphens or underscores, at most 64 characters, without extension")
    if name in {"con", "prn", "aux", "nul"} or re.fullmatch(r"(?:com|lpt)[1-9]", name):
        raise ValueError("Package name is reserved on Windows")
    return name


def load_sdk(path):
    spec = importlib.util.spec_from_file_location("qeaf_sdk", path)
    sdk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdk)
    return sdk


def load_qen():
    source = Path(__file__).resolve().with_name("qen.py")
    spec = importlib.util.spec_from_file_location("qeaf_qen", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_qenlink():
    source = Path(__file__).resolve().with_name("qenlink.py")
    spec = importlib.util.spec_from_file_location("qeaf_qenlink", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_qenrust():
    source = Path(__file__).resolve().with_name("qenrust.py")
    spec = importlib.util.spec_from_file_location("qeaf_qenrust", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def find_zig():
    env = os.environ.get("ZIG")
    if env and Path(env).is_file():
        return env
    base = os.environ.get("QEAFDROPOS") or "D:\\QeafivelsOfficial\\QeafdropOs"
    candidate = Path(base) / "out" / "toolchain" / "zig-windows-x86_64-0.13.0" / "zig.exe"
    if candidate.is_file():
        return str(candidate)
    raise ValueError("zig toolchain not found; set ZIG=/path/to/zig to compile native modules")


def compile_native(qen, project, arch, c_sources, entry, abi_version=5):
    """Compile freestanding PIC C sources into a real .qen module."""
    targets = {"x86_64": "x86_64-linux-gnu", "arm64": "aarch64-linux-musl"}
    if arch not in targets:
        raise ValueError("unsupported native architecture: %s" % arch)
    if abi_version not in (2, 3, 4, 5):
        raise ValueError("unsupported native abi_version: %s" % abi_version)
    qenlink = load_qenlink()
    zig = find_zig()
    api_include = Path(__file__).resolve().parents[2] / "framework" / "application_api"
    with tempfile.TemporaryDirectory(prefix="qencc-") as temp:
        objects = []
        for source in c_sources:
            obj = Path(temp) / (source.stem + ".o")
            cmd = [zig, "cc", "-target", targets[arch], "-fPIC", "-ffreestanding",
                   "-fno-stack-protector", "-fno-jump-tables", "-O2", "-std=c11",
                   "-Wall", "-Wextra", "-Werror", "-I", str(api_include),
                   "-I", str(project / "src"), "-c", str(source), "-o", str(obj)]
            if arch == "arm64":
                # A QEN module has no libc and the loader maps sections with
                # ordinary alignment; aarch64 raises an alignment fault on an
                # unaligned access. Keep LLVM from merging narrow fields into
                # wider or SIMD stores (observed as a Data Abort with
                # DFSC=0x21 inside reset_game on the QEMU harness).
                cmd.insert(4, "-mstrict-align")
            proc = subprocess.run(cmd, capture_output=True, text=True,
                                  encoding="utf-8", errors="replace")
            if proc.returncode != 0:
                raise ValueError("%s: native compile failed:\n%s"
                                 % (source.name, proc.stderr or proc.stdout))
            objects.append(obj)
        linked = qenlink.link_objects(objects, arch)
    if not linked["code"]:
        raise ValueError("native module has no code section")
    module = qen.build_module(linked["code"], linked["rodata"], linked["data"],
                              linked["bss_size"], linked["exports"], arch=arch,
                              min_api=abi_version, abi_version=abi_version,
                              entry_offset=linked["entry_offset"])
    output = project / entry
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(module)
    return module


def compile_project(project, mode, sdk):
    if mode not in ("release", "debug"):
        raise ValueError("Build mode must be release or debug")
    project = project.resolve()
    config = json.loads(project_file(project, "packaging/package.json").read_bytes())
    package_name = validate_packaging(config)
    manifest_data = project_file(project, "manifest.json").read_bytes()
    manifest = json.loads(manifest_data)
    errors = sdk.validate_manifest(manifest)
    if errors:
        raise ValueError("\n".join(errors))
    entry = manifest["entry"]
    native = manifest.get("runtime") == "native" or str(entry).endswith(".qen")
    language = None
    if native:
        if not entry or entry.startswith("/") or ".." in Path(entry).parts:
            raise ValueError("Unsafe native entry path")
        if len(entry.encode("utf-8")) >= 128:
            raise ValueError("Violet runtime entry path must be shorter than 128 bytes")
        sources = []
    else:
        if not entry.startswith("src/"):
            raise ValueError("The project entry must be inside src/")
        if len(entry.encode("utf-8")) >= 128:
            raise ValueError("Violet runtime entry path must be shorter than 128 bytes")
        sources = [entry] + [file.relative_to(project).as_posix()
                             for file in sorted((project / "libs").rglob("*.qe"))]
    compiled = []
    mapping = []
    originals = {}
    print_bytes = 0
    for name in sources:
        text = project_file(project, name).read_text(encoding="utf-8")
        errors = sdk.compile_qe(text)
        if errors:
            raise ValueError("\n".join(f"{name}: {error}" for error in errors))
        originals[name] = text
        for line_number, line in enumerate(text.splitlines(), 1):
            stripped = line.strip()
            if stripped.startswith("print "):
                print_bytes += len(stripped[6:].strip()[1:-1].encode("utf-8")) + 1
                if print_bytes >= 512:
                    raise ValueError(f"{name}: line {line_number}: combined print output exceeds 511 bytes")
            if mode == "release" and (not line.strip() or line.lstrip().startswith("#")):
                continue
            compiled.append(line)
            mapping.append({"compiled_line": len(compiled),
                            "source": name, "line": line_number})

    if native:
        qen = load_qen()
        arch = (manifest.get("architecture") or ["arm64"])[0]
        abi_version = int(manifest.get("abi_version") or 5)
        if abi_version not in (2, 3, 4, 5):
            raise ValueError("native abi_version must be 2, 3, 4 or 5, got %s"
                             % abi_version)
        language = config.get("language")
        if language not in (None, "c", "rust"):
            raise ValueError("Packaging language must be c or rust")
        c_sources = []
        rust_sources = []
        if (project / "src").is_dir():
            for pattern, destination in (("*.c", c_sources), ("*.rs", rust_sources)):
                for file in sorted((project / "src").rglob(pattern)):
                    project_file(project, file.relative_to(project).as_posix())
                    destination.append(file)
        if c_sources and rust_sources:
            raise ValueError("Native projects cannot mix C and Rust sources")
        if language == "rust" and not rust_sources:
            raise ValueError("Rust native projects require Rust sources under src/")
        if language == "c" and rust_sources:
            raise ValueError("C native projects cannot contain Rust sources")
        entry_path = project / entry
        native_sources = rust_sources if rust_sources else c_sources
        if rust_sources:
            local_sdk = project / "sdk" / "rust" / "src"
            module = load_qenrust().compile_rust(
                qen, load_qenlink(), sdk, project, arch, entry, abi_version,
                mode, local_sdk if local_sdk.is_dir() else None)
        elif c_sources:
            module = compile_native(qen, project, arch, c_sources, entry,
                                    abi_version)
        elif entry_path.is_file():
            module = project_file(project, entry).read_bytes()
        else:
            # Compiler stub: a valid, empty module for the target architecture.
            module = qen.build_stub(arch, min_api=abi_version,
                                    abi_version=abi_version)
        module_errors = qen.validate_module(module, arch)
        if module_errors:
            raise ValueError("\n".join("%s: %s" % (entry, e) for e in module_errors))
        sources = [file.relative_to(project).as_posix()
                   for file in native_sources]
        originals = {name: project_file(project, name).read_text(encoding="utf-8")
                     for name in sources}
        entries = {"manifest.json": manifest_data,
                   entry: module}
    else:
        entries = {"manifest.json": manifest_data,
                   entry: ("\n".join(compiled) + "\n").encode("utf-8")}
    for file in sorted((project / "assets").rglob("*")):
        if file.is_file():
            name = file.relative_to(project).as_posix()
            entries[name] = project_file(project, name).read_bytes()
    icon = manifest.get("icon")
    if icon != "assets/icon.rgba" or icon not in entries or len(entries[icon]) != 64 * 64 * 4:
        raise ValueError("Runtime icon must be assets/icon.rgba, 64x64 BGRA (16384 bytes)")

    if mode == "debug":
        source_map = {"format": "qe-debug", "format_version": 1,
                      "entry": entry, "lines": mapping}
        entries["debug/source-map.json"] = (
            json.dumps(source_map, indent=2) + "\n").encode("utf-8")
        for name, text in originals.items():
            entries[f"debug/sources/{name}"] = text.encode("utf-8")
    entries["SIGNATURE"] = sdk.package_signature(entries.items()).encode("ascii")
    if len(entries) > 32:
        raise ValueError("Violet runtime supports at most 32 package entries")

    output_dir = project / "build" / ("releases" if mode == "release" else "debug")
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / f"{package_name}.qeapp"
    with tempfile.TemporaryDirectory(prefix="qecompile-", dir=output_dir) as temp:
        package = Path(temp) / output.name
        with zipfile.ZipFile(package, "w", zipfile.ZIP_STORED) as archive:
            for name, data in sorted(entries.items()):
                archive.writestr(zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0)), data)
        if package.stat().st_size > 512 * 1024:
            raise ValueError("Violet runtime package limit is 512 KiB")
        errors = sdk.validate_package(package)
        if errors:
            raise ValueError("\n".join(errors))
        if language == "rust":
            sdk.load_qeapp().preview_package(package)
        package.replace(output)
    print(f"{mode}: compiled {len(sources)} sources, signed and validated {output}")
    return output


def main(argv=None):
    parser = argparse.ArgumentParser(description="Compile declarative QEAPP sources and libraries")
    parser.add_argument("--config", choices=("release", "debug", "all"), default="all")
    parser.add_argument("--project", type=Path, default=Path.cwd())
    parser.add_argument("--sdk", type=Path,
                        default=Path(__file__).resolve().parents[1] / "cli" / "qeaf.py")
    args = parser.parse_args(argv)
    try:
        sdk = load_sdk(args.sdk)
        for mode in (("release", "debug") if args.config == "all" else (args.config,)):
            compile_project(args.project, mode, sdk)
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
