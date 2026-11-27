#!/usr/bin/env python3
import argparse
from functools import lru_cache
import importlib.util
import json
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path, PurePosixPath

# A project path can be Vietnamese ("...\VQEAF Projects\Xếp Hình 4x4"), and the
# Windows console default codepage is cp1252: printing such a path used to
# abort the build with UnicodeEncodeError after the package was already
# written. Force UTF-8 with replacement so non-ASCII names never fail a build.
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

REQUIRED = {"format","format_version","id","name","version","min_os","entry","architecture","permissions","dependencies"}
REQUIRED_V2 = REQUIRED | {"runtime","abi_version"}
ID_RE = re.compile(r"^[a-z0-9]+(?:[.-][a-z0-9]+)+$")
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")
ENTRY_RE = re.compile(r"^[A-Za-z0-9_./-]+\.qe$")

def validate_manifest(m):
    errors = []
    if not isinstance(m, dict):
        return ["manifest root must be an object"]
    version = m.get("format_version")
    if version == 1:
        required = REQUIRED
    elif version == 2:
        required = REQUIRED_V2
    else:
        required = REQUIRED
    missing = sorted(required - set(m))
    if missing:
        errors.append("missing fields: " + ", ".join(missing))
    if m.get("format") != "qeapp":
        errors.append("format must be qeapp")
    if version not in (1, 2):
        errors.append("format_version must be 1 or 2")
    if not ID_RE.fullmatch(str(m.get("id", ""))):
        errors.append("id must use reverse-domain style")
    if not VERSION_RE.fullmatch(str(m.get("version", ""))):
        errors.append("version must be semantic x.y.z")
    if version == 2:
        if m.get("runtime") != "native":
            errors.append("runtime must be native for format_version 2")
        if not isinstance(m.get("abi_version"), int) or \
                m.get("abi_version") not in (2, 3, 4, 5):
            errors.append("abi_version must be 2, 3, 4 or 5")
        entry = str(m.get("entry", ""))
        if not re.fullmatch(r"^[A-Za-z0-9_./-]+\.qen$", entry) or \
                entry.startswith("/") or ".." in entry.split("/"):
            errors.append("entry must be a relative .qen path")
        if not isinstance(m.get("permissions"), list):
            errors.append("permissions must be an array")
    else:
        if not ENTRY_RE.fullmatch(str(m.get("entry", ""))):
            errors.append("entry must be a relative .qe path")
    arches = m.get("architecture")
    if not isinstance(arches, list) or not arches or \
            any(x not in ("arm64", "x86_64") for x in arches):
        errors.append("architecture must contain arm64 and/or x86_64")
    else:
        if any(arches.count(x) > 1 for x in arches):
            errors.append("architecture must not contain duplicates")
        if version == 2 and len(arches) != 1:
            errors.append("format_version 2 requires exactly one architecture")
    if "icon" in m and m["icon"] != "assets/icon.rgba":
        errors.append("Runtime icon must be assets/icon.rgba")
    for key in ("permissions", "dependencies"):
        if not isinstance(m.get(key), list):
            errors.append(key + " must be an array")
    return errors

def validate_package(package):
    errors = []
    try:
        with zipfile.ZipFile(package) as z:
            names = z.namelist()
            if len(names) != len(set(names)):
                errors.append("duplicate ZIP paths")
            for item in z.infolist():
                p = PurePosixPath(item.filename)
                if p.is_absolute() or ".." in p.parts or "\\" in item.filename:
                    errors.append("unsafe package path: " + item.filename)
                    continue
                mode = (item.external_attr >> 16) & 0o170000
                if mode == 0o120000:
                    errors.append("symbolic links are not allowed: " + item.filename)
            if "manifest.json" not in names:
                return errors + ["manifest.json missing"]
            manifest = json.loads(z.read("manifest.json"))
            errors.extend(validate_manifest(manifest))
            entry = manifest.get("entry")
            if entry not in names:
                errors.append("entry file missing: " + str(entry))
            elif str(entry).endswith(".qe"):
                errors.extend(compile_qe(z.read(entry).decode("utf-8", "replace")))
            elif str(entry).endswith(".qen"):
                # Phase 2: module is validated structurally; it is never loaded.
                data = z.read(entry)
                arches = manifest.get("architecture") or []
                expected = arches[0] if len(arches) == 1 else None
                for error in load_qen().validate_module(data, expected):
                    errors.append("native module %s: %s" % (entry, error))
    except (OSError, zipfile.BadZipFile, json.JSONDecodeError) as exc:
        errors.append(str(exc))
    return errors

APP_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

def compile_qe(text):
    """Syntax-check the declarative .qe entry script."""
    return load_qeapp().compile_qe(text)

def load_compiler():
    source = Path(__file__).resolve().parents[1] / "compiler" / "qecompile.py"
    spec = importlib.util.spec_from_file_location("qeaf_compiler", source)
    compiler = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(compiler)
    return compiler


@lru_cache(maxsize=1)
def load_qeapp():
    source = Path(__file__).resolve().parents[1] / "qeapp" / "package.py"
    spec = importlib.util.spec_from_file_location("qeapp_sdk", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_qen():
    source = Path(__file__).resolve().parents[1] / "compiler" / "qen.py"
    spec = importlib.util.spec_from_file_location("qeaf_qen", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_qesim():
    source = Path(__file__).resolve().parents[1] / "simulator" / "qesim.py"
    spec = importlib.util.spec_from_file_location("qeaf_qesim", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_simulate(package, device=None, panel_fb=None, list_devices=False):
    try:
        qesim = load_qesim()
        outdir = Path(__file__).resolve().parents[2] / "build" / "qeapp-simulator"
        exe = qesim.build_simulator(outdir)
    except (OSError, ValueError) as exc:
        print("ERROR:", exc, file=sys.stderr)
        return 1
    args = [str(exe)]
    if list_devices:
        args.append("--list-devices")
        return subprocess.run(args).returncode
    if package is None or not package.is_file():
        print("ERROR: package not found", file=sys.stderr)
        return 1
    args.append(str(package))
    if device:
        args.extend(["--device", device])
    if panel_fb:
        args.extend(["--panel-fb", str(panel_fb)])
    return subprocess.run(args).returncode


def main():
    parser = argparse.ArgumentParser(prog="qeaf", description="VQEAF OS native QEAPP SDK")
    sub = parser.add_subparsers(dest="command", required=True)
    create = sub.add_parser("create"); create.add_argument("name")
    create.add_argument("--id", help="Reverse-domain application identifier")
    create.add_argument("--package-name", help="Output basename without .qeapp extension")
    native_group = create.add_mutually_exclusive_group()
    native_group.add_argument("--native", action="store_true",
                              help="Scaffold a native C QEAPP project")
    native_group.add_argument("--rust", action="store_true",
                              help="Scaffold a freestanding Rust QEAPP project")
    create.add_argument("--arch", choices=("arm64", "x86_64"), default="arm64",
                        help="Native architecture (default: arm64)")
    validate = sub.add_parser("validate"); validate.add_argument("package")
    preview = sub.add_parser("preview", help="Inspect a signed package without installing or executing it")
    preview.add_argument("package")
    for command in ("build", "package"):
        build = sub.add_parser(command)
        build.add_argument("project", nargs="?", default=".")
        build.add_argument("--config", choices=("release", "debug", "all"), default="release")
        build.add_argument("--stored", action="store_true",
                           help="Use ZIP_STORED entries (always enabled)")
    sign = sub.add_parser("sign"); sign.add_argument("package")
    simulate = sub.add_parser("simulate")
    simulate.add_argument("package", nargs="?")
    simulate.add_argument("--device", help="Device VM profile, e.g. violet")
    simulate.add_argument("--panel-fb", help="Write the scaled device scanout .raw")
    simulate.add_argument("--list-devices", action="store_true",
                          help="List available device VM profiles")
    sub.add_parser("doctor")
    sub.add_parser("version")
    args = parser.parse_args()
    if args.command == "version":
        print("VQEAF SDK 0.1.0"); return 0
    if args.command == "doctor":
        print("Python:", sys.version.split()[0])
        print("QEAPP format: v1 declarative, v2 native")
        print("Package signatures: HMAC-SHA256 development key (not publisher authentication)")
        print("Native module execution: host simulator (x86_64) via 'qeaf simulate'; Violet device runtime")
        rust = load_compiler().load_qenrust()
        try:
            rustc = rust.find_rustc()
        except ValueError as exc:
            print("Rust: unavailable (%s)" % exc)
        else:
            print("Rust compiler:", rustc)
            for arch in ("arm64", "x86_64"):
                target = rust.rust_target(arch)
                state = "installed" if rust.target_installed(rustc, target) else "missing"
                print("Rust target %s: %s" % (target, state))
        return 0
    if args.command == "create":
        project = Path(args.name).absolute()
        name = project.name
        slug = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
        template_name = ("hello-qeapp-rust" if args.rust else
                         "hello-qeapp-native" if args.native else "hello-qeapp")
        template = Path(__file__).resolve().parents[1] / "templates" / template_name
        try:
            compiler = load_compiler()
            packaging = {"format": "qeapp-packaging", "format_version": 1,
                         "name": args.package_name if args.package_name is not None else slug}
            compiler.validate_packaging(packaging)
            manifest = json.loads((template / "manifest.json").read_text(encoding="utf-8"))
            manifest.update({"id": args.id if args.id is not None else "org.example." + slug,
                             "name": name})
            if args.native or args.rust:
                manifest.update({"entry": "native/%s/app.qen" % args.arch,
                                 "architecture": [args.arch]})
            errors = validate_manifest(manifest)
            if errors:
                raise ValueError("\n".join(errors))
            identifier = re.sub(r"[^A-Za-z0-9_]", "", name)
            if not identifier or identifier[0].isdigit():
                identifier = "App" + identifier
            script = f"app {identifier}\nprint {json.dumps('Hello from ' + name, ensure_ascii=False)}\n"
            shutil.copytree(template, project,
                            ignore=shutil.ignore_patterns("build", "__pycache__"))
            (project / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
            (project / "packaging" / "package.json").write_text(json.dumps(packaging, indent=2) + "\n", encoding="utf-8")
            if args.native or args.rust:
                module = project / manifest["entry"]
                module.parent.mkdir(parents=True, exist_ok=True)
                module.write_bytes(load_qen().build_stub(
                    args.arch, abi_version=manifest["abi_version"]))
                stale = project / "src" / "main.qe"
                if stale.is_file():
                    stale.unlink()
                if args.rust:
                    sdk_root = Path(__file__).resolve().parents[1] / "rust"
                    shutil.copytree(sdk_root, project / "sdk" / "rust",
                                    ignore=shutil.ignore_patterns("target", "__pycache__"))
                    cargo = project / "Cargo.toml"
                    cargo.write_text(cargo.read_text(encoding="utf-8").replace(
                        'path = "../../rust"', 'path = "sdk/rust"'), encoding="utf-8")
            else:
                (project / manifest["entry"]).write_text(script, encoding="utf-8")
            for folder in ("releases", "debug"):
                (project / "build" / folder).mkdir(parents=True)
        except (OSError, ValueError) as exc:
            print("ERROR:", exc, file=sys.stderr)
            return 1
        kind = ("Rust native QEAPP" if args.rust else
                "C native QEAPP" if args.native else "structured QEAPP")
        print("Created", project, "from the", kind, "template")
        return 0
    if args.command == "preview":
        try:
            metadata = load_qeapp().preview_package(Path(args.package))
        except load_qeapp().QEAPPError as exc:
            print(f"ERROR [{exc.code}]: {exc.message}", file=sys.stderr)
            return 1
        print(json.dumps(metadata, indent=2, sort_keys=True, ensure_ascii=False))
        return 0
    if args.command == "sign":
        return sign_package(Path(args.package))
    if args.command == "simulate":
        return run_simulate(Path(args.package) if args.package else None,
                            device=args.device, panel_fb=args.panel_fb,
                            list_devices=args.list_devices)
    if args.command == "validate":
        errors = validate_package(Path(args.package))
        if errors:
            print("\n".join("ERROR: "+e for e in errors)); return 1
        print("QEAPP package valid"); return 0
    return load_compiler().main(["--project", args.project, "--config", args.config,
                                 "--sdk", str(Path(__file__).resolve())])

DEV_KEY = load_qeapp().DEV_KEY
SIG_HEADER = load_qeapp().SIG_HEADER

def package_signature(entries):
    """HMAC over every ZIP entry except SIGNATURE, in name order."""
    return load_qeapp().package_signature(entries)

def sign_package(path):
    with zipfile.ZipFile(path) as archive:
        entries = [(name, archive.read(name)) for name in archive.namelist()]
    signature = package_signature(entries)
    payload = [(name, data) for name, data in entries if name != "SIGNATURE"]
    payload.append(("SIGNATURE", signature.encode("ascii")))
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as archive:
        for name, data in sorted(payload, key=lambda item: item[0]):
            archive.writestr(name, data)
    print("Signed", path)
    print("SIGNATURE", signature)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
