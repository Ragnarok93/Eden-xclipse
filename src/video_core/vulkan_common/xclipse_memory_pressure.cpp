// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/vulkan_common/xclipse_memory_pressure.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {
namespace {

MemoryPressureClass MaxPressure(MemoryPressureClass lhs, MemoryPressureClass rhs) noexcept {
    return static_cast<MemoryPressureClass>(
        std::max(static_cast<u8>(lhs), static_cast<u8>(rhs)));
}

MemoryPressureClass PercentUsedLevel(u32 percent) noexcept {
    if (percent >= 93) {
        return MemoryPressureClass::Critical;
    }
    if (percent >= 85) {
        return MemoryPressureClass::High;
    }
    if (percent >= 75) {
        return MemoryPressureClass::Elevated;
    }
    return MemoryPressureClass::Normal;
}

MemoryPressureClass RamAvailableLevel(u32 percent) noexcept {
    if (percent <= 6) {
        return MemoryPressureClass::Critical;
    }
    if (percent <= 10) {
        return MemoryPressureClass::High;
    }
    if (percent <= 15) {
        return MemoryPressureClass::Elevated;
    }
    return MemoryPressureClass::Normal;
}

MemoryPressureClass GttLevel(u32 percent) noexcept {
    // Keep WinXclipse's measured SGPU pressure bands as initial Xclipse benchmark ranges.
    if (percent >= 90) {
        return MemoryPressureClass::Critical;
    }
    if (percent >= 55) {
        return MemoryPressureClass::High;
    }
    if (percent >= 35) {
        return MemoryPressureClass::Elevated;
    }
    return MemoryPressureClass::Normal;
}

MemoryPressureClass PsiLevel(const XclipseMemoryPressureSample& sample) noexcept {
    if (!sample.psi_some_avg10 && !sample.psi_full_avg10) {
        return MemoryPressureClass::Normal;
    }
    const float some = sample.psi_some_avg10.value_or(0.0f);
    const float full = sample.psi_full_avg10.value_or(0.0f);
    if (full > 5.0f || some > 40.0f) {
        return MemoryPressureClass::Critical;
    }
    if (full > 1.0f || some > 20.0f) {
        return MemoryPressureClass::High;
    }
    if (some > 5.0f) {
        return MemoryPressureClass::Elevated;
    }
    return MemoryPressureClass::Normal;
}

std::optional<u64> ReadIntegerFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    u64 value{};
    if (!(file >> value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<u32> ReadGttPercent() {
#if defined(__ANDROID__)
    constexpr std::array direct_bases{
        "/sys/devices/platform/22200000.sgpu",
        "/sys/class/drm/card0/device",
        "/sys/class/drm/card1/device",
    };
    for (const char* base : direct_bases) {
        const auto used = ReadIntegerFile(std::filesystem::path(base) / "mem_info_gtt_used");
        const auto total = ReadIntegerFile(std::filesystem::path(base) / "mem_info_gtt_total");
        if (used && total && *total != 0) {
            return static_cast<u32>(std::min<u64>(100, (*used * 100) / *total));
        }
    }

    std::error_code error;
    const std::filesystem::path drm{"/sys/class/drm"};
    for (std::filesystem::directory_iterator it{drm, error}, end; !error && it != end;
         it.increment(error)) {
        const auto device = it->path() / "device";
        const auto used = ReadIntegerFile(device / "mem_info_gtt_used");
        const auto total = ReadIntegerFile(device / "mem_info_gtt_total");
        if (used && total && *total != 0) {
            return static_cast<u32>(std::min<u64>(100, (*used * 100) / *total));
        }
    }
#endif
    return std::nullopt;
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

void ReadRamAvailability(XclipseMemoryPressureSample& sample) {
#if defined(__linux__) || defined(__ANDROID__)
    std::ifstream file("/proc/meminfo");
    std::string key;
    u64 value{};
    std::string unit;
    u64 total_kib{};
    u64 available_kib{};
    while (file >> key >> value >> unit) {
        if (key == "MemTotal:") {
            total_kib = value;
        } else if (key == "MemAvailable:") {
            available_kib = value;
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

XclipseMemoryPressureSample ReadSample(const Device& device) {
    XclipseMemoryPressureSample sample{};

    if (device.CanReportMemoryUsage()) {
        const u64 budget = device.GetDeviceMemoryBudget();
        const u64 usage = device.GetDeviceMemoryUsage();
        if (budget != 0) {
            sample.memory_budget_percent =
                static_cast<u32>(std::min<u64>(100, usage * 100 / budget));
        }
    }

    ReadRamAvailability(sample);
    ReadPsi(sample);
    sample.gtt_percent = ReadGttPercent();
    return sample;
}

} // namespace

MemoryPressureClass XclipseMemoryPressureController::Classify(
    const XclipseMemoryPressureSample& sample) noexcept {
    MemoryPressureClass result = MemoryPressureClass::Normal;
    if (sample.memory_budget_percent) {
        result = MaxPressure(result, PercentUsedLevel(*sample.memory_budget_percent));
    }
    if (sample.ram_available_percent) {
        result = MaxPressure(result, RamAvailableLevel(*sample.ram_available_percent));
    }
    if (sample.gtt_percent) {
        result = MaxPressure(result, GttLevel(*sample.gtt_percent));
    }
    result = MaxPressure(result, PsiLevel(sample));
    return result;
}

bool XclipseMemoryPressureController::PushPsiAndCheckTrend(
    const XclipseMemoryPressureSample& sample) noexcept {
    if (!sample.psi_full_avg10) {
        return false;
    }
    psi_full_window[psi_window_head] = *sample.psi_full_avg10;
    psi_window_head = (psi_window_head + 1) % psi_full_window.size();
    psi_window_count = std::min<u32>(psi_window_count + 1, psi_full_window.size());
    if (psi_window_count < psi_full_window.size()) {
        return false;
    }

    const u32 oldest = psi_window_head;
    float previous = psi_full_window[oldest];
    if (previous < 0.05f) {
        return false;
    }
    for (u32 index = 1; index < psi_full_window.size(); ++index) {
        const float current_value =
            psi_full_window[(oldest + index) % psi_full_window.size()];
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
        // Demote slowly to avoid oscillating cache targets on transient Android/streaming changes.
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
