#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate a complete on-device corpus before invoking the NDK's llvm-profdata."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile

NDK_VERSION = "28.2.13676358"
MAX_PROFILE_BYTES = 256 * 1024 * 1024


def source_identity(root):
    paths = subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode().split("\0")
    digest = hashlib.sha256()
    for name in sorted(paths):
        if not name or not (name == "CMakeLists.txt" or name.startswith(
                ("src/", "CMakeModules/", "externals/"))):
            continue
        path = root / name
        if path.is_file():
            digest.update(name.encode() + b"\0" + path.read_bytes() + b"\0")
    digest.update(NDK_VERSION.encode())
    return digest.hexdigest()


def validate_archive(archive, expected_id, compiler):
    with zipfile.ZipFile(archive) as package:
        names = package.namelist()
        if len(names) != len(set(names)):
            raise ValueError("Duplicate ZIP entries")
        if any("/" in name or "\\" in name or name in (".", "..") for name in names):
            raise ValueError("Only flat profile exports are accepted")
        if sum(info.file_size for info in package.infolist()) > MAX_PROFILE_BYTES:
            raise ValueError("Profile archive exceeds 256 MiB")
        manifest = json.loads(package.read("manifest.json"))
        if manifest.get("complete") is not True:
            raise ValueError("Training session is incomplete")
        if manifest.get("abi") != "arm64-v8a":
            raise ValueError("Expected arm64-v8a training")
        build = manifest.get("build", {})
        if build.get("instrumented") is not True or build.get("suite_version") != 1:
            raise ValueError("Unsupported training build or suite")
        if build.get("build_id") != expected_id:
            raise ValueError("Training source identity does not match this build")
        if build.get("compiler") != compiler:
            raise ValueError("Training compiler version does not match the NDK compiler")
        stages = manifest.get("stages", [])
        if len(stages) != 4 or [stage.get("stage") for stage in stages] != list(range(4)):
            raise ValueError("All four ordered stages are required")
        profiles = []
        for index, stage in enumerate(stages):
            name = f"stage-{index}.profraw"
            if stage.get("success") is not True or stage.get("file") != name:
                raise ValueError(f"Stage {index} failed or has an invalid filename")
            raw = package.read(name)
            if not raw or len(raw) != stage.get("bytes"):
                raise ValueError(f"Stage {index} profile size mismatch")
            if hashlib.sha256(raw).hexdigest() != stage.get("sha256"):
                raise ValueError(f"Stage {index} profile hash mismatch")
            profiles.append((name, raw))
        if set(names) != {"manifest.json", *(name for name, _ in profiles)}:
            raise ValueError("Unexpected files in profile export")
        return profiles


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    identity = sub.add_parser("identity")
    identity.add_argument("--root", type=Path, default=Path.cwd())
    merge = sub.add_parser("merge")
    merge.add_argument("archive", type=Path)
    merge.add_argument("--build-id", required=True)
    merge.add_argument("--toolchain", type=Path, required=True)
    merge.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "identity":
        print(source_identity(args.root))
        return
    version = subprocess.check_output([str(args.toolchain / "clang"), "--version"], text=True)
    match = re.search(r"clang version (\d+\.\d+\.\d+)", version)
    if not match:
        raise ValueError("Cannot determine Clang version")
    profiles = validate_archive(args.archive, args.build_id, match.group(1))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as directory:
        files = []
        for name, raw in profiles:
            path = Path(directory) / name
            path.write_bytes(raw)
            files.append(str(path))
        temporary = Path(directory) / "merged.profdata"
        subprocess.run([str(args.toolchain / "llvm-profdata"), "merge", "-failure-mode=any",
                        *files, "-o", str(temporary)], check=True)
        # llvm-profdata validates format; retain all records (no -sparse) for PGO.
        summary = subprocess.check_output(
            [str(args.toolchain / "llvm-profdata"), "show", str(temporary)], text=True)
        count = re.search(r"Maximum function count:\s*(\d+)", summary)
        if not count or int(count.group(1)) == 0:
            raise ValueError("Training produced no executed profile counters")
        print(summary, end="")
        args.output.write_bytes(temporary.read_bytes())


if __name__ == "__main__":
    main()
