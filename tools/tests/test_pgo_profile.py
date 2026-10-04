# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("pgo_profile", Path(__file__).parents[1] / "pgo_profile.py")
pgo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pgo)


class ProfileValidation(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.archive = Path(self.directory.name) / "training.zip"
        self.raw = [f"profile-{i}".encode() for i in range(4)]
        self.manifest = {
            "complete": True, "abi": "arm64-v8a",
            "build": {"instrumented": True, "suite_version": 1,
                      "build_id": "source", "compiler": "19.0.0"},
            "stages": [{"stage": i, "success": True, "file": f"stage-{i}.profraw",
                        "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
                       for i, raw in enumerate(self.raw)],
        }

    def write(self, manifest=None, extra=None, corrupt=False):
        with zipfile.ZipFile(self.archive, "w") as package:
            package.writestr("manifest.json", json.dumps(manifest or self.manifest))
            for i, raw in enumerate(self.raw):
                package.writestr(f"stage-{i}.profraw", b"corrupted" if corrupt and i == 0 else raw)
            if extra:
                package.writestr(extra, b"unexpected")

    def validate(self, build_id="source", compiler="19.0.0"):
        return pgo.validate_archive(self.archive, build_id, compiler)

    def test_complete_export_preserves_all_raw_profiles(self):
        self.write()
        self.assertEqual([raw for _, raw in self.validate()], self.raw)

    def test_incomplete_and_failed_sessions_are_rejected(self):
        for mutation in ("incomplete", "failed", "missing", "reordered"):
            manifest = copy.deepcopy(self.manifest)
            if mutation == "incomplete":
                manifest["complete"] = False
            elif mutation == "failed":
                manifest["stages"][1]["success"] = False
            elif mutation == "missing":
                manifest["stages"].pop()
            else:
                manifest["stages"].reverse()
            with self.subTest(mutation=mutation):
                self.write(manifest)
                with self.assertRaises(ValueError):
                    self.validate()

    def test_stale_source_or_compiler_cannot_be_used(self):
        self.write()
        for identity, compiler in (("different", "19.0.0"), ("source", "18.0.0")):
            with self.assertRaises(ValueError):
                self.validate(identity, compiler)

    def test_corrupted_profile_is_rejected(self):
        self.write(corrupt=True)
        with self.assertRaises(ValueError):
            self.validate()

    def test_archive_traversal_and_unexpected_files_are_rejected(self):
        for name in ("../evil", "nested/data", "extra.profraw"):
            with self.subTest(name=name):
                self.write(extra=name)
                with self.assertRaises(ValueError):
                    self.validate()

    def test_duplicate_zip_entries_are_rejected(self):
        self.write()
        with zipfile.ZipFile(self.archive, "a") as package:
            package.writestr("stage-0.profraw", self.raw[0])
        with self.assertRaises(ValueError):
            self.validate()

    def test_wrong_abi_or_suite_is_rejected(self):
        for key, value in (("abi", "x86_64"), ("suite_version", 2), ("instrumented", False)):
            manifest = copy.deepcopy(self.manifest)
            if key == "abi":
                manifest[key] = value
            else:
                manifest["build"][key] = value
            self.write(manifest)
            with self.assertRaises(ValueError):
                self.validate()


if __name__ == "__main__":
    unittest.main()
