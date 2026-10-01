// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/vk_pipeline_policy.h"

TEST_CASE("Xclipse pipeline policy accepts Eden dynamic viewport producer shape", "[video_core]") {
    Vulkan::VulkanDevicePolicy policy{};
    policy.xclipse.detected = true;

    constexpr std::array dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };
    const VkPipelineViewportStateCreateInfo viewport_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = nullptr,
        .scissorCount = 1,
        .pScissors = nullptr,
    };
    const VkGraphicsPipelineCreateInfo pipeline_ci{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pViewportState = &viewport_ci,
        .pDynamicState = &dynamic_ci,
    };

    REQUIRE(Vulkan::InspectGraphicsPipeline(policy, pipeline_ci).Clean());
}

TEST_CASE("Xclipse pipeline policy detects WinXclipse defensive cases", "[video_core]") {
    Vulkan::VulkanDevicePolicy policy{};
    policy.xclipse.detected = true;
    policy.capabilities.required_subgroup_size = Vulkan::CapabilityState::Advertised;
    policy.capabilities.min_subgroup_size = 32;
    policy.capabilities.max_subgroup_size = 64;

    constexpr std::array dynamic_states{VK_DYNAMIC_STATE_VIEWPORT};
    const VkPipelineDynamicStateCreateInfo dynamic_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };
    const VkViewport viewport{};
    const VkPipelineViewportStateCreateInfo viewport_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = nullptr,
    };
    const VkPipelineColorBlendStateCreateInfo blend_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 0,
        .pAttachments = reinterpret_cast<const VkPipelineColorBlendAttachmentState*>(1),
    };
    const VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT required{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT,
        .requiredSubgroupSize = 128,
    };
    const VkPipelineShaderStageCreateInfo stage{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = &required,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .pName = "main",
    };
    const VkGraphicsPipelineCreateInfo pipeline_ci{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 1,
        .pStages = &stage,
        .pViewportState = &viewport_ci,
        .pColorBlendState = &blend_ci,
        .pDynamicState = &dynamic_ci,
    };

    const auto report = Vulkan::InspectGraphicsPipeline(policy, pipeline_ci);
    REQUIRE_FALSE(report.Clean());
    REQUIRE(report.issue_count == 4);
    REQUIRE((static_cast<u32>(report.issues) &
             static_cast<u32>(Vulkan::PipelinePolicyIssue::PNextTraversalLimit)) == 0);
}
