// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/vk_sync_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

TEST_CASE("Xclipse sync policy narrows upload dependency stages", "[video_core]") {
    REQUIRE(Vulkan::SelectUploadBarrierDestinationStages(false) ==
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

    const VkPipelineStageFlags narrowed =
        Vulkan::SelectUploadBarrierDestinationStages(true);
    REQUIRE(narrowed == vk::PIPELINE_STAGE_GRAPHICS_COMPUTE_TRANSFER);
    REQUIRE((narrowed & VK_PIPELINE_STAGE_TRANSFER_BIT) != 0);
    REQUIRE((narrowed & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) != 0);
    REQUIRE((narrowed & VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT) != 0);
    REQUIRE(narrowed != VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
}
