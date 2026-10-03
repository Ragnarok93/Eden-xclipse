# SPDX-License-Identifier: GPL-3.0-or-later
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[2] / "CMakeModules/AndroidPGO.cmake"
CMAKE = shutil.which("cmake") or str(Path.home() / ".local/bin/cmake")


@unittest.skipUnless(Path(CMAKE).is_file(), "CMake unavailable")
class BuildModes(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        sources = {
            "video_core": ["video_core/textures/decoders.cpp", "video_core/renderer_vulkan/untrained.cpp"],
            "shader_recompiler": ["shader_recompiler/backend/spirv/emit_spirv.cpp"],
            "common": ["common/dynamic_library.cpp", "common/untrained.cpp"],
            "bc_decoder": ["bc_decoder/bc_decoder.cpp"],
            "yuzu-android": ["jni.cpp"],
        }
        lines = ["cmake_minimum_required(VERSION 3.18)", "project(pgo_fixture LANGUAGES CXX)",
                 'set(CMAKE_CXX_COMPILER_ID "Clang")', "set(ANDROID ON)",
                 'set(CMAKE_ANDROID_ARCH_ABI "arm64-v8a")',
                 "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)"]
        for target, files in sources.items():
            for file in files:
                path = self.root / file
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int fixture() { return 1; }\n")
            kind = "SHARED" if target == "yuzu-android" else "STATIC"
            lines.append(f"add_library({target} {kind} {' '.join(files)})")
        lines.append(f'include("{MODULE}")')
        (self.root / "CMakeLists.txt").write_text("\n".join(lines))

    def configure(self, mode, *extra):
        return subprocess.run([CMAKE, "-S", str(self.root), "-B", str(self.root / "build"),
                               f"-DEDEN_PGO_MODE={mode}", *extra], capture_output=True, text=True)

    def commands(self):
        return json.loads((self.root / "build/compile_commands.json").read_text())

    def test_off_adds_no_pgo_flags(self):
        result = self.configure("OFF")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(all("fprofile" not in item["command"] for item in self.commands()))

    def test_generate_only_instruments_trained_sources(self):
        result = self.configure("GENERATE")
        self.assertEqual(result.returncode, 0, result.stderr)
        for item in self.commands():
            trained = not item["file"].endswith(("untrained.cpp", "jni.cpp"))
            self.assertEqual("-fprofile-instr-generate" in item["command"], trained)
            self.assertEqual("-fprofile-update=atomic" in item["command"], trained)
        link = (self.root / "build/CMakeFiles/yuzu-android.dir/link.txt").read_text()
        self.assertIn("-fprofile-instr-generate", link)

    def test_use_only_applies_profile_to_trained_sources(self):
        profile = self.root / "profile.profdata"
        profile.write_bytes(b"placeholder")
        result = self.configure("USE", f"-DEDEN_PGO_PROFILE={profile}")
        self.assertEqual(result.returncode, 0, result.stderr)
        for item in self.commands():
            trained = not item["file"].endswith(("untrained.cpp", "jni.cpp"))
            self.assertEqual("-fprofile-instr-use=" in item["command"], trained)
            self.assertNotIn("-fprofile-instr-generate", item["command"])
        link = (self.root / "build/CMakeFiles/yuzu-android.dir/link.txt").read_text()
        self.assertNotIn("-fprofile-instr-generate", link)

    def test_missing_profile_or_invalid_mode_fail_closed(self):
        for mode in ("USE", "INVALID"):
            with self.subTest(mode=mode):
                self.assertNotEqual(self.configure(mode).returncode, 0)

    def test_wrong_abi_is_rejected(self):
        path = self.root / "CMakeLists.txt"
        path.write_text(path.read_text().replace('"arm64-v8a"', '"x86_64"'))
        self.assertNotEqual(self.configure("GENERATE").returncode, 0)
