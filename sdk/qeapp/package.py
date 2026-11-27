"""Strict, read-only host inspection for QEAPP packages.

``preview_package`` never installs, extracts, or executes package content. The
HMAC is the existing shared-key development profile: it detects accidental or
deliberate tampering by someone without that key, but it does not authenticate
a publisher or provide a production trust model.

``QEAPPError.code`` is a stable uppercase identifier for callers that need to
branch on failures; ``message`` is concise English text for people.
"""

from functools import lru_cache
import hashlib
import hmac
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import zipfile


DEV_KEY = b"VQEAF-QEAPP-DEV-KEY-2026"
SIG_HEADER = b"VQEAF-QEAPP-V1\0"

MAX_PACKAGE_SIZE = 512 * 1024
MAX_PACKAGE_ENTRIES = 32
ICON_SIZE = 64 * 64 * 4
SUPPORTED_ARCHITECTURES = ("arm64", "x86_64")
KNOWN_PERMISSIONS = frozenset((
    "ui.display",
    "storage.read",
    "storage.write.app",
    "input.receive",
    "media.read",
))
ID_RE = re.compile(r"^[a-z0-9]+(?:[.-][a-z0-9]+)+$")
VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
ENTRY_QE_RE = re.compile(r"^[A-Za-z0-9_./-]+\.qe$")
ENTRY_QEN_RE = re.compile(r"^[A-Za-z0-9_./-]+\.qen$")
APP_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
REQUIRED_FIELDS = frozenset((
    "format", "format_version", "id", "name", "version", "min_os",
    "entry", "architecture", "permissions", "dependencies",
))


class QEAPPError(ValueError):
    """A structured QEAPP inspection failure."""

    def __init__(self, code, message):
        self.code = code
        self.message = message
        super().__init__("%s: %s" % (code, message))


def compile_qe(text):
    """Syntax-check the declarative .qe entry script.

    Returns a list of line-numbered errors (empty when the script compiles).
    The violet runtime executes only `app <Name>` and `print "<text>"`; this
    front-end rejects anything else before it is packaged.
    """
    errors = []
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("app "):
            name = line[4:].strip()
            if not APP_RE.fullmatch(name):
                errors.append(f"line {lineno}: app needs one identifier name")
        elif line.startswith("print "):
            body = line[6:].strip()
            if len(body) < 2 or body[0] != '"':
                errors.append(f"line {lineno}: print needs a quoted string")
                continue
            i, closed = 1, False
            while i < len(body):
                if body[i] == "\\":
                    i += 2
                    continue
                if body[i] == '"':
                    closed = True
                    break
                i += 1
            if not closed:
                errors.append(f"line {lineno}: unterminated string")
            elif body[i + 1:].strip():
                errors.append(f"line {lineno}: trailing text after string")
        else:
            errors.append(f"line {lineno}: unknown command (use app/print/#)")
    return errors


def package_signature(entries):
    """HMAC over every ZIP entry except SIGNATURE, in name order."""
    digest = hmac.new(DEV_KEY, SIG_HEADER, hashlib.sha256)
    for name, data in sorted(entries, key=lambda item: item[0]):
        if name == "SIGNATURE":
            continue
        digest.update(name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(struct.pack("<I", len(data)))
        digest.update(data)
    return digest.hexdigest()


@lru_cache(maxsize=1)
def _load_qen():
    """Load the sibling native validator in repository or copied SDK layouts."""
    try:
        from ..compiler import qen
        return qen
    except ImportError:
        pass
    source = Path(__file__).resolve().parents[1] / "compiler" / "qen.py"
    try:
        spec = importlib.util.spec_from_file_location("qeapp_qen", source)
        if spec is None or spec.loader is None:
            raise ImportError("cannot create a module spec")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    except (ImportError, OSError) as exc:
        raise QEAPPError("SDK_ERROR", "cannot load the QEAPP native validator: %s" % exc) from exc


def _safe_entry_name(name):
    if not isinstance(name, str) or not name:
        return False
    if name.startswith("/") or PurePosixPath(name).is_absolute():
        return False
    if "\\" in name or "\0" in name or ":" in name:
        return False
    if any(ord(char) < 0x20 or ord(char) == 0x7F for char in name):
        return False
    return all(part not in ("", ".", "..") for part in name.split("/"))


def _manifest_error(message):
    raise QEAPPError("MANIFEST_INVALID", message)


def _json_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON field: " + key)
        result[key] = value
    return result


def _json_constant(value):
    raise ValueError("non-standard JSON constant: " + value)


def _parse_manifest(data):
    try:
        text = data.decode("utf-8")
        manifest = json.loads(text, object_pairs_hook=_json_object,
                              parse_constant=_json_constant)
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError, RecursionError) as exc:
        raise QEAPPError("MANIFEST_INVALID", "manifest.json is not valid JSON: %s" % exc) from exc
    if not isinstance(manifest, dict):
        _manifest_error("manifest root must be an object")
    return manifest


def _validate_manifest(manifest):
    missing = sorted(REQUIRED_FIELDS - set(manifest))
    if missing:
        _manifest_error("missing fields: " + ", ".join(missing))

    for key in ("format", "id", "name", "version", "min_os", "entry"):
        if not isinstance(manifest[key], str):
            _manifest_error(key + " must be a string")
    if manifest["format"] != "qeapp":
        _manifest_error("format must be qeapp")
    version = manifest["format_version"]
    if type(version) is not int or version not in (1, 2):
        _manifest_error("format_version must be integer 1 or 2")

    app_id = manifest["id"]
    if not ID_RE.fullmatch(app_id) or len(app_id.encode("utf-8")) >= 64:
        _manifest_error("id must use reverse-domain style and be shorter than 64 bytes")
    name = manifest["name"]
    if (not name.strip() or len(name.encode("utf-8")) >= 64 or
            any(ord(char) < 0x20 or ord(char) == 0x7F for char in name)):
        _manifest_error("name must be non-empty, shorter than 64 bytes, and contain no controls")
    for key in ("version", "min_os"):
        value = manifest[key]
        if not VERSION_RE.fullmatch(value) or len(value.encode("utf-8")) >= 24:
            _manifest_error(key + " must be semantic x.y.z and shorter than 24 bytes")

    architecture = manifest["architecture"]
    if (not isinstance(architecture, list) or not architecture or
            any(not isinstance(item, str) or item not in SUPPORTED_ARCHITECTURES
                for item in architecture)):
        _manifest_error("architecture must be a non-empty array containing arm64 and/or x86_64")
    if len(set(architecture)) != len(architecture):
        _manifest_error("architecture must not contain duplicates")
    if version == 2 and len(architecture) != 1:
        _manifest_error("format_version 2 requires exactly one architecture")

    permissions = manifest["permissions"]
    if (not isinstance(permissions, list) or
            any(not isinstance(item, str) or item not in KNOWN_PERMISSIONS
                for item in permissions)):
        _manifest_error("permissions must be an array of known permission names")
    dependencies = manifest["dependencies"]
    if (not isinstance(dependencies, list) or
            any(not isinstance(item, str) or not item for item in dependencies)):
        _manifest_error("dependencies must be an array of non-empty strings")

    runtime = manifest.get("runtime")
    if version == 1:
        if "runtime" in manifest and runtime != "script":
            _manifest_error("format_version 1 runtime must be absent or script")
    else:
        if runtime != "native":
            _manifest_error("runtime must be native for format_version 2")
        abi = manifest.get("abi_version")
        if type(abi) is not int or abi not in (2, 3, 4, 5):
            _manifest_error("abi_version must be integer 2, 3, 4 or 5")

    entry = manifest["entry"]
    suffix = ".qe" if version == 1 else ".qen"
    pattern = ENTRY_QE_RE if version == 1 else ENTRY_QEN_RE
    if (not _safe_entry_name(entry) or not pattern.fullmatch(entry) or
            len(entry.encode("utf-8")) >= 128):
        _manifest_error("entry must be a safe relative %s path shorter than 128 bytes" % suffix)
    if "icon" in manifest:
        icon = manifest["icon"]
        if not isinstance(icon, str) or not _safe_entry_name(icon):
            _manifest_error("icon must be a safe relative path")
        if icon != "assets/icon.rgba":
            _manifest_error("Runtime icon must be assets/icon.rgba")
    return manifest


def _validate_native(data, manifest):
    qen = _load_qen()
    architecture = manifest["architecture"][0]
    errors = qen.validate_module(data, architecture)
    if errors:
        raise QEAPPError("MODULE_INVALID", "native module %s: %s" %
                         (manifest["entry"], "; ".join(errors)))

    # Empty compiler stubs have no import table. When imports are present they
    # must name exactly the API generation declared by the manifest; the module
    # ABI and min_api must agree even for an empty stub.
    module_abi = data[10]
    manifest_abi = manifest["abi_version"]
    if module_abi != manifest_abi:
        raise QEAPPError("MODULE_INVALID",
                         "module abi_version %d does not match manifest abi_version %d" %
                         (module_abi, manifest_abi))
    min_api = struct.unpack_from("<H", data, 53)[0]
    if min_api != manifest_abi:
        raise QEAPPError("MODULE_INVALID",
                         "module min_api %d does not match manifest abi_version %d" %
                         (min_api, manifest_abi))
    module_arch = qen.ARCH_BY_ID.get(data[11])
    if module_arch != architecture:
        raise QEAPPError("MODULE_INVALID",
                         "module architecture %s does not match manifest %s" %
                         (module_arch, architecture))
    try:
        imports, _ = qen.read_tables(data)
    except (ValueError, UnicodeDecodeError) as exc:
        raise QEAPPError("MODULE_INVALID", "native module import table is invalid: %s" % exc) from exc
    expected_import = qen.IMPORT_BY_ABI[manifest_abi]
    known_imports = {name for name in imports if name in qen.IMPORT_BY_ABI.values()}
    if imports and known_imports != {expected_import}:
        raise QEAPPError("MODULE_INVALID",
                         "module imports do not match manifest abi_version %d" % manifest_abi)


def _raw_zip_name(data, offset, central):
    """Decode a name without ZipInfo's Windows backslash normalization."""
    signature = b"PK\x01\x02" if central else b"PK\x03\x04"
    name_offset = 46 if central else 30
    if offset < 0 or offset + name_offset > len(data) or data[offset:offset + 4] != signature:
        raise QEAPPError("INVALID_ZIP", "package has a malformed ZIP directory")
    flag_offset = 8 if central else 6
    length_offset = 28 if central else 26
    flags = struct.unpack_from("<H", data, offset + flag_offset)[0]
    length = struct.unpack_from("<H", data, offset + length_offset)[0]
    start = offset + name_offset
    end = start + length
    if end > len(data):
        raise QEAPPError("INVALID_ZIP", "package has a truncated ZIP entry name")
    raw = data[start:end]
    try:
        name = raw.decode("utf-8" if flags & 0x0800 else "cp437")
    except UnicodeDecodeError as exc:
        raise QEAPPError("INVALID_ZIP", "package has an invalid ZIP entry name encoding") from exc
    return name, raw, flags


def _verified_zip_names(data, archive, infos):
    """Return central names after checking matching raw local headers."""
    offset = archive.start_dir
    for info in infos:
        central_name, central_raw, central_flags = _raw_zip_name(data, offset, True)
        local_name, local_raw, local_flags = _raw_zip_name(data, info.header_offset, False)
        for name in (central_name, local_name):
            if not _safe_entry_name(name):
                raise QEAPPError("UNSAFE_ENTRY_PATH", "unsafe ZIP entry path: %r" % name)
        if central_raw != local_raw or central_flags != local_flags:
            raise QEAPPError("INVALID_ZIP", "local and central ZIP entry metadata differ")
        if (central_flags | local_flags) & 0x0008:
            raise QEAPPError("INVALID_ZIP", "ZIP data descriptors are not allowed: " + central_name)
        encrypted_flags = 0x0001 | 0x0040 | 0x2000
        if (central_flags | local_flags) & encrypted_flags:
            raise QEAPPError("ENCRYPTED_ENTRY", "encrypted ZIP entry is not allowed: " + central_name)
        local_method = struct.unpack_from("<H", data, info.header_offset + 8)[0]
        if info.compress_type != zipfile.ZIP_STORED or local_method != zipfile.ZIP_STORED:
            raise QEAPPError("COMPRESSION_UNSUPPORTED",
                             "ZIP entry must use ZIP_STORED: " + central_name)
        yield central_name
        name_length = struct.unpack_from("<H", data, offset + 28)[0]
        extra_length = struct.unpack_from("<H", data, offset + 30)[0]
        comment_length = struct.unpack_from("<H", data, offset + 32)[0]
        offset += 46 + name_length + extra_length + comment_length


def _validate_eocd(data):
    search_end = max(0, len(data) - 22 - 65535)
    offset = data.rfind(b"PK\x05\x06", search_end)
    while offset >= 0:
        if offset + 22 <= len(data):
            comment_size = struct.unpack_from("<H", data, offset + 20)[0]
            if offset + 22 + comment_size == len(data):
                return
        offset = data.rfind(b"PK\x05\x06", search_end, offset)
    raise QEAPPError("INVALID_ZIP", "package has a malformed EOCD or trailing data")


def _inspect_archive(data, package_path):
    _validate_eocd(data)
    try:
        archive = zipfile.ZipFile(io.BytesIO(data))
    except (zipfile.BadZipFile, zipfile.LargeZipFile, UnicodeDecodeError, ValueError) as exc:
        raise QEAPPError("INVALID_ZIP", "package is not a valid ZIP archive: %s" % exc) from exc

    with archive:
        infos = archive.infolist()
        if len(infos) > MAX_PACKAGE_ENTRIES:
            raise QEAPPError("TOO_MANY_ENTRIES",
                             "package exceeds %d ZIP entries" % MAX_PACKAGE_ENTRIES)
        raw_names = list(_verified_zip_names(data, archive, infos))
        names = []
        seen = set()
        total_uncompressed = 0
        for info, name in zip(infos, raw_names):
            if name in seen:
                raise QEAPPError("DUPLICATE_ENTRY", "duplicate ZIP entry path: " + name)
            seen.add(name)
            names.append(name)
            mode = (info.external_attr >> 16) & 0o170000
            if mode == 0o120000:
                raise QEAPPError("UNSAFE_ENTRY_TYPE", "symbolic links are not allowed: " + name)
            total_uncompressed += info.file_size
            if total_uncompressed > MAX_PACKAGE_SIZE:
                raise QEAPPError("PACKAGE_TOO_LARGE",
                                 "package contents exceed 512 KiB")

        for required in ("manifest.json", "SIGNATURE"):
            if names.count(required) != 1:
                code = "MANIFEST_MISSING" if required == "manifest.json" else "SIGNATURE_MISSING"
                raise QEAPPError(code, required + " must appear exactly once")

        entries = {}
        for info, name in zip(infos, raw_names):
            try:
                entries[name] = archive.read(info)
            except (OSError, RuntimeError, ValueError, EOFError, NotImplementedError,
                    zipfile.BadZipFile) as exc:
                raise QEAPPError("PACKAGE_READ_ERROR",
                                 "cannot read ZIP entry %s: %s" % (name, exc)) from exc

    signature_data = entries["SIGNATURE"]
    if not re.fullmatch(rb"[0-9a-f]{64}", signature_data):
        raise QEAPPError("SIGNATURE_INVALID", "SIGNATURE must contain 64 lowercase hexadecimal characters")
    expected = package_signature(entries.items())
    if not hmac.compare_digest(signature_data, expected.encode("ascii")):
        raise QEAPPError("SIGNATURE_INVALID",
                         "development signature is invalid or the package was modified after signing")

    manifest = _validate_manifest(_parse_manifest(entries["manifest.json"]))
    entry = manifest["entry"]
    if entry not in entries:
        raise QEAPPError("ENTRY_MISSING", "manifest entry file is missing: " + entry)

    native = manifest["format_version"] == 2
    if native:
        _validate_native(entries[entry], manifest)
    else:
        try:
            script = entries[entry].decode("utf-8")
        except UnicodeDecodeError as exc:
            raise QEAPPError("SCRIPT_INVALID", "entry script is not valid UTF-8: %s" % exc) from exc
        errors = compile_qe(script)
        if errors:
            raise QEAPPError("SCRIPT_INVALID", "%s: %s" % (entry, "; ".join(errors)))

    icon = entries.get("assets/icon.rgba")
    if "icon" in manifest and icon is None:
        raise QEAPPError("ICON_MISSING", "manifest icon file is missing: " + manifest["icon"])
    if icon is not None and len(icon) != ICON_SIZE:
        raise QEAPPError("ICON_INVALID",
                         "assets/icon.rgba must contain exactly %d bytes" % ICON_SIZE)

    return {
        "format": manifest["format"],
        "format_version": manifest["format_version"],
        "id": manifest["id"],
        "name": manifest["name"],
        "version": manifest["version"],
        "min_os": manifest["min_os"],
        "entry": entry,
        "runtime": "native" if native else "script",
        "native": native,
        "abi_version": manifest["abi_version"] if native else None,
        "architecture": list(manifest["architecture"]),
        "permissions": list(manifest["permissions"]),
        "dependencies": list(manifest["dependencies"]),
        "has_icon": icon is not None,
        "icon_size": len(icon) if icon is not None else 0,
        "package_path": package_path,
        "package_size": len(data),
        "package_sha256": hashlib.sha256(data).hexdigest(),
        "signature_verified": True,
        "signature_scheme": "development-hmac-sha256",
        "publisher_authenticated": False,
    }


def preview_package(path):
    """Return verified metadata for a local .qeapp file without changing it.

    The package must be a regular local file, at most 512 KiB, and use only
    safe, unique, unencrypted ZIP_STORED entries. Both declarative v1 and
    native v2 packages require a valid development HMAC. Native modules are
    structurally validated only and are never executed.
    """
    try:
        package = Path(path)
        package_path = os.path.abspath(os.fspath(package))
        info = package.lstat()
    except (TypeError, ValueError) as exc:
        raise QEAPPError("INVALID_PATH", "package path is invalid: %s" % exc) from exc
    except FileNotFoundError as exc:
        raise QEAPPError("PACKAGE_NOT_FOUND", "package file not found: %s" % package_path) from exc
    except OSError as exc:
        raise QEAPPError("PACKAGE_READ_ERROR", "cannot inspect package file: %s" % exc) from exc
    if not stat.S_ISREG(info.st_mode):
        raise QEAPPError("PACKAGE_NOT_REGULAR", "package must be a regular local file")
    if info.st_size > MAX_PACKAGE_SIZE:
        raise QEAPPError("PACKAGE_TOO_LARGE", "package exceeds 512 KiB")

    try:
        with package.open("rb") as stream:
            opened = os.fstat(stream.fileno())
            if not stat.S_ISREG(opened.st_mode):
                raise QEAPPError("PACKAGE_NOT_REGULAR", "package must be a regular local file")
            if opened.st_size > MAX_PACKAGE_SIZE:
                raise QEAPPError("PACKAGE_TOO_LARGE", "package exceeds 512 KiB")
            data = stream.read(MAX_PACKAGE_SIZE + 1)
    except QEAPPError:
        raise
    except OSError as exc:
        raise QEAPPError("PACKAGE_READ_ERROR", "cannot read package file: %s" % exc) from exc
    if len(data) > MAX_PACKAGE_SIZE:
        raise QEAPPError("PACKAGE_TOO_LARGE", "package exceeds 512 KiB")
    return _inspect_archive(data, package_path)
