#!/usr/bin/env python3
"""Inspect a pinned application + test APK pair before any Eden lab deployment.

Read only. This does not install, downgrade, uninstall or clear data.
Requires Android SDK build-tools executables apksigner and aapt2 (or aapt).
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

APP_PACKAGE = "dev.eden.eden_emulator.debug"
TEST_PACKAGE = APP_PACKAGE + ".test"
RUNNER = "androidx.test.runner.AndroidJUnitRunner"

PKG_RE = re.compile(r"^package: name='([^']+)' versionCode='([0-9]+)'", re.MULTILINE)
INSTR_RE = re.compile(r"^instrumentation: name='([^']+)'.*?targetPackage='([^']+)'",
                      re.MULTILINE)
CERT_RE = re.compile(r"^Signer #([0-9]+) certificate SHA-256 digest: ([0-9a-fA-F]{64})$",
                     re.MULTILINE)


class ArtifactError(ValueError):
    pass


def check(cond: bool, error: str) -> None:
    if not cond:
        raise ArtifactError(error)


def run_tool(program: str, *args: str) -> str:
    try:
        process = subprocess.run([program, *args], stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, timeout=40, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise ArtifactError(f"Cannot run {program}: {exc}") from exc
    check(process.returncode == 0,
          f"{program} returned {process.returncode}: " +
          process.stderr.decode("utf-8", "replace")[:300])
    return process.stdout.decode("utf-8", "replace")


def file_hash(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def inspect_apk(path: Path, *, aapt: str, apksigner: str) -> dict:
    check(path.is_file() and path.suffix.lower() == ".apk", "Expected a readable APK")
    check(path.stat().st_size > 0, "Empty APK")
    package_dump = run_tool(aapt, "dump", "badging", str(path))
    matches = PKG_RE.findall(package_dump)
    check(len(matches) == 1, "Could not parse unique APK package/version")
    pkg, code = matches[0]
    signer = run_tool(apksigner, "verify", "--verbose", "--print-certs", str(path))
    certs = CERT_RE.findall(signer)
    check(len(certs) == 1 and certs[0][0] == "1",
          "Expected one verified APK signer")
    return {
        "file": str(path.resolve()), "sha256": file_hash(path),
        "package": pkg, "version_code": int(code),
        "signer_sha256": certs[0][1].lower(),
        "instrumentations": [
            {"runner": runner, "target": target}
            for runner, target in INSTR_RE.findall(package_dump)
        ],
    }


def attest(app: Path, test: Path, *, aapt: str, apksigner: str,
           expected_cert: str | None = None,
           expected_version_code: int | None = None,
           source_commit: str | None = None) -> dict:
    check(app.resolve() != test.resolve(), "APK paths must be different")
    if source_commit is not None:
        check(bool(re.fullmatch(r"[0-9a-f]{40}", source_commit)),
              "Expected a full pinned Git commit")
    if expected_cert is not None:
        check(bool(re.fullmatch(r"[a-f0-9]{64}", expected_cert)),
              "Invalid pinned signing certificate SHA256")
    app_data = inspect_apk(app, aapt=aapt, apksigner=apksigner)
    test_data = inspect_apk(test, aapt=aapt, apksigner=apksigner)
    check(app_data["package"] == APP_PACKAGE,
          "Unexpected application package; refusing to mix flavors")
    check(test_data["package"] == TEST_PACKAGE, "Unexpected test APK package")
    check(test_data["instrumentations"] == [{"runner": RUNNER, "target": APP_PACKAGE}],
          "Test APK does not instrument the exact debug application")
    check(app_data["version_code"] == test_data["version_code"],
          "APK pair built with mismatched version codes")
    check(app_data["signer_sha256"] == test_data["signer_sha256"],
          "APK pair has mismatched signing certificates")
    check(not app_data["instrumentations"],
          "Application APK unexpectedly contains an instrumentation runner")
    if expected_cert is not None:
        check(app_data["signer_sha256"] == expected_cert,
              "Signing certificate does not match pinned expected identity")
    if expected_version_code is not None:
        check(type(expected_version_code) is int and expected_version_code > 0,
              "Invalid pinned version code")
        check(app_data["version_code"] == expected_version_code,
              "Version code differs from expected build")
    return {
        "schema_version": 1,
        "verification": "apksigner_and_aapt_local_only",
        "source_commit_claim": source_commit,
        "source_commit_verified_in_binary": False,
        "application": app_data,
        "instrumentation": test_data,
        "ready_for_install_review": True,
        "installed_or_modified_device": False,
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("application", type=Path)
    parser.add_argument("instrumentation", type=Path)
    parser.add_argument("--aapt", default="aapt2")
    parser.add_argument("--apksigner", default="apksigner")
    parser.add_argument("--expected-cert-sha256")
    parser.add_argument("--expected-version-code", type=int)
    parser.add_argument("--source-commit")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        output = attest(args.application, args.instrumentation,
                        aapt=args.aapt, apksigner=args.apksigner,
                        expected_cert=args.expected_cert_sha256,
                        expected_version_code=args.expected_version_code,
                        source_commit=args.source_commit)
        rendered = json.dumps(output, indent=2, sort_keys=True)
        if args.output is not None:
            args.output.write_text(rendered + "\n")
        print(rendered)
        return 0
    except (ArtifactError, OSError) as exc:
        print(json.dumps({"status": "rejected", "error": str(exc)}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
