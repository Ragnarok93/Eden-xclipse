// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/vulkan_common/xclipse_memory_pressure.h"

TEST_CASE("Xclipse pressure uses the strongest available signal", "[video_core][xclipse]") {
    Vulkan::XclipseMemoryPressureSample sample{};
    sample.memory_budget_used_percent = 79;
    sample.ram_available_percent = 50;
    sample.process_rss_mib = 2300;
    REQUIRE(Vulkan::XclipseMemoryPressureController::Classify(sample) ==
            Vulkan::MemoryPressureClass::Elevated);

    sample.gtt_used_percent = 82;
    REQUIRE(Vulkan::XclipseMemoryPressureController::Classify(sample) ==
            Vulkan::MemoryPressureClass::High);

    sample.psi_full_avg10 = 8.5f;
    REQUIRE(Vulkan::XclipseMemoryPressureController::Classify(sample) ==
            Vulkan::MemoryPressureClass::Critical);
}

TEST_CASE("Xclipse RSS remains diagnostic until calibrated", "[video_core][xclipse]") {
    Vulkan::XclipseMemoryPressureSample sample{};
    sample.process_rss_mib = 3500;
    REQUIRE(Vulkan::XclipseMemoryPressureController::Classify(sample) ==
            Vulkan::MemoryPressureClass::Normal);
}

TEST_CASE("Xclipse pressure promotes immediately and demotes slowly",
          "[video_core][xclipse]") {
    Vulkan::XclipseMemoryPressureController controller;

    Vulkan::XclipseMemoryPressureSample critical{};
    critical.memory_budget_used_percent = 96;
    REQUIRE(controller.ApplySample(critical).pressure ==
            Vulkan::MemoryPressureClass::Critical);

    Vulkan::XclipseMemoryPressureSample normal{};
    normal.memory_budget_used_percent = 30;
    REQUIRE(controller.ApplySample(normal).pressure ==
            Vulkan::MemoryPressureClass::Critical);
    REQUIRE(controller.ApplySample(normal).pressure ==
            Vulkan::MemoryPressureClass::Critical);
    REQUIRE(controller.ApplySample(normal).pressure ==
            Vulkan::MemoryPressureClass::High);

    REQUIRE(controller.ApplySample(normal).pressure ==
            Vulkan::MemoryPressureClass::High);
    REQUIRE(controller.ApplySample(normal).pressure ==
            Vulkan::MemoryPressureClass::High);
    REQUIRE(controller.ApplySample(normal).pressure ==
            Vulkan::MemoryPressureClass::Elevated);
}

TEST_CASE("Xclipse rising PSI trend preemptively raises one class",
          "[video_core][xclipse]") {
    Vulkan::XclipseMemoryPressureController controller;
    Vulkan::XclipseMemoryPressureSample sample{};

    sample.psi_some_avg10 = 0.20f;
    REQUIRE(controller.ApplySample(sample).pressure == Vulkan::MemoryPressureClass::Normal);
    sample.psi_some_avg10 = 0.40f;
    REQUIRE(controller.ApplySample(sample).pressure == Vulkan::MemoryPressureClass::Normal);
    sample.psi_some_avg10 = 0.80f;
    const auto result = controller.ApplySample(sample);
    REQUIRE(result.psi_trending_up);
    REQUIRE(result.pressure == Vulkan::MemoryPressureClass::Elevated);
}

TEST_CASE("Xclipse isolated GTT pressure uses conservative bands", "[video_core][xclipse]") {
    Vulkan::XclipseMemoryPressureSample sample{};
    sample.gtt_used_percent = 59;
    REQUIRE(Vulkan::XclipseMemoryPressureController::Classify(sample) ==
            Vulkan::MemoryPressureClass::Normal);

    sample.gtt_used_percent = 60;
    REQUIRE(Vulkan::XclipseMemoryPressureController::Classify(sample) ==
            Vulkan::MemoryPressureClass::Elevated);

    sample.gtt_used_percent = 80;
    REQUIRE(Vulkan::XclipseMemoryPressureController::Classify(sample) ==
            Vulkan::MemoryPressureClass::High);
}

TEST_CASE("Xclipse texture GC requires Eden memory contribution",
          "[video_core][xclipse]") {
    using Vulkan::MemoryPressureClass;
    using Vulkan::XclipseMemoryPressureSample;
    using Vulkan::XclipseTextureGcPressure;

    XclipseMemoryPressureSample sample{};
    sample.memory_budget_used_percent = 13;
    sample.process_rss_percent = 5;

    REQUIRE(Vulkan::TextureGcPressureFor(MemoryPressureClass::Normal, sample) ==
            XclipseTextureGcPressure::None);
    REQUIRE(Vulkan::TextureGcPressureFor(MemoryPressureClass::Elevated, sample) ==
            XclipseTextureGcPressure::None);
    REQUIRE(Vulkan::TextureGcPressureFor(MemoryPressureClass::High, sample) ==
            XclipseTextureGcPressure::None);
    REQUIRE(Vulkan::TextureGcPressureFor(MemoryPressureClass::Critical, sample) ==
            XclipseTextureGcPressure::None);

    // The observed Xclipse failure mode: Eden itself occupies a large fraction of 8 GiB RAM
    // while Android has only Elevated free-memory pressure.
    sample.process_rss_percent = 50;
    REQUIRE(Vulkan::TextureGcPressureFor(MemoryPressureClass::Elevated, sample) ==
            XclipseTextureGcPressure::High);

    // Moderate Eden contribution under High pressure asks for the existing high-priority LRU.
    sample.process_rss_percent = 20;
    REQUIRE(Vulkan::TextureGcPressureFor(MemoryPressureClass::High, sample) ==
            XclipseTextureGcPressure::High);

    // Critical system pressure only invokes aggressive GC when Eden's contribution is heavy.
    sample.process_rss_percent = 35;
    REQUIRE(Vulkan::TextureGcPressureFor(MemoryPressureClass::Critical, sample) ==
            XclipseTextureGcPressure::Critical);

    sample.process_rss_percent = 5;
    sample.memory_budget_used_percent = 45;
    REQUIRE(Vulkan::TextureGcPressureFor(MemoryPressureClass::Critical, sample) ==
            XclipseTextureGcPressure::High);

    sample.memory_budget_used_percent = 65;
    REQUIRE(Vulkan::TextureGcPressureFor(MemoryPressureClass::Critical, sample) ==
            XclipseTextureGcPressure::Critical);
}


TEST_CASE("Xclipse staging reclaim starts only at high pressure",
          "[video_core][xclipse]") {
    using Vulkan::MemoryPressureClass;

    REQUIRE_FALSE(Vulkan::ShouldAggressivelyReclaimStaging(MemoryPressureClass::Normal));
    REQUIRE_FALSE(Vulkan::ShouldAggressivelyReclaimStaging(MemoryPressureClass::Elevated));
    REQUIRE(Vulkan::ShouldAggressivelyReclaimStaging(MemoryPressureClass::High));
    REQUIRE(Vulkan::ShouldAggressivelyReclaimStaging(MemoryPressureClass::Critical));
}
