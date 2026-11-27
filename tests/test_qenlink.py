import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


qenlink = load_module("qenlink", ROOT / "sdk" / "compiler" / "qenlink.py")


def elf_object(code, symbol_name="target", defined=True, rtype=None, addend=-4):
    names = b"\0" + symbol_name.encode("ascii") + b"\0"
    section_names = b"\0.text\0.rela.text\0.symtab\0.strtab\0.shstrtab\0"
    text_off = 64 + 6 * 64
    rela_off = text_off + len(code)
    sym_off = rela_off + 24
    str_off = sym_off + 48
    shstr_off = str_off + len(names)
    blob = bytearray(shstr_off + len(section_names))
    ident = b"\x7fELF\x02\x01\x01" + b"\0" * 9
    blob[:64] = struct.pack(
        "<16sHHIQQQIHHHHHH", ident, 1, qenlink.EM_X86_64, 1, 0, 0, 64,
        0, 64, 0, 0, 64, 6, 5)
    blob[text_off:text_off + len(code)] = code
    rtype = qenlink.R_X86_64_GOTPCREL if rtype is None else rtype
    struct.pack_into("<QQq", blob, rela_off, 4, (1 << 32) | rtype, addend)
    struct.pack_into("<IBBHQQ", blob, sym_off + 24, 1, 0x10, 0,
                     1 if defined else 0, 0, 0)
    blob[str_off:str_off + len(names)] = names
    blob[shstr_off:] = section_names
    sections = [
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (1, 1, 6, 0, text_off, len(code), 0, 0, 1, 0),
        (7, 4, 0, 0, rela_off, 24, 3, 1, 8, 24),
        (18, 2, 0, 0, sym_off, 48, 4, 1, 8, 24),
        (26, 3, 0, 0, str_off, len(names), 0, 0, 1, 0),
        (34, 3, 0, 0, shstr_off, len(section_names), 0, 0, 1, 0),
    ]
    for index, section in enumerate(sections):
        struct.pack_into("<IIQQQQIIQQ", blob, 64 + index * 64, *section)
    return bytes(blob)


class X86RelocationTests(unittest.TestCase):
    def test_got_call_becomes_direct_call(self):
        code = bytearray(b"\x90\x90\xff\x15\0\0\0\0\x90")
        qenlink._apply_x86(code, 4, qenlink.R_X86_64_GOTPCREL, 0x100, -4)
        self.assertEqual(code[:8], b"\x90\x90\xe8" + struct.pack("<i", 0xF9) + b"\x90")

    def test_rex_got_jump_becomes_direct_jump(self):
        code = bytearray(b"\x90\x90\xff\x25\0\0\0\0\x90")
        qenlink._apply_x86(code, 4, qenlink.R_X86_64_REX_GOTPCRELX, 0x100, -4)
        self.assertEqual(code[:8], b"\x90\x90\xe9" + struct.pack("<i", 0xF9) + b"\x90")

    def test_got_load_becomes_lea(self):
        code = bytearray(b"\x90\x48\x8b\x05\0\0\0\0\x90")
        qenlink._apply_x86(code, 4, qenlink.R_X86_64_GOTPCREL, 0x100, -4)
        self.assertEqual(code, b"\x90\x48\x8d\x05" + struct.pack("<i", 0xF8) + b"\x90")

    def test_unsupported_got_instruction_rejected(self):
        code = bytearray(b"\x90\x48\x8b\x03\0\0\0\0\x90")
        with self.assertRaisesRegex(qenlink.LinkError, "unsupported x86_64 GOTPCREL"):
            qenlink._apply_x86(code, 4, qenlink.R_X86_64_GOTPCREL, 0x100, -4)

    def test_absolute_relocation_rejected(self):
        with self.assertRaisesRegex(qenlink.LinkError, "not position-independent"):
            qenlink._apply_x86(bytearray(8), 0, qenlink.R_X86_64_64, 0x100, 0)

    def test_defined_got_call_links_without_runtime_relocation(self):
        code = b"\xc3\x90\xff\x15\0\0\0\0\x90"
        with tempfile.TemporaryDirectory(prefix="qenlink-reloc-") as temp:
            path = Path(temp) / "probe.o"
            path.write_bytes(elf_object(code))
            linked = qenlink.link_objects([path], "x86_64")
        self.assertEqual(linked["code"][:8],
                         b"\xc3\x90\xe8" + struct.pack("<i", -7) + b"\x90")
        self.assertIn(("target", 0), linked["exports"])

    def test_undefined_symbol_rejected(self):
        with tempfile.TemporaryDirectory(prefix="qenlink-undef-") as temp:
            path = Path(temp) / "missing.o"
            path.write_bytes(elf_object(
                b"\xc3\x90\xff\x15\0\0\0\0\x90",
                symbol_name="missing", defined=False))
            with self.assertRaisesRegex(qenlink.LinkError, "undefined symbol: missing"):
                qenlink.link_objects([path], "x86_64")


class AArch64RelocationTests(unittest.TestCase):
    def test_call26_remains_position_independent(self):
        code = bytearray(struct.pack("<I", 0x94000000))
        qenlink._apply_aarch64(code, 0, qenlink.R_AARCH64_CALL26, 0x100, 0)
        self.assertEqual(struct.unpack("<I", code)[0], 0x94000040)

    def test_absolute_relocation_rejected(self):
        with self.assertRaisesRegex(qenlink.LinkError, "not position-independent"):
            qenlink._apply_aarch64(bytearray(4), 0,
                                   qenlink.R_AARCH64_ABS64, 0x100, 0)


if __name__ == "__main__":
    unittest.main()
