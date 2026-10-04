// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/vk_fragment_output.h"
#include "video_core/renderer_vulkan/xclipse_fragment_output_diagnostics.h"

TEST_CASE("Vulkan fragment output classes follow the guest format", "[video_core][vulkan]") {
    using Shader::AttributeType;
    using VideoCore::Surface::PixelFormat;

    REQUIRE(Vulkan::FragmentOutputTypeForPixelFormat(PixelFormat::R16G16B16A16_FLOAT) ==
            AttributeType::Float);
    REQUIRE(Vulkan::FragmentOutputTypeForPixelFormat(PixelFormat::R16G16B16A16_SINT) ==
            AttributeType::SignedInt);
    REQUIRE(Vulkan::FragmentOutputTypeForPixelFormat(PixelFormat::R16G16B16A16_UINT) ==
            AttributeType::UnsignedInt);
    REQUIRE(Vulkan::FragmentOutputTypeForPixelFormat(PixelFormat::Invalid) ==
            AttributeType::Disabled);

    REQUIRE(Vulkan::FragmentOutputTypeForVkFormat(VK_FORMAT_R16G16B16A16_UINT) ==
            AttributeType::UnsignedInt);
    REQUIRE(Vulkan::FragmentOutputTypeForVkFormat(VK_FORMAT_R16G16B16A16_SINT) ==
            AttributeType::SignedInt);
    REQUIRE(Vulkan::FragmentOutputTypeForVkFormat(VK_FORMAT_R16G16B16A16_SFLOAT) ==
            AttributeType::Float);
    REQUIRE(Vulkan::FragmentOutputTypeForVkFormat(VK_FORMAT_BC1_RGBA_UNORM_BLOCK) ==
            AttributeType::Disabled);
}

TEST_CASE("Inactive fragment targets are pruned and dual-source types follow target zero",
          "[video_core][vulkan]") {
    using Shader::AttributeType;
    using VideoCore::Surface::PixelFormat;

    std::array<PixelFormat, 8> formats;
    formats.fill(PixelFormat::Invalid);
    formats[0] = PixelFormat::R16G16B16A16_UINT;
    formats[1] = PixelFormat::R16G16B16A16_FLOAT;
    const auto output_types = Vulkan::MakeFragmentColorOutputTypes(formats, true);

    REQUIRE(output_types[0] == AttributeType::UnsignedInt);
    REQUIRE(output_types[1] == AttributeType::UnsignedInt);
    REQUIRE(output_types[2] == AttributeType::Disabled);
    REQUIRE_FALSE(Shader::ShouldDeclareFragmentColorOutput(
        output_types[2], true, false, false));
    REQUIRE_FALSE(Shader::ShouldEmitFragmentColorStore(output_types[2]));
    REQUIRE(Shader::ShouldDeclareFragmentColorOutput(output_types[1], false, false, true));

    formats.fill(PixelFormat::Invalid);
    const auto no_attachment_types = Vulkan::MakeFragmentColorOutputTypes(formats, true);
    REQUIRE(no_attachment_types[0] == AttributeType::Disabled);
    REQUIRE(no_attachment_types[1] == AttributeType::Disabled);
    REQUIRE_FALSE(Shader::ShouldDeclareFragmentColorOutput(
        no_attachment_types[1], false, false, true));
    REQUIRE(Shader::ShouldDeclareFragmentColorOutput(
        output_types[0], false, true, false));
}

TEST_CASE("Xclipse fragment-output diagnostic categories have independent bounds",
          "[video_core][xclipse]") {
    using Vulkan::XCLIPSE_FRAGMENT_OUTPUT_DIAGNOSTIC_LIMIT;
    using Vulkan::XclipseFragmentOutputDiagnosticBudget;
    using Vulkan::XclipseFragmentOutputDiagnosticCategory;

    XclipseFragmentOutputDiagnosticBudget budget;
    REQUIRE_FALSE(budget.TryConsume(XclipseFragmentOutputDiagnosticCategory::Count));
    for (u32 i = 0; i < XCLIPSE_FRAGMENT_OUTPUT_DIAGNOSTIC_LIMIT; ++i) {
        REQUIRE(budget.TryConsume(XclipseFragmentOutputDiagnosticCategory::NumericClassMismatch));
    }
    REQUIRE_FALSE(budget.TryConsume(
        XclipseFragmentOutputDiagnosticCategory::NumericClassMismatch));
    REQUIRE(budget.TryConsume(XclipseFragmentOutputDiagnosticCategory::OutputWithoutAttachment));
}


TEST_CASE("Xclipse fragment-output diagnostics ignore pruned outputs",
          "[video_core][xclipse]") {
    using Vulkan::ShouldDiagnoseUnattachedFragmentOutput;

    REQUIRE_FALSE(ShouldDiagnoseUnattachedFragmentOutput(false, false));
    REQUIRE(ShouldDiagnoseUnattachedFragmentOutput(false, true));
    REQUIRE_FALSE(ShouldDiagnoseUnattachedFragmentOutput(true, true));
}
