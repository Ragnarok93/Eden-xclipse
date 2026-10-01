// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/vulkan_common/xclipse_memory_pressure.h"

TEST_CASE("Xclipse memory pressure takes the worst available signal", "[video_core]") {
    Vulkan::XclipseMemoryPressureSample sample{};
    sample.memory_budget_percent = 76;
    sample.ram_available_percent = 50;
    REQUIRE(Vulkan::XclipseMemoryPressureController::Classify(sample) ==
            Vulkan::MemoryPressureClass::Elevated);

    sample.gtt_percent = 60;
    REQUIRE(Vulkan::XclipseMemoryPressureController::Classify(sample) ==
            Vulkan::MemoryPressureClass::High);

    sample.psi_full_avg10 = 6.0f;
    REQUIRE(Vulkan::XclipseMemoryPressureController::Classify(sample) ==
            Vulkan::MemoryPressureClass::Critical);
}

TEST_CASE("Xclipse memory pressure promotes immediately and demotes with hysteresis",
          "[video_core]") {
    Vulkan::XclipseMemoryPressureController controller;

    Vulkan::XclipseMemoryPressureSample critical{};
    critical.memory_budget_percent = 95;
    REQUIRE(controller.ApplySample(critical).pressure ==
            Vulkan::MemoryPressureClass::Critical);

    Vulkan::XclipseMemoryPressureSample normal{};
    normal.memory_budget_percent = 30;
    REQUIRE(controller.ApplySample(normal).pressure ==
            Vulkan::MemoryPressureClass::Critical);
    REQUIRE(controller.ApplySample(normal).pressure ==
            Vulkan::MemoryPressureClass::Critical);
    REQUIRE(controller.ApplySample(normal).pressure ==
            Vulkan::MemoryPressureClass::High);
}

TEST_CASE("Xclipse PSI rising trend can preemptively raise one pressure class", "[video_core]") {
    Vulkan::XclipseMemoryPressureController controller;

    Vulkan::XclipseMemoryPressureSample sample{};
    sample.psi_full_avg10 = 0.10f;
    REQUIRE(controller.ApplySample(sample).pressure == Vulkan::MemoryPressureClass::Normal);

    sample.psi_full_avg10 = 0.20f;
    REQUIRE(controller.ApplySample(sample).pressure == Vulkan::MemoryPressureClass::Normal);

    sample.psi_full_avg10 = 0.30f;
    const auto result = controller.ApplySample(sample);
    REQUIRE(result.psi_trending_up);
    REQUIRE(result.pressure == Vulkan::MemoryPressureClass::Elevated);
}
