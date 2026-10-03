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

    for (std::uint64_t i = 0; i < XCLIPSE_IMAGE_DIAGNOSTIC_LIMIT; ++i) {
        REQUIRE(budget.TryConsume(XclipseImageDiagnosticCategory::Upload3dLayout));
    }

    REQUIRE_FALSE(budget.TryConsume(XclipseImageDiagnosticCategory::Upload3dLayout));
    REQUIRE(budget.TryConsume(XclipseImageDiagnosticCategory::ImageCopyBounds));
    REQUIRE(budget.TryConsume(XclipseImageDiagnosticCategory::ReinterpretCopy));
    REQUIRE(budget.TryConsume(XclipseImageDiagnosticCategory::SamplerViewCapability));
}
