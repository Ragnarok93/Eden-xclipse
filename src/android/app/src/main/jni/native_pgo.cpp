// SPDX-License-Identifier: GPL-3.0-or-later
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>

#include <jni.h>
#include <nlohmann/json.hpp>

#include "common/android/android_common.h"
#include "pgo_workloads.h"

#ifdef EDEN_PGO_GENERATE
extern "C" void __llvm_profile_set_filename(const char*);
extern "C" void __llvm_profile_reset_counters();
extern "C" int __llvm_profile_dump();
#endif

namespace {
std::mutex training_mutex;
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_yuzu_yuzu_1emu_utils_NativePgo_buildInfo(JNIEnv* env, jobject) {
    nlohmann::json info{{"suite_version", 1}, {"instrumented", false}};
#ifdef EDEN_PGO_GENERATE
    info["instrumented"] = true;
#endif
#ifdef EDEN_PGO_BUILD_ID
    info["build_id"] = EDEN_PGO_BUILD_ID;
    info["compiler"] = EDEN_PGO_COMPILER;
#endif
    return Common::Android::ToJString(env, info.dump());
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_yuzu_yuzu_1emu_utils_NativePgo_runStage(JNIEnv* env, jobject, jint stage,
                                               jstring output_file) {
    nlohmann::json result{{"stage", stage}, {"success", false}};
#ifdef EDEN_PGO_GENERATE
    std::lock_guard lock{training_mutex};
    try {
        if (stage < 0 || stage >= 4) {
            throw std::runtime_error("Unknown stage");
        }
        const auto filename = Common::Android::GetJString(env, output_file);
        const std::filesystem::path output{filename};
        if (!output.is_absolute() || output.extension() != ".profraw" ||
            filename.find('%') != std::string::npos || std::filesystem::exists(output)) {
            throw std::runtime_error("Profile output must be a fresh absolute .profraw path");
        }
        __llvm_profile_set_filename(filename.c_str());
        __llvm_profile_reset_counters();
        const auto started = std::chrono::steady_clock::now();
        result["detail"] = AndroidPgo::RunWorkload(stage);
        result["duration_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - started).count();
        // dump marks this snapshot complete, suppressing an atexit append that
        // would otherwise count the final stage twice. reset_counters clears it.
        if (__llvm_profile_dump() != 0 || !std::filesystem::exists(output) ||
            std::filesystem::file_size(output) == 0) {
            throw std::runtime_error("LLVM could not write profile data");
        }
        result["success"] = true;
    } catch (const std::exception& error) {
        result["error"] = error.what();
    }
#else
    result["error"] = "Install the PGO training APK to collect profiles";
#endif
    return Common::Android::ToJString(env, result.dump());
}
