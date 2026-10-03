// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstddef>

#include "common/common_types.h"

namespace Vulkan {

inline constexpr u32 XCLIPSE_FRAGMENT_OUTPUT_DIAGNOSTIC_LIMIT = 32;

enum class XclipseFragmentOutputDiagnosticCategory : u8 {
    NumericClassMismatch,
    OutputWithoutAttachment,
    Count,
};

[[nodiscard]] constexpr const char* XclipseFragmentOutputDiagnosticName(
    XclipseFragmentOutputDiagnosticCategory category) noexcept {
    switch (category) {
    case XclipseFragmentOutputDiagnosticCategory::NumericClassMismatch:
        return "numeric-class-mismatch";
    case XclipseFragmentOutputDiagnosticCategory::OutputWithoutAttachment:
        return "unattached-output";
    case XclipseFragmentOutputDiagnosticCategory::Count:
        break;
    }
    return "invalid";
}

class XclipseFragmentOutputDiagnosticBudget {
public:
    [[nodiscard]] bool TryConsume(XclipseFragmentOutputDiagnosticCategory category) noexcept {
        const size_t index = static_cast<size_t>(category);
        if (index >= counters.size()) {
            return false;
        }

        u32 current = counters[index].load(std::memory_order_relaxed);
        while (current < XCLIPSE_FRAGMENT_OUTPUT_DIAGNOSTIC_LIMIT) {
            if (counters[index].compare_exchange_weak(current, current + 1,
                                                      std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }

private:
    std::array<std::atomic<u32>, 2> counters{};
};

} // namespace Vulkan
