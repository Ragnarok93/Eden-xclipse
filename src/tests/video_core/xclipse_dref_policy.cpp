// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/runtime_info.h"
#include "shader_recompiler/shader_info.h"
#include "video_core/renderer_vulkan/vk_rescaling_push_constant.h"

TEST_CASE("Xclipse DREF execution mode selection", "[video_core][xclipse][dref]") {
    using Shader::DrefExecutionMode;
    using Shader::SelectDrefExecutionMode;
    using Shader::TexturePixelFormat;

    REQUIRE(SelectDrefExecutionMode(false, false, TexturePixelFormat::R32_FLOAT, true) ==
            DrefExecutionMode::NonDref);
    REQUIRE(SelectDrefExecutionMode(true, false, TexturePixelFormat::D32_FLOAT, true) ==
            DrefExecutionMode::NativeDref);
    REQUIRE(SelectDrefExecutionMode(true, false, TexturePixelFormat::R32_FLOAT, true) ==
            DrefExecutionMode::SoftwareDref);
    REQUIRE(SelectDrefExecutionMode(true, false, TexturePixelFormat::R32_FLOAT, false) ==
            DrefExecutionMode::NativeDref);
    REQUIRE(SelectDrefExecutionMode(true, true, TexturePixelFormat::R32_FLOAT, true) ==
            DrefExecutionMode::RuntimeValidatedDref);

    // Ordinary R32 sampling must never inherit shadow/DREF semantics simply because the
    // underlying guest texture can also be viewed by a DREF descriptor.
    REQUIRE(SelectDrefExecutionMode(false, false, TexturePixelFormat::R32_FLOAT, true) ==
            DrefExecutionMode::NonDref);
}

TEST_CASE("Xclipse software DREF comparison semantics", "[video_core][xclipse][dref]") {
    using Shader::CompareFunction;
    using Shader::EvaluateDrefCompare;

    constexpr float low = 0.25f;
    constexpr float high = 0.75f;

    REQUIRE_FALSE(EvaluateDrefCompare(CompareFunction::Never, low, high));
    REQUIRE(EvaluateDrefCompare(CompareFunction::Less, low, high));
    REQUIRE(EvaluateDrefCompare(CompareFunction::Equal, high, high));
    REQUIRE(EvaluateDrefCompare(CompareFunction::LessThanEqual, high, high));
    REQUIRE(EvaluateDrefCompare(CompareFunction::Greater, high, low));
    REQUIRE(EvaluateDrefCompare(CompareFunction::NotEqual, low, high));
    REQUIRE(EvaluateDrefCompare(CompareFunction::GreaterThanEqual, high, high));
    REQUIRE(EvaluateDrefCompare(CompareFunction::Always, low, high));

    REQUIRE_FALSE(EvaluateDrefCompare(CompareFunction::Never, high, high));
    REQUIRE(EvaluateDrefCompare(CompareFunction::Always, high, high));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(EvaluateDrefCompare(CompareFunction::Less, nan, high));
    REQUIRE_FALSE(EvaluateDrefCompare(CompareFunction::Equal, nan, high));
    REQUIRE_FALSE(EvaluateDrefCompare(CompareFunction::LessThanEqual, nan, high));
    REQUIRE_FALSE(EvaluateDrefCompare(CompareFunction::Greater, nan, high));
    REQUIRE_FALSE(EvaluateDrefCompare(CompareFunction::NotEqual, nan, high));
    REQUIRE_FALSE(EvaluateDrefCompare(CompareFunction::GreaterThanEqual, nan, high));
    REQUIRE_FALSE(EvaluateDrefCompare(CompareFunction::Never, nan, nan));
    REQUIRE(EvaluateDrefCompare(CompareFunction::Always, nan, nan));
}

TEST_CASE("Xclipse DREF comparison ops pack per descriptor across word boundaries",
          "[video_core][xclipse][dref]") {
    Vulkan::RescalingPushConstant push;

    for (u32 descriptor = 0; descriptor < 10; ++descriptor) {
        const u32 compare_op = descriptor & 7u;
        push.SetDrefCompareOp(compare_op);
        push.PushTexture(false);
    }

    const auto& words = push.DrefCompareOps();
    for (u32 descriptor = 0; descriptor < 10; ++descriptor) {
        const u32 word = descriptor / Shader::Backend::SPIRV::DREF_COMPARE_OPS_PER_WORD;
        const u32 shift =
            (descriptor % Shader::Backend::SPIRV::DREF_COMPARE_OPS_PER_WORD) * 4u;
        REQUIRE(((words[word] >> shift) & 0x7u) == (descriptor & 7u));
    }
}

TEST_CASE("Xclipse DREF comparison packing is bounded at the maximum texture descriptor index",
          "[video_core][xclipse][dref]") {
    Vulkan::RescalingPushConstant push;
    constexpr u32 descriptor_capacity =
        Shader::Backend::SPIRV::NUM_DREF_COMPARE_OP_WORDS *
        Shader::Backend::SPIRV::DREF_COMPARE_OPS_PER_WORD;

    for (u32 descriptor = 0; descriptor + 1 < descriptor_capacity; ++descriptor) {
        push.PushTexture(false);
    }

    push.SetDrefCompareOp(static_cast<u32>(Shader::CompareFunction::Always));
    push.PushTexture(false);

    const auto before_overflow = push.DrefCompareOps();
    const u32 final_shift =
        ((descriptor_capacity - 1u) % Shader::Backend::SPIRV::DREF_COMPARE_OPS_PER_WORD) * 4u;
    REQUIRE(((before_overflow.back() >> final_shift) & 0x7u) ==
            static_cast<u32>(Shader::CompareFunction::Always));

    // One descriptor beyond the push-constant capacity must fail closed by leaving the
    // packed array unchanged rather than corrupting adjacent push constants.
    push.SetDrefCompareOp(static_cast<u32>(Shader::CompareFunction::Greater));
    REQUIRE(push.DrefCompareOps() == before_overflow);
}
