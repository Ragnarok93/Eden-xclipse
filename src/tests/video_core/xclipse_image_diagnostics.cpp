// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/xclipse_image_diagnostics.h"

TEST_CASE("Xclipse image diagnostics have independent bounded budgets",
          "[video_core][xclipse]") {
    using Vulkan::XCLIPSE_IMAGE_DIAGNOSTIC_LIMIT;
    using Vulkan::XclipseImageDiagnosticBudget;
    using Vulkan::XclipseImageDiagnosticCategory;

    XclipseImageDiagnosticBudget budget;
    REQUIRE_FALSE(budget.TryConsume(XclipseImageDiagnosticCategory::Count));
    REQUIRE_FALSE(budget.HasRemaining(XclipseImageDiagnosticCategory::Count));

    for (std::uint64_t i = 0; i < XCLIPSE_IMAGE_DIAGNOSTIC_LIMIT; ++i) {
        REQUIRE(budget.TryConsume(XclipseImageDiagnosticCategory::Upload3dLayout));
    }

    REQUIRE_FALSE(budget.TryConsume(XclipseImageDiagnosticCategory::Upload3dLayout));
    REQUIRE_FALSE(budget.HasRemaining(XclipseImageDiagnosticCategory::Upload3dLayout));
    REQUIRE(budget.HasRemaining(XclipseImageDiagnosticCategory::ImageCopyBounds));
    REQUIRE(budget.TryConsume(XclipseImageDiagnosticCategory::ImageCopyBounds));
    REQUIRE(budget.TryConsume(XclipseImageDiagnosticCategory::ReinterpretCopy));

    for (std::uint64_t i = 0; i < XCLIPSE_IMAGE_DIAGNOSTIC_LIMIT; ++i) {
        REQUIRE(budget.TryConsume(XclipseImageDiagnosticCategory::SamplerViewCapability));
    }
    REQUIRE_FALSE(budget.TryConsume(XclipseImageDiagnosticCategory::SamplerViewCapability));
    REQUIRE(budget.TryConsume(XclipseImageDiagnosticCategory::SamplerDepthComparison));

    for (std::uint64_t i = 1; i < XCLIPSE_IMAGE_DIAGNOSTIC_LIMIT; ++i) {
        REQUIRE(budget.TryConsume(XclipseImageDiagnosticCategory::SamplerDepthComparison));
    }
    REQUIRE_FALSE(budget.TryConsume(XclipseImageDiagnosticCategory::SamplerDepthComparison));
    REQUIRE_FALSE(budget.HasRemaining(XclipseImageDiagnosticCategory::SamplerDepthComparison));
}

TEST_CASE("Xclipse image diagnostics remember depth-compare bindings once",
          "[video_core][xclipse]") {
    using Vulkan::XclipseImageDiagnosticBindingSet;

    XclipseImageDiagnosticBindingSet bindings;
    REQUIRE(bindings.TryRemember(0x100, 0x200));
    REQUIRE_FALSE(bindings.TryRemember(0x100, 0x200));
    REQUIRE(bindings.TryRemember(0x101, 0x200));
    REQUIRE(bindings.TryRemember(0x100, 0x201));
}

TEST_CASE("Xclipse image diagnostics bound remembered depth-compare bindings",
          "[video_core][xclipse]") {
    using Vulkan::XCLIPSE_IMAGE_DIAGNOSTIC_BINDING_LIMIT;
    using Vulkan::XclipseImageDiagnosticBindingSet;

    XclipseImageDiagnosticBindingSet bindings;
    for (std::uint64_t index = 0; index < XCLIPSE_IMAGE_DIAGNOSTIC_BINDING_LIMIT; ++index) {
        REQUIRE(bindings.TryRemember(0x100 + index, 0x200 + index));
    }

    REQUIRE_FALSE(bindings.TryRemember(0x300, 0x400));
}
