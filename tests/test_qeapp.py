import importlib.util, io, json, os, shutil, subprocess, sys, tempfile, unittest, zipfile
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("qeaf",ROOT/"sdk"/"cli"/"qeaf.py")
qeaf = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(qeaf)
EXAMPLE = ROOT / "examples" / "hello-qeapp"
COMPILER_SPEC = importlib.util.spec_from_file_location(
    "qecompile", ROOT / "sdk" / "compiler" / "qecompile.py")
compiler = importlib.util.module_from_spec(COMPILER_SPEC)
COMPILER_SPEC.loader.exec_module(compiler)


def copy_example(parent):
    project = Path(parent) / "hello-qeapp"
    shutil.copytree(EXAMPLE, project,
                    ignore=shutil.ignore_patterns("build", "__pycache__"))
    return project

def run_cli(argv):
    import sys
    old = sys.argv
    sys.argv = ["qeaf"] + argv
    try:
        return qeaf.main()
    finally:
        sys.argv = old

class QEAPPTests(unittest.TestCase):
    def assert_cli_error(self, args, message):
        output = io.StringIO()
        with redirect_stdout(output), redirect_stderr(output):
            status = run_cli(args)
        self.assertEqual(status, 1, output.getvalue())
        self.assertIn(message, output.getvalue())
        return output.getvalue()

    def assert_stored_signed_package(self, package):
        self.assertTrue(package.is_file(), str(package))
        self.assertEqual(qeaf.validate_package(package), [])
        with zipfile.ZipFile(package) as archive:
            self.assertIsNone(archive.testzip())
            names = archive.namelist()
            self.assertEqual(len(names), len(set(names)))
            self.assertIn("src/main.qe", names)
            self.assertNotIn("main.qe", names)
            self.assertFalse(any(name.startswith(("build/", "compiler/", "packaging/", "tests/"))
                                 or name in ("build.py", "README.md") for name in names))
            self.assertTrue(all(item.compress_type == zipfile.ZIP_STORED
                                for item in archive.infolist()))
            self.assertEqual(archive.read("SIGNATURE").decode("ascii"),
                             qeaf.package_signature([(name, archive.read(name)) for name in names]))

    def test_example_manifest_valid(self):
        m=json.loads((ROOT/"runtime"/"qeapp"/"examples"/"hello"/"manifest.json").read_text())
        self.assertEqual(qeaf.validate_manifest(m),[])
    def test_reject_foreign_apk_format(self):
        self.assertIn("format must be qeapp",qeaf.validate_manifest({"format":"apk","format_version":1}))
    def test_reject_invalid_id(self):
        m={"format":"qeapp","format_version":1,"id":"Hello","name":"x","version":"1.0.0","min_os":"0.1","entry":"main.qe","architecture":["x86_64"],"permissions":[],"dependencies":[]}
        self.assertTrue(any("id must" in e for e in qeaf.validate_manifest(m)))
    def test_package_builder_and_validator(self):
        with tempfile.TemporaryDirectory() as td:
            project=Path(td)/"Sample"; project.mkdir()
            manifest={"format":"qeapp","format_version":1,"id":"org.test.sample","name":"Sample","version":"1.0.0","min_os":"0.1.0","entry":"main.qe","architecture":["x86_64"],"permissions":[],"dependencies":[]}
            (project/"manifest.json").write_text(json.dumps(manifest))
            (project/"main.qe").write_text('app Sample\nprint "ok"\n')
            output=project.with_suffix(".qeapp")
            with zipfile.ZipFile(output,"w",zipfile.ZIP_DEFLATED) as z:
                z.write(project/"manifest.json","manifest.json")
                z.write(project/"main.qe","main.qe")
            self.assertEqual(qeaf.validate_package(output),[])
    def test_reject_zip_path_traversal(self):
        with tempfile.TemporaryDirectory() as td:
            package=Path(td)/"bad.qeapp"
            manifest={"format":"qeapp","format_version":1,"id":"org.test.bad","name":"Bad","version":"1.0.0","min_os":"0.1.0","entry":"main.qe","architecture":["x86_64"],"permissions":[],"dependencies":[]}
            with zipfile.ZipFile(package,"w") as z:
                z.writestr("manifest.json",json.dumps(manifest))
                z.writestr("../escape.txt","bad")
                z.writestr("main.qe",'print "x"')
            self.assertTrue(any("unsafe package path" in e for e in qeaf.validate_package(package)))
    def test_template_manifest_valid(self):
        m=json.loads((ROOT/"sdk"/"templates"/"hello-qeapp"/"manifest.json").read_text())
        self.assertEqual(qeaf.validate_manifest(m),[])
    def test_compile_qe_accepts_declarative(self):
        self.assertEqual(qeaf.compile_qe('app Demo\n# comment\nprint "hi"\nprint "a\\"b"\n'),[])
    def test_compile_qe_rejects_bad_lines(self):
        self.assertTrue(any("line 1" in e for e in qeaf.compile_qe('frobnicate x\n')))
        self.assertTrue(any("unterminated" in e for e in qeaf.compile_qe('print "oops\n')))
        self.assertTrue(any("identifier" in e for e in qeaf.compile_qe('app 123bad\n')))
    def test_create_copies_template(self):
        with tempfile.TemporaryDirectory() as td:
            cwd = os.getcwd()
            os.chdir(td)
            try:
                self.assertEqual(run_cli(["create", "My App"]), 0)
            finally:
                os.chdir(cwd)
            proj = Path(td) / "My App"
            for name in ("manifest.json", "src/main.qe", "assets/icon.png",
                         "assets/icon.rgba", "libs/greeting.qe", "compiler/qecompile.py",
                         "build.py", "packaging/package.json", "tests/test_project.py", "README.md"):
                with self.subTest(file=name):
                    self.assertTrue((proj / name).is_file(), name)
            for folder in ("releases", "debug"):
                self.assertEqual(list((proj / "build" / folder).iterdir()), [])
            self.assertFalse((proj / "icon.png").exists())
            self.assertFalse((proj / "main.qe").exists())
            self.assertEqual((proj / "assets" / "icon.rgba").stat().st_size, 64 * 64 * 4)
            manifest = json.loads((proj / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["id"], "org.example.my-app")
            self.assertEqual(manifest["name"], "My App")
            self.assertEqual(manifest["entry"], "src/main.qe")
            self.assertEqual(manifest["icon"], "assets/icon.rgba")
            self.assertEqual(qeaf.validate_manifest(manifest), [])
            self.assertEqual(json.loads((proj / "packaging" / "package.json").read_text(encoding="utf-8")),
                             {"format": "qeapp-packaging", "format_version": 1, "name": "my-app"})

    def test_create_explicit_identity_and_package_name_are_independent(self):
        with tempfile.TemporaryDirectory() as td:
            proj = Path(td) / "Display Folder"
            self.assertEqual(run_cli(["create", str(proj), "--id", "org.test.identity",
                                      "--package-name", "artifact_42"]), 0)
            manifest = json.loads((proj / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["id"], "org.test.identity")
            self.assertEqual(manifest["name"], "Display Folder")
            config = json.loads((proj / "packaging" / "package.json").read_text(encoding="utf-8"))
            self.assertEqual(config["name"], "artifact_42")
            source = (proj / "src" / "main.qe").read_text(encoding="utf-8")
            self.assertIn('print "Hello from Display Folder"', source)
            self.assertEqual(qeaf.compile_qe(source), [])
            self.assertEqual(run_cli(["build", str(proj)]), 0)
            package = proj / "build" / "releases" / "artifact_42.qeapp"
            self.assert_stored_signed_package(package)
            with zipfile.ZipFile(package) as archive:
                self.assertEqual(json.loads(archive.read("manifest.json")), manifest)
            self.assertFalse(proj.with_suffix(".qeapp").exists())

    def test_create_absolute_path_defaults_use_basename(self):
        with tempfile.TemporaryDirectory(prefix="qeapp parent with spaces ") as td:
            proj = Path(td).resolve() / "My Second App"
            self.assertTrue(proj.is_absolute())
            self.assertEqual(run_cli(["create", str(proj)]), 0)
            manifest = json.loads((proj / "manifest.json").read_text(encoding="utf-8"))
            config = json.loads((proj / "packaging" / "package.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["name"], "My Second App")
            self.assertEqual(manifest["id"], "org.example.my-second-app")
            self.assertEqual(config["name"], "my-second-app")
            source = (proj / "src" / "main.qe").read_text(encoding="utf-8")
            self.assertIn('print "Hello from My Second App"', source)
            self.assertNotIn(td, source)

    def test_create_leading_digit_name_builds_valid_identifier(self):
        with tempfile.TemporaryDirectory() as td:
            proj = Path(td) / "123 App"
            self.assertEqual(run_cli(["create", str(proj)]), 0)
            source = (proj / "src" / "main.qe").read_text(encoding="utf-8")
            self.assertRegex(source.splitlines()[0], r"^app [A-Za-z_][A-Za-z0-9_]*$")
            self.assertEqual(qeaf.compile_qe(source), [])
            manifest = json.loads((proj / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["name"], "123 App")
            self.assertEqual(manifest["id"], "org.example.123-app")
            self.assertEqual(run_cli(["build", str(proj)]), 0)
            self.assert_stored_signed_package(proj / "build" / "releases" / "123-app.qeapp")

    def test_create_invalid_package_names_leave_no_partial_project(self):
        invalid = ("", "Uppercase", "has space", "../outside", "a/b", "C:\\bad",
                   "app.qeapp", "x" * 65, "-app", "app_", "a--b",
                   "con", "prn", "aux", "nul", "com1", "com9", "lpt1", "lpt9")
        for name in invalid:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                proj = Path(td) / "uncreated parent" / "Valid App"
                self.assert_cli_error(["create", str(proj), "--id", "org.test.valid",
                                       "--package-name=" + name], "Package name")
                self.assertFalse(proj.exists())
                self.assertEqual(list(Path(td).iterdir()), [])

    def test_create_invalid_ids_leave_no_partial_project(self):
        for app_id in ("", "Hello", "single", "org..bad", "org.test.bad_name",
                       "org.test.has space", "../outside", "org.test."):
            with self.subTest(app_id=app_id), tempfile.TemporaryDirectory() as td:
                proj = Path(td) / "uncreated parent" / "Valid App"
                self.assert_cli_error(["create", str(proj), "--id=" + app_id,
                                       "--package-name", "valid"], "id must")
                self.assertFalse(proj.exists())
                self.assertEqual(list(Path(td).iterdir()), [])

    def test_create_invalid_basename_defaults_leave_no_partial_project(self):
        for name in ("!!!", "CON", "x" * 65):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                proj = Path(td) / name
                self.assert_cli_error(["create", str(proj)], "Package name")
                self.assertEqual(list(Path(td).iterdir()), [])

    def test_create_existing_directory_is_untouched(self):
        with tempfile.TemporaryDirectory() as td:
            proj = Path(td) / "Existing App"
            (proj / "build" / "debug").mkdir(parents=True)
            (proj / "assets").mkdir()
            (proj / "manifest.json").write_bytes(b"original manifest\n")
            (proj / "build" / "debug" / "keep.bin").write_bytes(b"\x00\xffunchanged")
            before = {path.relative_to(proj).as_posix(): path.read_bytes() if path.is_file() else None
                      for path in proj.rglob("*")}
            self.assert_cli_error(["create", str(proj), "--id", "org.test.new",
                                   "--package-name", "new-artifact"], "ERROR:")
            after = {path.relative_to(proj).as_posix(): path.read_bytes() if path.is_file() else None
                     for path in proj.rglob("*")}
            self.assertEqual(after, before)

    def test_build_sign_validate_template(self):
        with tempfile.TemporaryDirectory() as td:
            proj = Path(td) / "hello-qeapp"
            shutil.copytree(ROOT / "sdk" / "templates" / "hello-qeapp", proj,
                            ignore=shutil.ignore_patterns("build", "__pycache__"))
            self.assertEqual(run_cli(["build", str(proj), "--stored"]), 0)
            package = proj / "build" / "releases" / "hello-qeapp.qeapp"
            self.assert_stored_signed_package(package)
            self.assertFalse(proj.with_suffix(".qeapp").exists())
            self.assertFalse((proj / "build" / "debug").exists())
            self.assertEqual(run_cli(["sign", str(package)]), 0)
            self.assert_stored_signed_package(package)
            with zipfile.ZipFile(package) as archive:
                self.assertIn("assets/icon.rgba", archive.namelist())
                self.assertIn("assets/icon.png", archive.namelist())
    def test_build_and_package_configurations_produce_signed_stored_artifacts(self):
        for command in ("build", "package"):
            for config in (None, "release", "debug", "all"):
                with self.subTest(command=command, config=config), tempfile.TemporaryDirectory() as td:
                    proj = copy_example(td)
                    config_file = proj / "packaging" / "package.json"
                    packaging = json.loads(config_file.read_text(encoding="utf-8"))
                    packaging["name"] = "custom_artifact-7"
                    config_file.write_text(json.dumps(packaging), encoding="utf-8")
                    args = [command, str(proj)]
                    if config is not None:
                        args.extend(["--config", config])
                    if config == "all":
                        args.append("--stored")
                    self.assertEqual(run_cli(args), 0)
                    folders = {"debug"} if config == "debug" else {"releases"}
                    if config == "all":
                        folders.add("debug")
                    expected = {proj / "build" / folder / "custom_artifact-7.qeapp"
                                for folder in folders}
                    self.assertEqual(set(proj.rglob("*.qeapp")), expected)
                    self.assertFalse(proj.with_suffix(".qeapp").exists())
                    for package in expected:
                        self.assert_stored_signed_package(package)
                        with zipfile.ZipFile(package) as archive:
                            self.assertEqual("debug/source-map.json" in archive.namelist(),
                                             package.parent.name == "debug")

    def test_sdk_build_and_package_never_execute_project_build_or_compiler(self):
        for command in ("build", "package"):
            with self.subTest(command=command), tempfile.TemporaryDirectory() as td:
                proj = copy_example(td)
                markers = []
                for script_name in ("build.py", "compiler/qecompile.py"):
                    marker = proj / (Path(script_name).stem + "-executed")
                    markers.append(marker)
                    (proj / script_name).write_text(
                        "from pathlib import Path\n"
                        f"Path({str(marker)!r}).write_text('executed')\n"
                        "raise RuntimeError('project-local Python must not run')\n",
                        encoding="utf-8")
                self.assertEqual(run_cli([command, str(proj), "--config", "all"]), 0)
                for marker in markers:
                    self.assertFalse(marker.exists(), str(marker))
                for folder in ("releases", "debug"):
                    package = proj / "build" / folder / "hello-qeapp.qeapp"
                    self.assert_stored_signed_package(package)
                    with zipfile.ZipFile(package) as archive:
                        self.assertIn(b"Hello from libs/greeting.qe", archive.read("src/main.qe"))

    def test_packaging_name_accepts_valid_boundaries(self):
        for name in ("a", "0", "123-app", "custom_artifact-7", "a" * 64):
            with self.subTest(name=name):
                self.assertEqual(compiler.validate_packaging(
                    {"format": "qeapp-packaging", "format_version": 1, "name": name}), name)

    def test_compiler_rejects_invalid_packaging_names_without_output(self):
        for name in (None, 1, True, [], {}, "", "UPPER", "a b", "../outside",
                     "a/b", "C:\\bad", "app.qeapp", "x" * 65,
                     "con", "prn", "aux", "nul", "com1", "lpt9"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                proj = copy_example(td)
                config_file = proj / "packaging" / "package.json"
                config_file.write_text(json.dumps({"format": "qeapp-packaging",
                                                   "format_version": 1, "name": name}), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "Package name"):
                    compiler.compile_project(proj, "release", qeaf)
                for command in ("build", "package"):
                    self.assert_cli_error([command, str(proj)], "Package name")
                self.assertFalse((proj / "build").exists())
                self.assertEqual(list(Path(td).glob("*.qeapp")), [])

    def test_compiler_rejects_missing_or_malformed_packaging_json(self):
        for contents in (None, "", "{", '{"format": "qeapp-packaging",}'):
            with self.subTest(contents=contents), tempfile.TemporaryDirectory() as td:
                proj = copy_example(td)
                config_file = proj / "packaging" / "package.json"
                if contents is None:
                    config_file.unlink()
                    message = "Missing or external project file: packaging/package.json"
                    exception = ValueError
                else:
                    config_file.write_text(contents, encoding="utf-8")
                    message = "ERROR:"
                    exception = json.JSONDecodeError
                with self.assertRaises(exception):
                    compiler.compile_project(proj, "release", qeaf)
                for command in ("build", "package"):
                    output = self.assert_cli_error([command, str(proj)], message)
                    self.assertNotIn("Traceback", output)
                self.assertFalse((proj / "build").exists())
                self.assertEqual(list(Path(td).glob("*.qeapp")), [])
                if contents is not None:
                    self.assertEqual(config_file.read_text(encoding="utf-8"), contents)

    def test_compiler_rejects_invalid_packaging_format_version_and_root(self):
        valid = {"format": "qeapp-packaging", "format_version": 1, "name": "valid"}
        configs = [None, [], [valid], "qeapp-packaging", 1, True, {}]
        configs.extend({**valid, "format": value} for value in (None, "qeapp", "other", 1))
        configs.extend({**valid, "format_version": value}
                       for value in (None, 0, 2, "1", True, 1.0))
        configs.extend({key: value for key, value in valid.items() if key != missing}
                       for missing in ("format", "format_version"))
        for config in configs:
            with self.subTest(config=config), tempfile.TemporaryDirectory() as td:
                proj = copy_example(td)
                config_file = proj / "packaging" / "package.json"
                config_file.write_text(json.dumps(config), encoding="utf-8")
                original = config_file.read_bytes()
                with self.assertRaisesRegex(ValueError, "Packaging config"):
                    compiler.validate_packaging(config)
                with self.assertRaisesRegex(ValueError, "Packaging config"):
                    compiler.compile_project(proj, "release", qeaf)
                for command in ("build", "package"):
                    self.assert_cli_error([command, str(proj)], "Packaging config")
                self.assertEqual(config_file.read_bytes(), original)
                self.assertFalse((proj / "build").exists())
                self.assertEqual(list(Path(td).glob("*.qeapp")), [])

    def test_build_sign_validate_structured_example(self):
        with tempfile.TemporaryDirectory() as td:
            proj = copy_example(td)
            source = proj / "src" / "main.qe"
            source.write_text(source.read_text(encoding="utf-8") + "\n# debug source\n",
                              encoding="utf-8")
            for mode, folder in (("release", "releases"), ("debug", "debug")):
                with self.subTest(mode=mode):
                    package = compiler.compile_project(proj, mode, qeaf)
                    self.assertEqual(package, (proj / "build" / folder / "hello-qeapp.qeapp").resolve())
                    self.assertEqual(qeaf.validate_package(package), [])
                    with zipfile.ZipFile(package) as z:
                        self.assertIsNone(z.testzip())
                        expected = {"manifest.json", "src/main.qe", "assets/icon.png",
                                    "assets/icon.rgba", "SIGNATURE"}
                        if mode == "debug":
                            expected.update({"debug/source-map.json",
                                             "debug/sources/src/main.qe",
                                             "debug/sources/libs/greeting.qe"})
                        self.assertEqual(set(z.namelist()), expected)
                        manifest = json.loads(z.read("manifest.json"))
                        self.assertEqual(manifest["permissions"], [])
                        self.assertEqual(len(z.read(manifest["icon"])), 64 * 64 * 4)
                        self.assertTrue(all(item.compress_type == zipfile.ZIP_STORED
                                            for item in z.infolist()))
                        entries = [(name, z.read(name)) for name in z.namelist()]
                        self.assertEqual(z.read("SIGNATURE").decode("ascii"),
                                         qeaf.package_signature(entries))
                        compiled = z.read(manifest["entry"]).decode("utf-8")
                        self.assertIn("Hello from libs/greeting.qe", compiled)
                        if mode == "debug":
                            self.assertIn("# debug source", compiled)
                            mapping = json.loads(z.read("debug/source-map.json"))
                            self.assertEqual(mapping["entry"], manifest["entry"])
                            for item in mapping["lines"]:
                                original = z.read("debug/sources/" + item["source"]).decode("utf-8")
                                self.assertEqual(compiled.splitlines()[item["compiled_line"] - 1],
                                                 original.splitlines()[item["line"] - 1])
                        else:
                            self.assertNotIn("# debug source", compiled)
                    original = package.read_bytes()
                    compiler.compile_project(proj, mode, qeaf)
                    self.assertEqual(package.read_bytes(), original)
    def test_example_compiler_error_preserves_last_build(self):
        with tempfile.TemporaryDirectory() as td:
            proj = copy_example(td)
            package = compiler.compile_project(proj, "release", qeaf)
            previous = package.read_bytes()
            (proj / "libs" / "greeting.qe").write_text('print "unterminated\n')
            with self.assertRaisesRegex(ValueError, "libs/greeting.qe: line 1: unterminated"):
                compiler.compile_project(proj, "release", qeaf)
            self.assertEqual(package.read_bytes(), previous)
    def test_example_compiler_rejects_oversized_print_output(self):
        with tempfile.TemporaryDirectory() as td:
            proj = copy_example(td)
            (proj / "src" / "main.qe").write_text('print "' + "A" * 510 + '"\n')
            with self.assertRaisesRegex(ValueError, "libs/greeting.qe: line 1: combined print output"):
                compiler.compile_project(proj, "release", qeaf)
            (proj / "libs" / "greeting.qe").write_text("")
            self.assertTrue(compiler.compile_project(proj, "release", qeaf).is_file())
            (proj / "src" / "main.qe").write_text('print "' + "é" * 256 + '"\n', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "combined print output"):
                compiler.compile_project(proj, "debug", qeaf)
    def test_example_compiler_rejects_long_entry_path(self):
        with tempfile.TemporaryDirectory() as td:
            proj = copy_example(td)
            manifest_file = proj / "manifest.json"
            manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
            manifest["entry"] = "src/" + "a" * 121 + ".qe"
            manifest_file.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "shorter than 128 bytes"):
                compiler.compile_project(proj, "release", qeaf)
    def test_example_compiler_rejects_alternate_icon_path(self):
        with tempfile.TemporaryDirectory() as td:
            proj = copy_example(td)
            manifest_file = proj / "manifest.json"
            manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
            manifest["icon"] = "assets/other.rgba"
            shutil.copyfile(proj / "assets" / "icon.rgba", proj / manifest["icon"])
            manifest_file.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Runtime icon must be assets/icon.rgba"):
                compiler.compile_project(proj, "release", qeaf)
    def test_example_compiler_rejects_traversal(self):
        with tempfile.TemporaryDirectory() as td:
            proj = copy_example(td)
            manifest_file = proj / "manifest.json"
            manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
            manifest["entry"] = "src/../../outside.qe"
            manifest_file.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Unsafe project path"):
                compiler.compile_project(proj, "release", qeaf)
            self.assertFalse((proj / "build").exists())
    def test_example_compiler_rejects_invalid_icon(self):
        with tempfile.TemporaryDirectory() as td:
            proj = copy_example(td)
            (proj / "assets" / "icon.rgba").write_bytes(b"bad")
            with self.assertRaisesRegex(ValueError, "Runtime icon"):
                compiler.compile_project(proj, "debug", qeaf)
    def test_example_compiler_enforces_package_limits(self):
        with tempfile.TemporaryDirectory() as td:
            proj = copy_example(td)
            extra = proj / "assets" / "large.bin"
            extra.write_bytes(b"x" * (512 * 1024))
            with self.assertRaisesRegex(ValueError, "512 KiB"):
                compiler.compile_project(proj, "release", qeaf)
            self.assertFalse(list((proj / "build" / "releases").glob("*.qeapp")))
        with tempfile.TemporaryDirectory() as td:
            proj = copy_example(td)
            for i in range(32):
                (proj / "assets" / f"extra-{i}.txt").write_text("x")
            with self.assertRaisesRegex(ValueError, "32 package entries"):
                compiler.compile_project(proj, "release", qeaf)
    def test_example_build_cli_from_another_directory(self):
        with tempfile.TemporaryDirectory(prefix="qeapp space ") as td:
            proj = copy_example(td)
            result = subprocess.run(
                [sys.executable, str(proj / "build.py"), "--sdk",
                 str(ROOT / "sdk" / "cli" / "qeaf.py"), "--config", "all"],
                cwd=td, capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue((proj / "build" / "releases" / "hello-qeapp.qeapp").is_file())
            self.assertTrue((proj / "build" / "debug" / "hello-qeapp.qeapp").is_file())
    def test_generated_build_defaults_to_all_with_external_sdk_and_unrelated_cwd(self):
        for sdk_source in ("argument", "environment"):
            with self.subTest(sdk_source=sdk_source), tempfile.TemporaryDirectory(prefix="qeapp external ") as td:
                proj = Path(td) / "Generated App"
                unrelated = Path(td) / "unrelated cwd"
                unrelated.mkdir()
                self.assertEqual(run_cli(["create", str(proj)]), 0)
                env = os.environ.copy()
                env.pop("VQEAF_SDK", None)
                args = [sys.executable, str(proj / "build.py")]
                sdk = str(ROOT / "sdk" / "cli" / "qeaf.py")
                if sdk_source == "argument":
                    args.extend(["--sdk", sdk])
                else:
                    env["VQEAF_SDK"] = sdk
                result = subprocess.run(args, cwd=unrelated, env=env, capture_output=True,
                                        text=True, encoding="utf-8", timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                for folder in ("releases", "debug"):
                    self.assert_stored_signed_package(proj / "build" / folder / "generated-app.qeapp")
                self.assertEqual(list(unrelated.iterdir()), [])

    def test_generated_project_runs_its_five_tests_with_external_sdk(self):
        with tempfile.TemporaryDirectory(prefix="qeapp external tests ") as td:
            proj = Path(td) / "Project Test App"
            unrelated = Path(td) / "unrelated cwd"
            unrelated.mkdir()
            self.assertEqual(run_cli(["create", str(proj), "--id", "org.test.generated",
                                      "--package-name", "generated_tests"]), 0)
            env = os.environ.copy()
            env["VQEAF_SDK"] = str(ROOT / "sdk" / "cli" / "qeaf.py")
            result = subprocess.run(
                [sys.executable, "-m", "unittest", "discover", "-s", str(proj / "tests"),
                 "-p", "test_project.py", "-v"], cwd=unrelated, env=env,
                capture_output=True, text=True, encoding="utf-8", timeout=60)
            output = result.stdout + result.stderr
            self.assertEqual(result.returncode, 0, output)
            self.assertRegex(output, r"\bRan 5 tests\b")
            self.assertRegex(output, r"(?m)^OK$")
            self.assertEqual(list(proj.rglob("*.qeapp")), [])
            self.assertEqual(list(unrelated.iterdir()), [])

    def test_build_rejects_bad_script(self):
        with tempfile.TemporaryDirectory() as td:
            proj = copy_example(td)
            (proj / "src" / "main.qe").write_text('print "unterminated\n', encoding="utf-8")
            output = self.assert_cli_error(["build", str(proj), "--config", "release", "--stored"],
                                          "src/main.qe: line 1: unterminated")
            self.assertNotIn("packaging/package.json", output)
            self.assertFalse((proj / "build").exists())
if __name__ == "__main__":
    unittest.main()
