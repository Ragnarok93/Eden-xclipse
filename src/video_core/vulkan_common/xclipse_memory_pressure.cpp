// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/vulkan_common/xclipse_memory_pressure.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <string>

#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {
namespace {

MemoryPressureClass MaxPressure(MemoryPressureClass lhs, MemoryPressureClass rhs) noexcept {
    return static_cast<MemoryPressureClass>(
        std::max(static_cast<u8>(lhs), static_cast<u8>(rhs)));
}

MemoryPressureClass BudgetPressure(
    u32 used_percent, const XclipseMemoryPressureThresholds& t) noexcept {
    if (used_percent >= t.budget_critical) {
        return MemoryPressureClass::Critical;
    }
    if (used_percent >= t.budget_high) {
        return MemoryPressureClass::High;
    }
    if (used_percent >= t.budget_elevated) {
        return MemoryPressureClass::Elevated;
    }
    return MemoryPressureClass::Normal;
}

MemoryPressureClass RamPressure(
    u32 available_percent, const XclipseMemoryPressureThresholds& t) noexcept {
    if (available_percent <= t.ram_available_critical) {
        return MemoryPressureClass::Critical;
    }
    if (available_percent <= t.ram_available_high) {
        return MemoryPressureClass::High;
    }
    if (available_percent <= t.ram_available_elevated) {
        return MemoryPressureClass::Elevated;
    }
    return MemoryPressureClass::Normal;
}

MemoryPressureClass GttPressure(
    u32 used_percent, const XclipseMemoryPressureThresholds& t) noexcept {
    if (used_percent >= t.gtt_critical) {
        return MemoryPressureClass::Critical;
    }
    if (used_percent >= t.gtt_high) {
        return MemoryPressureClass::High;
    }
    if (used_percent >= t.gtt_elevated) {
        return MemoryPressureClass::Elevated;
    }
    return MemoryPressureClass::Normal;
}

MemoryPressureClass PsiPressure(
    const XclipseMemoryPressureSample& sample,
    const XclipseMemoryPressureThresholds& t) noexcept {
    const float some = sample.psi_some_avg10.value_or(0.0f);
    const float full = sample.psi_full_avg10.value_or(0.0f);
    if (full >= t.psi_full_critical || some >= t.psi_some_critical) {
        return MemoryPressureClass::Critical;
    }
    if (full >= t.psi_full_high || some >= t.psi_some_high) {
        return MemoryPressureClass::High;
    }
    if (full >= t.psi_full_elevated || some >= t.psi_some_elevated) {
        return MemoryPressureClass::Elevated;
    }
    return MemoryPressureClass::Normal;
}

std::optional<u64> ReadIntegerFile(const char* path) {
    std::ifstream file(path);
    u64 value{};
    if (!(file >> value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<u32> ReadGttUsedPercent() {
#if defined(__ANDROID__)
    struct Paths {
        const char* used;
        const char* total;
    };
    // Prefer the direct Samsung SGPU node used by WinXclipse. The DRM paths cover kernels that
    // expose the same counters through cardN. Avoid scanning sysfs every poll.
    constexpr std::array candidates{
        Paths{"/sys/devices/platform/22200000.sgpu/mem_info_gtt_used",
              "/sys/devices/platform/22200000.sgpu/mem_info_gtt_total"},
        Paths{"/sys/class/drm/card0/device/mem_info_gtt_used",
              "/sys/class/drm/card0/device/mem_info_gtt_total"},
        Paths{"/sys/class/drm/card1/device/mem_info_gtt_used",
              "/sys/class/drm/card1/device/mem_info_gtt_total"},
    };
    for (const auto& candidate : candidates) {
        const auto used = ReadIntegerFile(candidate.used);
        const auto total = ReadIntegerFile(candidate.total);
        if (used && total && *total != 0) {
            return static_cast<u32>(std::min<u64>(100, (*used * 100) / *total));
        }
    }
#endif
    return std::nullopt;
}

void ReadRamAvailability(XclipseMemoryPressureSample& sample) {
#if defined(__linux__) || defined(__ANDROID__)
    std::ifstream file("/proc/meminfo");
    std::string line;
    u64 total_kib{};
    u64 available_kib{};
    while (std::getline(file, line)) {
        unsigned long long parsed{};
        if (std::sscanf(line.c_str(), "MemTotal: %llu kB", &parsed) == 1) {
            total_kib = static_cast<u64>(parsed);
        } else if (std::sscanf(line.c_str(), "MemAvailable: %llu kB", &parsed) == 1) {
            available_kib = static_cast<u64>(parsed);
        }
        if (total_kib != 0 && available_kib != 0) {
            break;
        }
    }
    if (total_kib != 0) {
        sample.ram_available_percent =
            static_cast<u32>(std::min<u64>(100, available_kib * 100 / total_kib));
    }
#endif
}

void ReadPsi(XclipseMemoryPressureSample& sample) {
#if defined(__linux__) || defined(__ANDROID__)
    std::ifstream file("/proc/pressure/memory");
    std::string line;
    while (std::getline(file, line)) {
        float avg10{};
        float avg60{};
        if (line.rfind("some ", 0) == 0 &&
            std::sscanf(line.c_str(), "some avg10=%f avg60=%f", &avg10, &avg60) == 2) {
            sample.psi_some_avg10 = avg10;
        } else if (line.rfind("full ", 0) == 0 &&
                   std::sscanf(line.c_str(), "full avg10=%f avg60=%f", &avg10, &avg60) == 2) {
            sample.psi_full_avg10 = avg10;
        }
    }
#endif
}

XclipseMemoryPressureSample ReadSample(const Device& device) {
    XclipseMemoryPressureSample sample{};

    if (device.CanReportMemoryUsage()) {
        const u64 budget = device.GetDeviceLocalMemory();
        const u64 usage = device.GetDeviceMemoryUsageAboveBaseline();
        if (budget != 0) {
            sample.memory_budget_used_percent =
                static_cast<u32>(std::min<u64>(100, usage * 100 / budget));
        }
    }

    ReadRamAvailability(sample);
    ReadPsi(sample);
    sample.gtt_used_percent = ReadGttUsedPercent();
    return sample;
}

float PsiTrendScore(const XclipseMemoryPressureSample& sample) noexcept {
    // Full memory stalls carry more weight than "some" stalls, but direction matters more than
    // absolute scale here. The classifier still handles severity independently.
    if (sample.psi_full_avg10) {
        return *sample.psi_full_avg10 * 8.0f;
    }
    return sample.psi_some_avg10.value_or(0.0f);
}

} // namespace

MemoryPressureClass XclipseMemoryPressureController::Classify(
    const XclipseMemoryPressureSample& sample,
    const XclipseMemoryPressureThresholds& thresholds) noexcept {
    MemoryPressureClass pressure = MemoryPressureClass::Normal;
    if (sample.memory_budget_used_percent) {
        pressure = MaxPressure(
            pressure, BudgetPressure(*sample.memory_budget_used_percent, thresholds));
    }
    if (sample.ram_available_percent) {
        pressure =
            MaxPressure(pressure, RamPressure(*sample.ram_available_percent, thresholds));
    }
    if (sample.gtt_used_percent) {
        pressure = MaxPressure(pressure, GttPressure(*sample.gtt_used_percent, thresholds));
    }
    return MaxPressure(pressure, PsiPressure(sample, thresholds));
}

bool XclipseMemoryPressureController::PushPsiAndCheckTrend(
    const XclipseMemoryPressureSample& sample) noexcept {
    if (!sample.psi_some_avg10 && !sample.psi_full_avg10) {
        return false;
    }

    const float score = PsiTrendScore(sample);
    psi_trend_window[psi_window_head] = score;
    psi_window_head = (psi_window_head + 1) % psi_trend_window.size();
    psi_window_count =
        std::min<u32>(psi_window_count + 1, static_cast<u32>(psi_trend_window.size()));
    if (psi_window_count < psi_trend_window.size()) {
        return false;
    }

    const u32 oldest = psi_window_head;
    float previous = psi_trend_window[oldest];
    if (previous < 0.10f) {
        return false;
    }
    for (u32 index = 1; index < psi_trend_window.size(); ++index) {
        const float current_value =
            psi_trend_window[(oldest + index) % psi_trend_window.size()];
        if (current_value <= previous) {
            return false;
        }
        previous = current_value;
    }
    return true;
}

XclipseMemoryPressureSnapshot XclipseMemoryPressureController::ApplySample(
    const XclipseMemoryPressureSample& sample) {
    MemoryPressureClass raw = Classify(sample);
    const bool psi_trending_up = PushPsiAndCheckTrend(sample);
    if (psi_trending_up && raw < MemoryPressureClass::High) {
        raw = static_cast<MemoryPressureClass>(static_cast<u8>(raw) + 1);
    }

    const MemoryPressureClass previous = current;
    if (raw > current) {
        current = raw;
        lower_pressure_samples = 0;
    } else if (raw < current) {
        // Recover gradually so cache targets do not oscillate when Android memory pressure hovers
        // around a boundary. Promotion remains immediate.
        if (++lower_pressure_samples >= 3) {
            current = static_cast<MemoryPressureClass>(static_cast<u8>(current) - 1);
            lower_pressure_samples = 0;
        }
    } else {
        lower_pressure_samples = 0;
    }

    last_snapshot = {
        .pressure = current,
        .sample = sample,
        .sampled = true,
        .changed = current != previous,
        .psi_trending_up = psi_trending_up,
    };
    return last_snapshot;
}

XclipseMemoryPressureSnapshot XclipseMemoryPressureController::Tick(
    const Device& device, bool enabled) {
    if (!enabled || !device.IsXclipse()) {
        current = MemoryPressureClass::Normal;
        lower_pressure_samples = 0;
        last_snapshot = {
            .pressure = current,
            .sampled = false,
        };
        return last_snapshot;
    }

    const auto now = std::chrono::steady_clock::now();
    if (last_poll.time_since_epoch().count() != 0 &&
        now - last_poll < std::chrono::seconds(1)) {
        last_snapshot.sampled = false;
        last_snapshot.changed = false;
        return last_snapshot;
    }
    last_poll = now;
    return ApplySample(ReadSample(device));
}

} // namespace Vulkan
