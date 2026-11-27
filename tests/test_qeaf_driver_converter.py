import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONVERTER = ROOT / 'tools' / 'qeaf_driver_converter.py'

class DriverConverterTests(unittest.TestCase):
    def test_android_bound_driver_is_flagged_and_source_is_preserved(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            src = base / 'android_wifi'
            src.mkdir()
            original = '#include <linux/platform_device.h>\n#include <linux/pm_wakeup.h>\nvoid f(void) { __pm_stay_awake(0); }\n'
            (src / 'driver.c').write_text(original, encoding='utf-8')
            out = base / 'out'
            subprocess.run([sys.executable, str(CONVERTER), str(src), '--name', 'test-wifi', '--out', str(out)], check=True, capture_output=True, text=True)
            root = out / 'test-wifi'
            plan = json.loads((root / 'port-plan.json').read_text(encoding='utf-8'))
            self.assertEqual(plan['overall_status'], 'REQUIRES_PORT')
            self.assertFalse(plan['source_modified'])
            self.assertEqual((root / 'reference_source' / 'driver.c').read_text(encoding='utf-8'), original)
            self.assertIn('return -2', (root / 'qeaf_driver_skeleton.c').read_text(encoding='utf-8'))

    def test_binder_dependency_blocks_automatic_port(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            src = base / 'hal.c'
            src.write_text('class Binder {};\n', encoding='utf-8')
            out = base / 'out'
            subprocess.run([sys.executable, str(CONVERTER), str(src), '--name', 'binder-hal', '--out', str(out)], check=True, capture_output=True, text=True)
            plan = json.loads((out / 'binder-hal' / 'port-plan.json').read_text(encoding='utf-8'))
            self.assertEqual(plan['overall_status'], 'BLOCKED')
            self.assertIn('binder_hidl', plan['category_status'])

if __name__ == '__main__':
    unittest.main()
