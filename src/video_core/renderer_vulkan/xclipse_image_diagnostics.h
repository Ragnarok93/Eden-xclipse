// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Vulkan {

inline constexpr std::uint64_t XCLIPSE_IMAGE_DIAGNOSTIC_LIMIT = 64;

enum class XclipseImageDiagnosticCategory : std::size_t {
    Upload3dLayout,
    ImageCopyBounds,
    ReinterpretCopy,
    SamplerViewCapability,
    SamplerDepthComparison,
    Count,
};

class XclipseImageDiagnosticBudget {
public:
    [[nodiscard]] bool TryConsume(XclipseImageDiagnosticCategory category) noexcept {
        const auto index = static_cast<std::size_t>(category);
        if (index >= counters.size()) {
            return false;
        }
        return counters[index].fetch_add(1, std::memory_order_relaxed) <
               XCLIPSE_IMAGE_DIAGNOSTIC_LIMIT;
    }

private:
    static constexpr std::size_t CategoryCount =
        static_cast<std::size_t>(XclipseImageDiagnosticCategory::Count);
    std::array<std::atomic<std::uint64_t>, CategoryCount> counters{};
};

} // namespace Vulkan
