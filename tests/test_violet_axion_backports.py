from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "check_violet_axion_backports.py"
PATCH_DIR = ROOT / "platform" / "violet" / "kernel-patches" / "axion"

SPEC = importlib.util.spec_from_file_location("violet_axion_backports", SCRIPT)
assert SPEC and SPEC.loader
BACKPORTS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BACKPORTS)

FIXTURES = {
    "drivers/soc/qcom/msm_bus/msm_bus_fabric_rpmh.c": (
        "static int msm_bus_dev_init_qos(struct device *dev, void *data)\n"
        "{\n"
        "\t\tgoto exit_init_qos;\n"
        "\t}\n"
        "\n"
        "\tif (node_dev->ap_owned) {\n"
        "\t\tstruct msm_bus_node_device_type *bus_node_info;\n"
        "\n"
        "\t\tbus_node_info =\n"
    ),
    "drivers/gpu/drm/msm/msm_drv.c": (
        "\tkfree(priv);\n"
        "priv_alloc_fail:\n"
        "\tdrm_dev_unref(ddev);\n"
        "\treturn ret;\n"
        "}\n"
        "\n"
    ),
    "drivers/gpu/drm/msm/dsi-staging/dsi_drm.c": (
        "\t\treturn;\n"
        "\t}\n"
        "\n"
        "\tatomic_set(&c_bridge->display->panel->esd_recovery_pending, 0);\n"
        "\n"
        "\tif (c_bridge->display->is_prim_display && atomic_read(&prim_panel_is_on)) {\n"
        "\t\tcancel_delayed_work_sync(&prim_panel_work);\n"
    ),
}


def run_script(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )


def make_source(root: Path, mutate: bool = False) -> None:
    subprocess.run(["git", "init", "-q", str(root)], check=True)
    for relative, contents in FIXTURES.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents + ("\n/* local source delta */\n" if mutate else ""), encoding="utf-8")


class VioletAxionBackportsTest(unittest.TestCase):
    def test_manifest_and_patch_hashes(self) -> None:
        result = run_script()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("manifest: PASS (3 patches)", result.stdout)

    def test_tampered_patch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shutil.copytree(PATCH_DIR, root / "patches")
            patch = root / "patches" / "0001-msm-bus-skip-qos-clocks-when-none.patch"
            patch.write_text(patch.read_text(encoding="ascii") + "\n", encoding="ascii")
            old_dir, old_manifest = BACKPORTS.PATCH_DIR, BACKPORTS.MANIFEST
            try:
                BACKPORTS.PATCH_DIR = root / "patches"
                BACKPORTS.MANIFEST = root / "patches" / "provenance.json"
                with self.assertRaisesRegex(SystemExit, "hash mismatch"):
                    BACKPORTS.validate_manifest()
            finally:
                BACKPORTS.PATCH_DIR, BACKPORTS.MANIFEST = old_dir, old_manifest

    def test_unverified_source_requires_explicit_override(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "kernel"
            make_source(source, mutate=True)
            result = run_script("--source", str(source))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unverified source", result.stdout)
            result = run_script("--source", str(source), "--allow-unverified-source")
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertEqual(result.stdout.count("apply-check: PASS"), 3)

    def test_apply_verifies_every_postcondition(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "kernel"
            make_source(source)
            result = run_script("--source", str(source), "--allow-unverified-source", "--apply")
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertEqual(result.stdout.count("apply-check: PASS"), 3)
            self.assertEqual(result.stdout.count("applied:"), 3)
            manifest = json.loads((PATCH_DIR / "provenance.json").read_text(encoding="utf-8"))
            for entry in manifest["patches"]:
                self.assertIn(entry["postcondition"], (source / entry["path"]).read_text(encoding="utf-8"))

    def test_apply_requires_source(self) -> None:
        result = run_script("--apply")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--apply requires --source", result.stdout)


if __name__ == "__main__":
    unittest.main()
