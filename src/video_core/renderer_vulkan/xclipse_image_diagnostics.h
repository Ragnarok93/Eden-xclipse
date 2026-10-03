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

inline constexpr std::size_t XCLIPSE_IMAGE_DIAGNOSTIC_BINDING_LIMIT = 64;

class XclipseImageDiagnosticBindingSet {
public:
    [[nodiscard]] bool TryRemember(std::uint64_t image_view,
                                   std::uint64_t sampler) noexcept {
        if (count >= bindings.size()) {
            return false;
        }
        for (std::size_t index = 0; index < count; ++index) {
            if (bindings[index].image_view == image_view &&
                bindings[index].sampler == sampler) {
                return false;
            }
        }
        bindings[count++] = Binding{image_view, sampler};
        return true;
    }

private:
    struct Binding {
        std::uint64_t image_view;
        std::uint64_t sampler;
    };

    std::array<Binding, XCLIPSE_IMAGE_DIAGNOSTIC_BINDING_LIMIT> bindings{};
    std::size_t count{};
};

class XclipseImageDiagnosticBudget {
public:
    [[nodiscard]] bool HasRemaining(XclipseImageDiagnosticCategory category) const noexcept {
        const auto index = static_cast<std::size_t>(category);
        return index < counters.size() &&
               counters[index].load(std::memory_order_relaxed) <
                   XCLIPSE_IMAGE_DIAGNOSTIC_LIMIT;
    }

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
