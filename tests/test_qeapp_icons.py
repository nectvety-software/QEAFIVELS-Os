import importlib.util
import re
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


converter = load_module("qeapp_icon_converter", ROOT / "scripts" / "png_to_qeapp_icon.py")
generator = load_module("qeapp_icon_generator", ROOT / "scripts" / "make_vqeaf_pixel_icons.py")


def png_chunk(tag, payload):
    return (struct.pack(">I", len(payload)) + tag + payload +
            struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


class IconConverterTests(unittest.TestCase):
    def test_alpha_edges_borrow_opaque_color_without_changing_alpha(self):
        rows = [bytearray((255, 0, 0, 255, 0, 0, 0, 128, 0, 0, 0, 0))]
        result = converter.bleed_alpha_edges(3, 1, 4, rows)
        self.assertEqual(result[0][:3], bytes((255, 0, 0)))
        self.assertEqual(result[0][4:7], bytes((255, 0, 0)))
        self.assertEqual(result[0][8:11], bytes((255, 0, 0)))
        self.assertEqual(result[0][3::4], bytes((255, 128, 0)))

    def test_rgb_input_is_unchanged(self):
        rows = [bytearray((1, 2, 3, 4, 5, 6))]
        self.assertEqual(converter.bleed_alpha_edges(2, 1, 3, rows), rows)

    def test_png_conversion_removes_black_alpha_fringe(self):
        ihdr = struct.pack(">IIBBBBB", 2, 1, 8, 6, 0, 0, 0)
        pixels = bytes((255, 0, 0, 255, 0, 0, 0, 128))
        png = (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", ihdr) +
               png_chunk(b"IDAT", zlib.compress(b"\0" + pixels)) +
               png_chunk(b"IEND", b""))
        with tempfile.TemporaryDirectory(prefix="qeapp-icon-") as temp:
            source = Path(temp) / "icon.png"
            output = Path(temp) / "icon.rgba"
            source.write_bytes(png)
            self.assertEqual(converter.main(["png_to_qeapp_icon.py", str(source),
                                             str(output)]), 0)
            data = output.read_bytes()
        self.assertEqual(len(data), 64 * 64 * 4)
        for index in range(0, len(data), 4):
            if 0 < data[index + 3] < 255:
                self.assertNotEqual(data[index:index + 3], b"\0\0\0")
        self.assertIn(bytes((0, 0, 255, 255)), data)
        self.assertIn(bytes((0, 0, 255, 128)), data)


class SystemIconGeneratorTests(unittest.TestCase):
    def test_badge_has_no_black_outer_frame(self):
        canvas = generator.Canvas(generator.BG)
        generator.badge(canvas, generator.BLUE)
        self.assertNotEqual(canvas.px[2 * generator.SIZE + 5], generator.INK)
        self.assertNotEqual(canvas.px[2 * generator.SIZE + 26], generator.INK)

    def test_export_is_argb8888_with_transparent_corners(self):
        class FakeVpe:
            @staticmethod
            def load_vpe(_path):
                return generator.SIZE, generator.SIZE, [0] * (generator.SIZE ** 2)

        source = generator.c_source(FakeVpe())
        self.assertIn(".cf = LV_COLOR_FORMAT_ARGB8888", source)
        self.assertIn(".stride = 128", source)
        payload = re.search(r"app_icon_home_data\[\] = \{(.*?)\};", source, re.S)
        self.assertIsNotNone(payload)
        values = [int(value) for value in re.findall(r"\d+", payload.group(1))]
        self.assertEqual(len(values), generator.SIZE * generator.SIZE * 4)
        self.assertEqual(values[3], 0)
        center = (16 * generator.SIZE + 16) * 4
        self.assertEqual(values[center + 3], 255)


if __name__ == "__main__":
    unittest.main()
