import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "platform/violet/boot_journal.c").read_text(encoding="utf-8")
APPSTORE = (ROOT / "platform/violet/appstore.c").read_text(encoding="utf-8")


class VioletRamTraceTests(unittest.TestCase):
    def test_live_trace_flush_targets_ram_file_not_partition(self):
        start = SOURCE.index("static void trace_flush(void)")
        end = SOURCE.index("static void *trace_flusher(void *arg)", start)
        block = SOURCE[start:end]
        self.assertIn('"/tmp/qeaf-wifi-trace.log"', block)
        self.assertIn("O_NOFOLLOW", block)
        self.assertNotIn("journal_open(", block)
        self.assertNotIn("VIO_JOURNAL_OFFSET", block)

    def test_disarm_does_not_commit_journal_to_p77(self):
        start = SOURCE.index("void vio_journal_trace_arm(int on)")
        end = SOURCE.index("void vio_journal_trace_raw", start)
        block = SOURCE[start:end]
        self.assertNotIn("journal_commit()", block)
        self.assertIn("trace_flush()", block)

    def test_ram_only_diagnostics_redirects_appstore_to_tmpfs(self):
        self.assertIn("#ifdef VQEAF_RAM_ONLY_DIAGNOSTICS", APPSTORE)
        self.assertIn('"/tmp/qeaf-appstore.bin"', APPSTORE)
        self.assertIn('"/dev/mmcblk0p77"', APPSTORE)
        self.assertEqual(APPSTORE.count("open(APPSTORE_PATH"),
                         APPSTORE.count("O_NOFOLLOW"))

    def test_ram_only_appstore_layout_fits_tmpfs_and_creates_backing_file(self):
        start = APPSTORE.index("#ifdef VQEAF_RAM_ONLY_DIAGNOSTICS\n#define APPSTORE_BASE")
        end = APPSTORE.index("#endif", start) + len("#endif")
        layout = APPSTORE[start:end]
        self.assertIn("#define APPSTORE_BASE 0u", layout)
        self.assertIn("#define APPSTORE_END (16u * 1024u * 1024u)", layout)
        self.assertLess(16 * 1024 * 1024, 32 * 1024 * 1024)
        save_start = APPSTORE.index("int appstore_save(")
        save_end = APPSTORE.index("int appstore_volume_has(", save_start)
        save = APPSTORE[save_start:save_end]
        self.assertIn("open_flags |= O_CREAT", save)
        self.assertIn("open(APPSTORE_PATH, open_flags, 0600)", save)

    def test_ram_only_diagnostics_refuses_journal_writes(self):
        start = SOURCE.index("static int journal_open(int writable)")
        end = SOURCE.index("static int journal_write", start)
        block = SOURCE[start:end]
        guard = block.index("#ifdef VQEAF_RAM_ONLY_DIAGNOSTICS")
        self.assertLess(guard, block.index("if(writable)"))
        self.assertIn("errno = EROFS", block)
        self.assertIn("return -1", block)


if __name__ == "__main__":
    unittest.main()
