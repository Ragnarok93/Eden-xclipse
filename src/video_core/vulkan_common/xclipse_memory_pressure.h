// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <chrono>
#include <optional>

#include "common/common_types.h"

namespace Vulkan {

class Device;

enum class MemoryPressureClass : u8 {
    Normal = 0,
    Elevated = 1,
    High = 2,
    Critical = 3,
};

[[nodiscard]] constexpr const char* MemoryPressureClassName(
    MemoryPressureClass pressure) noexcept {
    switch (pressure) {
    case MemoryPressureClass::Normal:
        return "normal";
    case MemoryPressureClass::Elevated:
        return "elevated";
    case MemoryPressureClass::High:
        return "high";
    case MemoryPressureClass::Critical:
        return "critical";
    }
    return "normal";
}

struct XclipseMemoryPressureSample {
    // Percent of current Vulkan heap budget in use.
    std::optional<u32> memory_budget_used_percent;
    // Percent of system RAM reported as MemAvailable.
    std::optional<u32> ram_available_percent;
    // Optional SGPU/GTT used percentage exposed by the kernel.
    std::optional<u32> gtt_used_percent;
    // Linux PSI memory avg10 values, when readable.
    std::optional<float> psi_some_avg10;
    std::optional<float> psi_full_avg10;
};

struct XclipseMemoryPressureSnapshot {
    MemoryPressureClass pressure{MemoryPressureClass::Normal};
    XclipseMemoryPressureSample sample{};
    bool sampled{};
    bool changed{};
    bool psi_trending_up{};
};

class XclipseMemoryPressureController {
public:
    XclipseMemoryPressureController() = default;

    /// Samples Xclipse pressure at most once per second.
    [[nodiscard]] XclipseMemoryPressureSnapshot Tick(const Device& device, bool enabled);

    /// Deterministic injection path used by tests and diagnostics.
    [[nodiscard]] XclipseMemoryPressureSnapshot ApplySample(
        const XclipseMemoryPressureSample& sample);

    [[nodiscard]] MemoryPressureClass Current() const noexcept {
        return current;
    }

    [[nodiscard]] const XclipseMemoryPressureSnapshot& LastSnapshot() const noexcept {
        return last_snapshot;
    }

    [[nodiscard]] static MemoryPressureClass Classify(
        const XclipseMemoryPressureSample& sample) noexcept;

private:
    [[nodiscard]] bool PushPsiAndCheckTrend(
        const XclipseMemoryPressureSample& sample) noexcept;

    MemoryPressureClass current{MemoryPressureClass::Normal};
    u32 lower_pressure_samples{};
    std::array<float, 3> psi_trend_window{};
    u32 psi_window_count{};
    u32 psi_window_head{};
    std::chrono::steady_clock::time_point last_poll{};
    XclipseMemoryPressureSnapshot last_snapshot{};
};

} // namespace Vulkan
