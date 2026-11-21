import importlib.util, struct, unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DISK = ROOT / "build" / "virtio-test.img"


def _generator():
    path = ROOT / "scripts" / "make_test_disk.py"
    spec = importlib.util.spec_from_file_location("make_test_disk", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class VirtioDiskTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gen = _generator()
        if not DISK.is_file():
            cls.gen.build_image(DISK)
        cls.data = DISK.read_bytes()

    def test_disk_size_and_mbr(self):
        self.assertEqual(len(self.data), self.gen.TOTAL_SECTORS * self.gen.SECTOR)
        self.assertEqual(self.data[510:512], b"\x55\xAA")
        e = self.data[446:462]
        self.assertEqual(e[4], 0x06)  # FAT16 partition
        self.assertEqual(struct.unpack_from("<I", e, 8)[0], self.gen.PART_START)
        self.assertEqual(struct.unpack_from("<I", e, 12)[0], self.gen.PART_SECTORS)

    def test_fat16_bpb(self):
        b = self.data[self.gen.PART_START * self.gen.SECTOR:]
        self.assertEqual(b[510:512], b"\x55\xAA")
        self.assertEqual(struct.unpack_from("<H", b, 11)[0], 512)
        self.assertEqual(b[13], self.gen.SECTORS_PER_CLUSTER)
        self.assertEqual(struct.unpack_from("<H", b, 14)[0], self.gen.RESERVED)
        self.assertEqual(b[16], self.gen.FAT_COUNT)
        self.assertEqual(struct.unpack_from("<H", b, 17)[0], self.gen.ROOT_ENTRIES)
        self.assertEqual(struct.unpack_from("<H", b, 19)[0], self.gen.PART_SECTORS)
        self.assertEqual(struct.unpack_from("<H", b, 22)[0], self.gen.FAT_SECTORS)
        self.assertEqual(b[54:62], b"FAT16   ")
        root_sectors = (self.gen.ROOT_ENTRIES * 32 + 511) // 512
        meta = self.gen.RESERVED + self.gen.FAT_COUNT * self.gen.FAT_SECTORS + root_sectors
        clusters = (self.gen.PART_SECTORS - meta) // self.gen.SECTORS_PER_CLUSTER
        self.assertTrue(4085 <= clusters < 65525, "must be a real FAT16 cluster count")

    def test_fat_chain_root_entry_and_file_payload(self):
        fat_lba = self.gen.PART_START + self.gen.RESERVED
        fat = self.data[fat_lba * self.gen.SECTOR:]
        self.assertEqual(struct.unpack_from("<HHH", fat, 0), (0xFFF8, 0xFFFF, 0xFFFF))
        root_lba = fat_lba + self.gen.FAT_COUNT * self.gen.FAT_SECTORS
        entry = self.data[root_lba * self.gen.SECTOR:root_lba * self.gen.SECTOR + 32]
        self.assertEqual(entry[:11], b"VQEAF   TXT")
        self.assertEqual(struct.unpack_from("<H", entry, 26)[0], 2)
        self.assertEqual(struct.unpack_from("<I", entry, 28)[0], len(self.gen.FILE_CONTENT))
        root_sectors = (self.gen.ROOT_ENTRIES * 32 + 511) // 512
        data_lba = root_lba + root_sectors
        payload = self.data[data_lba * self.gen.SECTOR:
                            data_lba * self.gen.SECTOR + len(self.gen.FILE_CONTENT)]
        self.assertEqual(payload, self.gen.FILE_CONTENT)


if __name__ == "__main__":
    unittest.main()
