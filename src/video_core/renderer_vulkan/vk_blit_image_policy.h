// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vulkan/vulkan_core.h>

namespace Vulkan {

[[nodiscard]] constexpr VkFilter SelectBlitFilter(bool wants_linear,
                                                   bool supports_linear_filter) noexcept {
    return wants_linear && supports_linear_filter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}

} // namespace Vulkan
