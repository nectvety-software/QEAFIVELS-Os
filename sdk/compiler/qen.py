"""QEAF native module (``.qen``) header, stub builder, and validator.

This module is the single source of truth for the module header layout that
``platform/violet/ui/vqeaf_qeapp_native.c`` validates on the device. It never
executes native code; it only checks structure, sizes, and the payload hash.

Header layout (64 bytes, little-endian)::

    0   char[9]  magic "QEAF-QEN1"
    9   u8       module_version (1)
    10  u8       abi_version (2/3/4/5)
    11  u8       arch (1 = arm64, 2 = x86_64)
    12  u8       flags (0)
    13  u16      header_size (64)
    15  u16      section_count (<= 16)
    17  u32      module_size (total bytes)
    21  u32      code_size
    25  u32      rodata_size
    29  u32      data_size
    33  u32      bss_size
    37  u32      entry_offset
    41  u32      imports_size
    45  u32      exports_size
    49  u32      payload_hash (FNV-1a 32 over payload)
    53  u16      min_api
    55  u8[9]    reserved (zero)
"""

import struct

MAGIC = b"QEAF-QEN1"
MODULE_VERSION = 1
ABI_VERSION = 2
HEADER_SIZE = 64
MAX_MODULE_SIZE = 8 * 1024 * 1024
MAX_SECTIONS = 16

ARCH_ARM64 = 1
ARCH_X86_64 = 2
ARCH_NAMES = {"arm64": ARCH_ARM64, "x86_64": ARCH_X86_64}
ARCH_BY_ID = {value: name for name, value in ARCH_NAMES.items()}

# The only framework imports a module may declare (see qeaf_native_abi.h).
# The import names a module declares are a capability check; the API table
# itself is passed to create(). v3 adds blit_pixels/read_file_part; v4 adds
# draw_text_weighted (typography weight); v5 adds layout_split_weighted
# (proportional layout weight).
IMPORT_API_V2 = "qeaf_app_api_v2"
IMPORT_API_V3 = "qeaf_app_api_v3"
IMPORT_API_V4 = "qeaf_app_api_v4"
IMPORT_API_V5 = "qeaf_app_api_v5"
IMPORT_BY_ABI = {2: IMPORT_API_V2, 3: IMPORT_API_V3, 4: IMPORT_API_V4,
                 5: IMPORT_API_V5}

_HEADER = struct.Struct("<9sBBBBHHIIIIIIIIIH9s")


def arch_id(name):
    """Return the numeric architecture id for a manifest architecture name."""
    return ARCH_NAMES.get(str(name), 0)


def fnv1a32(data):
    value = 0x811C9DC5
    for byte in data:
        value ^= byte
        value = (value * 0x01000193) & 0xFFFFFFFF
    return value


def build_stub(arch="arm64", min_api=None, abi_version=None):
    """Build a structurally valid module with no code.

    The stub is what ``qeaf create --native`` and the compiler emit when no
    compiled module exists yet. It is valid to ship and validate, but it has
    an empty code section, so a future loader will refuse to start it.
    ``abi_version`` defaults to the frozen module baseline; the compiler
    passes the manifest ABI so new projects emit the current default.
    """
    arch_value = arch_id(arch)
    if arch_value == 0:
        raise ValueError("unsupported native architecture: %s" % arch)
    abi = ABI_VERSION if abi_version is None else int(abi_version)
    if abi not in IMPORT_BY_ABI:
        raise ValueError("unsupported module abi_version: %s" % abi)
    min_api = abi if min_api is None else int(min_api)
    payload = b""
    header = _HEADER.pack(
        MAGIC,
        MODULE_VERSION,
        abi,
        arch_value,
        0,
        HEADER_SIZE,
        0,
        HEADER_SIZE + len(payload),
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        fnv1a32(payload),
        min_api,
        b"\0" * 9,
    )
    assert len(header) == HEADER_SIZE
    return header + payload


def _pack_imports(names):
    out = bytearray()
    for name in names:
        raw = str(name).encode("ascii")
        if not raw or b"\0" in raw:
            raise ValueError("import name must be non-empty ASCII without NUL")
        out += raw + b"\0"
    return bytes(out)


def _pack_exports(exports):
    out = bytearray()
    for name, offset in exports:
        raw = str(name).encode("ascii")
        if not raw or b"\0" in raw:
            raise ValueError("export name must be non-empty ASCII without NUL")
        out += raw + b"\0" + struct.pack("<I", int(offset))
    return bytes(out)


def build_module(code, rodata=b"", data=b"", bss_size=0, exports=(),
                 imports=None, arch="x86_64", min_api=2, abi_version=2,
                 entry_offset=None):
    """Build a real module from linked sections.

    Payload layout: code | rodata | data | imports | exports (bss is not
    stored; the loader zero-fills it). ``exports`` is a list of (name, offset)
    pairs into the code section. ``entry_offset`` defaults to the ``create``
    export when present, else 0. ``abi_version`` is the module ABI revision
    (2 = frozen v2 table, 3 = v3 with blit_pixels/read_file_part, 4 = v4 with
    draw_text_weighted, 5 = v5 with layout_split_weighted); the import name
    must match the ABI revision unless ``imports`` is given explicitly.
    """
    arch_value = arch_id(arch)
    if arch_value == 0:
        raise ValueError("unsupported native architecture: %s" % arch)
    if abi_version not in (2, 3, 4, 5):
        raise ValueError("unsupported module abi_version: %s" % abi_version)
    if imports is None:
        imports = (IMPORT_BY_ABI[abi_version],)
    if int(min_api) != int(abi_version):
        raise ValueError("min_api must equal abi_version (%s != %s)"
                         % (min_api, abi_version))
    code = bytes(code)
    rodata = bytes(rodata)
    data = bytes(data)
    imports_blob = _pack_imports(list(imports))
    exports_blob = _pack_exports(list(exports))
    if entry_offset is None:
        entry_offset = next((off for name, off in exports if name == "create"), 0)
    if code and entry_offset >= len(code):
        raise ValueError("entry_offset outside code section")
    payload = code + rodata + data + imports_blob + exports_blob
    section_count = sum(1 for s in (code, rodata, data, imports_blob, exports_blob) if s)
    if bss_size:
        section_count += 1
    header = _HEADER.pack(
        MAGIC, MODULE_VERSION, int(abi_version), arch_value, 0, HEADER_SIZE,
        section_count, HEADER_SIZE + len(payload), len(code), len(rodata),
        len(data), int(bss_size), int(entry_offset), len(imports_blob),
        len(exports_blob), fnv1a32(payload), int(min_api), b"\0" * 9)
    assert len(header) == HEADER_SIZE
    return header + payload


def read_tables(data):
    """Return (imports, exports) parsed from a valid module.

    exports is a list of (name, offset). Raises ValueError on bad structure.
    """
    if len(data) < HEADER_SIZE:
        raise ValueError("module shorter than header")
    (code_size, rodata_size, data_size, imports_size, exports_size) = (
        struct.unpack_from("<I", data, 21)[0],
        struct.unpack_from("<I", data, 25)[0],
        struct.unpack_from("<I", data, 29)[0],
        struct.unpack_from("<I", data, 41)[0],
        struct.unpack_from("<I", data, 45)[0])
    imports_off = HEADER_SIZE + code_size + rodata_size + data_size
    exports_off = imports_off + imports_size
    imports = []
    i = imports_off
    end = imports_off + imports_size
    while i < end:
        j = data.find(b"\0", i, end)
        if j <= i:
            raise ValueError("bad imports table")
        imports.append(data[i:j].decode("ascii"))
        i = j + 1
    exports = []
    i = exports_off
    end = exports_off + exports_size
    while i < end:
        j = data.find(b"\0", i, end)
        if j <= i or j + 5 > end:
            raise ValueError("bad exports table")
        name = data[i:j].decode("ascii")
        offset = struct.unpack_from("<I", data, j + 1)[0]
        exports.append((name, offset))
        i = j + 5
    return imports, exports


def validate_module(data, expected_arch=None):
    """Validate a module without executing it.

    Returns a list of human-readable errors; an empty list means the module is
    structurally valid. ``expected_arch`` may be an architecture name or id.
    """
    errors = []
    if not isinstance(data, (bytes, bytearray)):
        return ["module must be bytes"]
    if len(data) < HEADER_SIZE:
        return ["module shorter than %d-byte header" % HEADER_SIZE]
    if len(data) > MAX_MODULE_SIZE:
        return ["module exceeds %d bytes" % MAX_MODULE_SIZE]
    (magic, module_version, abi_version, arch, flags, header_size,
     section_count, module_size, code_size, rodata_size, data_size, bss_size,
     entry_offset, imports_size, exports_size, payload_hash, min_api,
     reserved) = _HEADER.unpack_from(data, 0)
    if magic != MAGIC:
        errors.append("bad module magic")
    if module_version != MODULE_VERSION:
        errors.append("module_version must be %d" % MODULE_VERSION)
    if abi_version not in (2, 3, 4, 5):
        errors.append("abi_version must be 2, 3, 4 or 5")
    if abi_version in (2, 3, 4, 5) and code_size and min_api != abi_version:
        errors.append("min_api must equal abi_version")
    if abi_version in (2, 3, 4, 5) and imports_size:
        i = HEADER_SIZE + code_size + rodata_size + data_size
        end = i + imports_size
        seen = set()
        while i < end:
            j = data.find(b"\0", i, end)
            if j <= i:
                break
            seen.add(data[i:j].decode("ascii", "replace"))
            i = j + 1
        mine = IMPORT_BY_ABI[abi_version]
        if mine not in seen:
            errors.append("module must import %s for abi_version %d"
                          % (mine, abi_version))
        for other_abi, other_name in IMPORT_BY_ABI.items():
            if other_abi != abi_version and other_name in seen:
                errors.append("module must not import %s for abi_version %d"
                              % (other_name, abi_version))
    if arch not in ARCH_BY_ID:
        errors.append("unknown architecture id %d" % arch)
    if flags != 0:
        errors.append("flags must be zero")
    if header_size != HEADER_SIZE:
        errors.append("header_size must be %d" % HEADER_SIZE)
    if section_count > MAX_SECTIONS:
        errors.append("section_count exceeds %d" % MAX_SECTIONS)
    if module_size != len(data):
        errors.append("module_size does not match file length")
    if module_size > MAX_MODULE_SIZE:
        errors.append("module_size exceeds %d bytes" % MAX_MODULE_SIZE)
    payload_size = max(0, len(data) - HEADER_SIZE)
    if code_size + rodata_size + data_size + imports_size + exports_size > payload_size:
        errors.append("section sizes exceed module payload")
    else:
        imports_off = HEADER_SIZE + code_size + rodata_size + data_size
        exports_off = imports_off + imports_size
        i, end = imports_off, imports_off + imports_size
        while i < end:
            j = data.find(b"\0", i, end)
            if j < 0:
                errors.append("imports table not NUL-terminated")
                break
            if j == i:
                errors.append("empty import name")
                break
            if any(c < 0x20 or c > 0x7E for c in data[i:j]):
                errors.append("import name not printable ASCII")
                break
            i = j + 1
        i, end = exports_off, exports_off + exports_size
        while i < end:
            j = data.find(b"\0", i, end)
            if j < 0:
                errors.append("exports table not NUL-terminated")
                break
            if j == i:
                errors.append("empty export name")
                break
            if j + 5 > end:
                errors.append("export record truncated")
                break
            offset = struct.unpack_from("<I", data, j + 1)[0]
            if code_size and offset >= code_size:
                errors.append("export offset outside code section")
            i = j + 5
    if code_size and entry_offset >= code_size:
        errors.append("entry_offset outside code section")
    if payload_hash != fnv1a32(bytes(data[HEADER_SIZE:])):
        errors.append("payload hash mismatch")
    if any(reserved):
        errors.append("reserved header bytes must be zero")
    if expected_arch is not None:
        expected = expected_arch
        if isinstance(expected, str):
            expected = arch_id(expected)
        if expected and arch != expected:
            errors.append("architecture mismatch: module %s, manifest %s" % (
                ARCH_BY_ID.get(arch, str(arch)),
                ARCH_BY_ID.get(expected, str(expected))))
    return errors
