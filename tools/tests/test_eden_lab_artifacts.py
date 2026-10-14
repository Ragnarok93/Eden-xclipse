# SPDX-License-Identifier: GPL-3.0-or-later
"""APK attestation unit tests; mocked build-tools, no Android device needed."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import eden_lab_artifacts as a

CERT = "a" * 64


def apks_badging(package: str, test: bool = False, version: int = 123) -> str:
    result = f"package: name='{package}' versionCode='{version}' versionName='0.1'\n"
    if test:
        result += ("instrumentation: name='androidx.test.runner.AndroidJUnitRunner' "
                   "label='' targetPackage='dev.eden.eden_emulator.debug'\n")
    return result


class AttestationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.app = Path(self.temp.name, "app.apk")
        self.test = Path(self.temp.name, "test.apk")
        self.app.write_bytes(b"application")
        self.test.write_bytes(b"instrumentation")

    def tearDown(self):
        self.temp.cleanup()

    def exercise(self, app_pkg=a.APP_PACKAGE, test_pkg=a.TEST_PACKAGE,
                 test_version=123, test_cert=CERT, has_runner=True):
        def tool(program, *args):
            filename = args[-1]
            if program == "aapt2":
                if filename == str(self.app):
                    return apks_badging(app_pkg)
                return apks_badging(test_pkg, test=has_runner, version=test_version)
            return ("Verified using v2 scheme (APK Signature Scheme v2): true\n"
                    "Signer #1 certificate SHA-256 digest: " +
                    (CERT if filename == str(self.app) else test_cert) + "\n")
        with mock.patch.object(a, "run_tool", side_effect=tool):
            return a.attest(self.app, self.test, aapt="aapt2", apksigner="apksigner",
                            expected_cert=CERT, expected_version_code=123,
                            source_commit="b" * 40)

    def test_valid_pair_has_no_device_mutation(self):
        result = self.exercise()
        self.assertTrue(result["ready_for_install_review"])
        self.assertFalse(result["installed_or_modified_device"])
        self.assertFalse(result["source_commit_verified_in_binary"])
        self.assertNotEqual(result["application"]["sha256"],
                            result["instrumentation"]["sha256"])

    def test_unexpected_packages_or_runner(self):
        for change in ({"app_pkg": "other"}, {"test_pkg": "other.test"},
                       {"has_runner": False}):
            with self.subTest(change=change), self.assertRaises(a.ArtifactError):
                self.exercise(**change)

    def test_signing_or_version_mismatch(self):
        for change in ({"test_cert": "c" * 64}, {"test_version": 124}):
            with self.subTest(change=change), self.assertRaises(a.ArtifactError):
                self.exercise(**change)

    def test_source_provenance_is_never_claimed_from_apk_name(self):
        with self.assertRaises(a.ArtifactError):
            with mock.patch.object(a, "run_tool"):
                a.attest(self.app, self.test, aapt="aapt2", apksigner="apksigner",
                         source_commit="not_a_commit")

    def test_apk_not_present_rejected(self):
        self.app.unlink()
        with self.assertRaises(a.ArtifactError):
            self.exercise()


if __name__ == "__main__":
    unittest.main()
