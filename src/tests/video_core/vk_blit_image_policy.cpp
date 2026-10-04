// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/vk_blit_image_policy.h"

TEST_CASE("Vulkan blits fall back when linear filtering is unsupported",
          "[video_core][vulkan]") {
    REQUIRE(Vulkan::SelectBlitFilter(false, true) == VK_FILTER_NEAREST);
    REQUIRE(Vulkan::SelectBlitFilter(true, false) == VK_FILTER_NEAREST);
    REQUIRE(Vulkan::SelectBlitFilter(true, true) == VK_FILTER_LINEAR);
}
