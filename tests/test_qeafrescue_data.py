import json, sys, tempfile, unittest, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]; sys.path.insert(0,str(ROOT/"scripts"))
import qeafrescue_data as q

class BackupRestoreTests(unittest.TestCase):
 def test_roundtrip_and_dry_run(self):
  with tempfile.TemporaryDirectory() as td:
   root=Path(td); src=root/"source"; src.mkdir(); (src/"a.txt").write_text("QEAF\n")
   (src/"sub").mkdir(); (src/"sub"/"b.bin").write_bytes(bytes(range(256)))
   arc=root/"backup.qrb"; dst=root/"restored"
   self.assertEqual(q.create_backup(src,arc)["file_count"],2)
   self.assertEqual(q.inspect_backup(arc)["file_count"],2)
   self.assertEqual(q.restore_backup(arc,dst)["status"],"DRY_RUN"); self.assertFalse(dst.exists())
   self.assertEqual(q.restore_backup(arc,dst,True)["status"],"RESTORED_VERIFIED")
   self.assertEqual((dst/"sub"/"b.bin").read_bytes(),bytes(range(256)))
 def test_no_overwrite_archive(self):
  with tempfile.TemporaryDirectory() as td:
   r=Path(td); s=r/"s"; s.mkdir(); a=r/"a.qrb"; q.create_backup(s,a)
   with self.assertRaises(q.DataError): q.create_backup(s,a)
 def test_no_overwrite_restore(self):
  with tempfile.TemporaryDirectory() as td:
   r=Path(td); s=r/"s"; s.mkdir(); a=r/"a.qrb"; q.create_backup(s,a); d=r/"d"; d.mkdir()
   with self.assertRaises(q.DataError): q.restore_backup(a,d,True)
 def test_corrupt_archive_detected(self):
  with tempfile.TemporaryDirectory() as td:
   r=Path(td); s=r/"s"; s.mkdir(); (s/"x").write_text("good"); a=r/"a.qrb"; q.create_backup(s,a)
   with zipfile.ZipFile(a,"a") as z: z.writestr("x","bad")
   with self.assertRaises(q.DataError): q.inspect_backup(a)
 def test_traversal_rejected(self):
  for n in ("../evil","/absolute","a/../../b","C:/windows"):
   with self.assertRaises(q.DataError): q.safe_relative(n)
 def test_log_jsonl(self):
  with tempfile.TemporaryDirectory() as td:
   x=q.append_recovery_event(Path(td),"BACKUP_BEGIN","INFO","test")
   rec=json.loads(Path(x["log"]).read_text().splitlines()[0])
   self.assertEqual(rec["event"],"BACKUP_BEGIN"); self.assertTrue(rec["session"])
 def test_symlink_source_rejected(self):
  with tempfile.TemporaryDirectory() as td:
   r=Path(td); real=r/"real"; real.mkdir(); link=r/"link"
   try: link.symlink_to(real,target_is_directory=True)
   except OSError: self.skipTest("symlinks unavailable")
   with self.assertRaises(q.DataError): q.collect_files(link)

if __name__=="__main__": unittest.main(verbosity=2)
