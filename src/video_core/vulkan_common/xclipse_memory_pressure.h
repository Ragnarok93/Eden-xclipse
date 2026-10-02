// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <string>

#include "common/common_types.h"

namespace Vulkan {

class Device;

enum class MemoryPressureClass : u8 {
    Normal = 0,
    Elevated = 1,
    High = 2,
    Critical = 3,
};

enum class XclipseTextureGcPressure : u8 {
    None = 0,
    High = 1,
    Critical = 2,
};

[[nodiscard]] constexpr XclipseTextureGcPressure TextureGcPressureFor(
    MemoryPressureClass pressure) noexcept {
    switch (pressure) {
    case MemoryPressureClass::High:
        return XclipseTextureGcPressure::High;
    case MemoryPressureClass::Critical:
        return XclipseTextureGcPressure::Critical;
    case MemoryPressureClass::Normal:
    case MemoryPressureClass::Elevated:
    default:
        return XclipseTextureGcPressure::None;
    }
}

[[nodiscard]] constexpr const char* TextureGcPressureName(
    XclipseTextureGcPressure pressure) noexcept {
    switch (pressure) {
    case XclipseTextureGcPressure::High:
        return "high";
    case XclipseTextureGcPressure::Critical:
        return "critical";
    case XclipseTextureGcPressure::None:
    default:
        return "none";
    }
}

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
    // Percent of Eden's current Vulkan memory budget reported in use.
    std::optional<u32> memory_budget_used_percent;
    // Percent of system RAM reported as MemAvailable.
    std::optional<u32> ram_available_percent;
    // Current process RSS in MiB from /proc/self/status.
    std::optional<u32> process_rss_mib;
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

    /// Samples Xclipse pressure at most once per second. This is diagnostic-only; callers decide
    /// whether and how to feed the signal into existing Eden cache policy.
    [[nodiscard]] XclipseMemoryPressureSnapshot Tick(const Device& device, bool enabled);

    /// Deterministic injection path used by tests.
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
    [[nodiscard]] XclipseMemoryPressureSample ReadSample(const Device& device);
    [[nodiscard]] std::optional<u32> ReadGttUsedPercent();

    MemoryPressureClass current{MemoryPressureClass::Normal};
    u32 lower_pressure_samples{};
    std::array<float, 3> psi_trend_window{};
    u32 psi_window_count{};
    u32 psi_window_head{};
    std::chrono::steady_clock::time_point last_poll{};
    XclipseMemoryPressureSnapshot last_snapshot{};

    // Discover the Samsung GTT sysfs base at most once. Re-scanning /sys/class/drm every second
    // is needless I/O in the renderer frame loop.
    bool gtt_discovery_done{};
    std::optional<std::string> gtt_base;
};

} // namespace Vulkan
