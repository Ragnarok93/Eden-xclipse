// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/shader_info.h"

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
}
