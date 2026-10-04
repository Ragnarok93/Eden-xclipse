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

[[nodiscard]] constexpr bool ShouldReclaimFreeStaging(
    MemoryPressureClass pressure) noexcept {
    return pressure != MemoryPressureClass::Normal;
}

[[nodiscard]] constexpr bool ShouldPreferStagingWaitReuse(
    MemoryPressureClass pressure) noexcept {
    return pressure == MemoryPressureClass::High ||
           pressure == MemoryPressureClass::Critical;
}

// Cached staging only; the persistent stream buffer is accounted separately. A zero limit means
// normal Eden behavior (uncapped). These bands intentionally become strict only after Android
// reports real system pressure.
[[nodiscard]] constexpr u64 XclipseStagingCacheLimitBytes(
    MemoryPressureClass pressure) noexcept {
    constexpr u64 MiB = 1024ULL * 1024ULL;
    switch (pressure) {
    case MemoryPressureClass::Elevated:
        return 384ULL * MiB;
    case MemoryPressureClass::High:
        return 192ULL * MiB;
    case MemoryPressureClass::Critical:
        return 96ULL * MiB;
    case MemoryPressureClass::Normal:
    default:
        return 0;
    }
}

// The persistent upload ring is allocated up front and is not part of the reclaimable cache.
// When Xclipse memory-pressure handling is enabled, cap its baseline footprint while preserving
// the normal size everywhere else. Requests that do not fit still use Eden's staging cache.
[[nodiscard]] constexpr u64 XclipsePressureStreamBufferSize(u64 stream_size,
                                                             bool enabled) noexcept {
    constexpr u64 limit = 128ULL * 1024ULL * 1024ULL;
    return enabled && stream_size > limit ? limit : stream_size;
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
    // Absolute MemAvailable from /proc/meminfo in KiB; diagnostic only.
    std::optional<u64> ram_available_kib;
    // Kernel-reported swap counters from /proc/meminfo in KiB; diagnostic only.
    std::optional<u64> swap_total_kib;
    std::optional<u64> swap_free_kib;
    std::optional<u64> swap_used_kib;
    // Current process RSS in MiB from /proc/self/status.
    std::optional<u32> process_rss_mib;
    // Process RSS as a percentage of total system RAM. This is used only to decide whether
    // Eden is materially contributing to already-detected system pressure.
    std::optional<u32> process_rss_percent;
    // Optional SGPU/GTT used percentage exposed by the kernel.
    std::optional<u32> gtt_used_percent;
    // Linux PSI memory avg10 values, when readable.
    std::optional<float> psi_some_avg10;
    std::optional<float> psi_full_avg10;
};

[[nodiscard]] constexpr std::optional<u64> XclipseSwapUsedKiB(
    std::optional<u64> total_kib, std::optional<u64> free_kib) noexcept {
    if (!total_kib || !free_kib || *free_kib > *total_kib) {
        return std::nullopt;
    }
    return *total_kib - *free_kib;
}

[[nodiscard]] inline XclipseTextureGcPressure TextureGcPressureFor(
    MemoryPressureClass pressure, const XclipseMemoryPressureSample& sample) noexcept {
    // Do not evict textures merely because the rest of Android is under pressure. Escalate
    // Eden's existing LRU only when Eden itself has a meaningful share of either system RAM
    // or the Vulkan budget. These bands are intentionally conservative and independently
    // observable in telemetry.
    const bool meaningful_contribution =
        (sample.process_rss_percent && *sample.process_rss_percent >= 15) ||
        (sample.memory_budget_used_percent && *sample.memory_budget_used_percent >= 40);
    const bool heavy_contribution =
        (sample.process_rss_percent && *sample.process_rss_percent >= 30) ||
        (sample.memory_budget_used_percent && *sample.memory_budget_used_percent >= 60);

    switch (pressure) {
    case MemoryPressureClass::Elevated:
        // An unusually large Eden process can justify early high-priority trimming even just
        // above the global High threshold. This matches the observed ~4 GiB RSS / 11% free case.
        return heavy_contribution ? XclipseTextureGcPressure::High
                                  : XclipseTextureGcPressure::None;
    case MemoryPressureClass::High:
        return meaningful_contribution ? XclipseTextureGcPressure::High
                                       : XclipseTextureGcPressure::None;
    case MemoryPressureClass::Critical:
        if (heavy_contribution) {
            return XclipseTextureGcPressure::Critical;
        }
        return meaningful_contribution ? XclipseTextureGcPressure::High
                                       : XclipseTextureGcPressure::None;
    case MemoryPressureClass::Normal:
    default:
        return XclipseTextureGcPressure::None;
    }
}

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

    /// Samples Xclipse pressure at most once per second. Consumers may use the sampled state only
    /// through conservative, independently disableable policy such as TextureGcPressureFor().
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
