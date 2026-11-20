import json
import tempfile
import unittest
from pathlib import Path

import prepare_mm3d_data as tool


class PrepareMm3dDataTest(unittest.TestCase):
    def installation(self, root: Path) -> None:
        (root / "game").mkdir()
        for name in ("code.bin", "exheader.bin", "romfs.bin"):
            (root / "game" / name).write_bytes(b"x")
        (root / "resources").mkdir()
        (root / "resources/a.txt").write_text("a")
        (root / "process-manifest.json").write_text(json.dumps({"source": {
            "code_bin_path": "game/code.bin", "exheader_path": "game/exheader.bin",
            "romfs_image_path": "game/romfs.bin"}}))

    def test_stages_inputs_and_profile(self):
        with tempfile.TemporaryDirectory() as tmp:
            install, out = Path(tmp) / "install", Path(tmp) / "out"
            install.mkdir()
            self.installation(install)
            tool.prepare(install, out, None)
            profile = json.loads((out / "TriAevum.android.launch.json").read_text())
            args = dict(zip(profile["arguments"][::2], profile["arguments"][1::2]))
            self.assertEqual(args["--ui-profile"], "oot3d")
            self.assertNotIn("--title-plugin", args)
            self.assertNotIn("--max-seconds", args)
            self.assertTrue((out / "game/romfs.bin").is_file())
            self.assertTrue((out / "resources/a.txt").is_file())

    def test_rejects_manifest_path_escape(self):
        with tempfile.TemporaryDirectory() as tmp:
            install, out = Path(tmp) / "install", Path(tmp) / "out"
            install.mkdir()
            self.installation(install)
            manifest = json.loads((install / "process-manifest.json").read_text())
            manifest["source"]["code_bin_path"] = "../outside.bin"
            (install / "process-manifest.json").write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                tool.prepare(install, out, None)


if __name__ == "__main__":
    unittest.main()
