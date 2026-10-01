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
    // Eden's effective Vulkan memory budget usage.
    std::optional<u32> memory_budget_used_percent;
    // Android/Linux system RAM still available.
    std::optional<u32> ram_available_percent;
    // Samsung SGPU GTT pressure, when readable without elevated privileges.
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

struct XclipseMemoryPressureThresholds {
    u32 budget_elevated{78};
    u32 budget_high{88};
    u32 budget_critical{95};

    u32 ram_available_elevated{18};
    u32 ram_available_high{10};
    u32 ram_available_critical{5};

    u32 gtt_elevated{60};
    u32 gtt_high{80};
    u32 gtt_critical{94};

    float psi_some_elevated{7.5f};
    float psi_some_high{25.0f};
    float psi_some_critical{50.0f};
    float psi_full_elevated{0.5f};
    float psi_full_high{2.0f};
    float psi_full_critical{8.0f};
};

class XclipseMemoryPressureController {
public:
    XclipseMemoryPressureController() = default;

    [[nodiscard]] XclipseMemoryPressureSnapshot Tick(const Device& device, bool enabled);

    [[nodiscard]] XclipseMemoryPressureSnapshot ApplySample(
        const XclipseMemoryPressureSample& sample);

    [[nodiscard]] MemoryPressureClass Current() const noexcept {
        return current;
    }

    [[nodiscard]] const XclipseMemoryPressureSnapshot& LastSnapshot() const noexcept {
        return last_snapshot;
    }

    [[nodiscard]] static MemoryPressureClass Classify(
        const XclipseMemoryPressureSample& sample,
        const XclipseMemoryPressureThresholds& thresholds = {}) noexcept;

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
