import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import meson_to_cyclonedx as sbom

SERIAL = "urn:uuid:11111111-2222-4333-8444-555555555555"
TIMESTAMP = "2026-10-28T07:00:00Z"


def _projectinfo() -> dict:
    return {
        "descriptive_name": "kitsas",
        "version": "6.0.0",
        "license": ["GPL-3.0-or-later"],
    }


class CycloneDxFromMesonTest(unittest.TestCase):
    def test_lists_named_dependencies_and_skips_anonymous_ones(self):
        bom = sbom.build_bom(
            _projectinfo(),
            [
                {"name": "qt6", "version": "6.7.3", "link_args": ["-lQt6Core"]},
                {"name": "libzip", "version": "1.7.3"},
                {"name": "libzip", "version": "1.7.3"},
                {"name": "", "version": "", "link_args": ["-lkitsas"]},
                {"name": "threads", "version": ""},
            ],
            timestamp=TIMESTAMP,
            serial_number=SERIAL,
        )

        self.assertEqual(bom["bomFormat"], "CycloneDX")
        self.assertEqual(bom["specVersion"], "1.5")
        self.assertEqual(bom["serialNumber"], SERIAL)
        root = bom["metadata"]["component"]
        self.assertEqual(root["name"], "kitsas")
        self.assertEqual(root["version"], "6.0.0")
        self.assertEqual(root["purl"], "pkg:generic/kitsas@6.0.0")
        self.assertEqual(root["licenses"], [{"license": {"id": "GPL-3.0-or-later"}}])

        self.assertEqual(
            [(item["name"], item.get("version")) for item in bom["components"]],
            [("libzip", "1.7.3"), ("qt6", "6.7.3"), ("threads", None)],
        )
        self.assertEqual(
            bom["dependencies"],
            [
                {
                    "ref": "pkg:generic/kitsas@6.0.0",
                    "dependsOn": [
                        "pkg:generic/libzip@1.7.3",
                        "pkg:generic/qt6@6.7.3",
                        "pkg:generic/threads",
                    ],
                }
            ],
        )

    def test_qt_modules_come_from_link_args(self):
        bom = sbom.build_bom(
            _projectinfo(),
            [
                {
                    "name": "qt6",
                    "version": "6.7.3",
                    "link_args": [
                        "-L/opt/Qt/6.7.3/gcc_64/lib",
                        "-lQt6Widgets",
                        "/opt/Qt/6.7.3/gcc_64/lib/libQt6Gui.so",
                        "-lQt6Core",
                        "-lQt6Core",
                    ],
                }
            ],
            timestamp=TIMESTAMP,
            serial_number=SERIAL,
        )
        self.assertEqual(
            bom["components"][0]["properties"],
            [{"name": "qt:modules", "value": "Core,Gui,Widgets"}],
        )

    def test_encodes_names_that_are_not_purl_tokens(self):
        self.assertEqual(sbom.purl_for("Qt6::Core", "6.7.3"), "pkg:generic/Qt6%3A%3ACore@6.7.3")

    def test_freeform_license_is_not_emitted_as_an_spdx_id(self):
        bom = sbom.build_bom(
            {"descriptive_name": "kitsas", "version": "6.0.0", "license": "GNU GPL"},
            [],
            timestamp=TIMESTAMP,
            serial_number=SERIAL,
        )
        self.assertEqual(
            bom["metadata"]["component"]["licenses"],
            [{"license": {"name": "GNU GPL"}}],
        )

    def test_summary_lists_components(self):
        bom = sbom.build_bom(
            _projectinfo(),
            [{"name": "libzip", "version": "1.7.3"}],
            timestamp=TIMESTAMP,
            serial_number=SERIAL,
        )
        text = sbom.format_summary(bom)
        self.assertIn("kitsas 6.0.0", text)
        self.assertIn("| `libzip` | 1.7.3 |", text)

    def test_main_writes_the_document(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            info = root / "meson-info"
            info.mkdir()
            (info / "intro-projectinfo.json").write_text(
                json.dumps(_projectinfo()),
                encoding="utf-8",
            )
            (info / "intro-dependencies.json").write_text(
                json.dumps([{"name": "libzip", "version": "1.7.3"}]),
                encoding="utf-8",
            )
            output = root / "sbom.cdx.json"
            stdout, stderr = StringIO(), StringIO()
            with redirect_stdout(stdout), redirect_stderr(stderr):
                status = sbom.main([str(root), "-o", str(output)])
            self.assertEqual(status, 0)
            written = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(written["components"][0]["name"], "libzip")
            self.assertIn("libzip", stdout.getvalue())
            self.assertIn("1 components", stderr.getvalue())
            self.assertFalse((root / "sbom.cdx.json.tmp").exists())

    def test_main_reports_a_missing_build_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            stderr = StringIO()
            with redirect_stderr(stderr):
                status = sbom.main([str(Path(tmp) / "missing"), "-o", str(Path(tmp) / "out.json")])
            self.assertEqual(status, 1)
            self.assertIn("meson setup", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
