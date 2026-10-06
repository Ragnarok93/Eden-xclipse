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

TEST_CASE("Xclipse pipeline policy validates the requested subgroup size exactly",
          "[video_core][xclipse]") {
    Vulkan::VulkanDevicePolicy policy{};
    policy.xclipse.detected = true;
    // A successful wave64 probe must not validate pipelines that require guest wave32.
    policy.xclipse.wave64_validated = true;
    policy.capabilities.required_subgroup_size = Vulkan::CapabilityState::Validated;
    policy.capabilities.required_subgroup_size_stages = VK_SHADER_STAGE_VERTEX_BIT;
    policy.capabilities.min_subgroup_size = 32;
    policy.capabilities.max_subgroup_size = 128;
    policy.use_xclipse_subgroup_size_control = true;

    const VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT required{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT,
        .requiredSubgroupSize = 32,
    };
    const VkPipelineShaderStageCreateInfo stage{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = &required,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .pName = "main",
    };
    const VkGraphicsPipelineCreateInfo pipeline_ci{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 1,
        .pStages = &stage,
    };

    const auto unvalidated_report = Vulkan::InspectGraphicsPipeline(policy, pipeline_ci);
    REQUIRE((static_cast<u32>(unvalidated_report.issues) &
             static_cast<u32>(Vulkan::PipelinePolicyIssue::RequiredSubgroupUnvalidated)) != 0);

    policy.xclipse.wave32_validated = true;
    policy.xclipse.preferred_compute_wave = 32;
    Vulkan::UpdateXclipseSubgroupSizePolicy(policy, true);
    const auto validated_report = Vulkan::InspectGraphicsPipeline(policy, pipeline_ci);
    REQUIRE(validated_report.Clean());

    VkPipelineShaderStageCreateInfo unsupported_stage = stage;
    unsupported_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    const VkGraphicsPipelineCreateInfo unsupported_stage_pipeline{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 1,
        .pStages = &unsupported_stage,
    };
    const auto unsupported_stage_report =
        Vulkan::InspectGraphicsPipeline(policy, unsupported_stage_pipeline);
    REQUIRE((static_cast<u32>(unsupported_stage_report.issues) &
             static_cast<u32>(Vulkan::PipelinePolicyIssue::RequiredSubgroupUnvalidated)) != 0);
}


TEST_CASE("Xclipse policy hash is stable across diagnostic timing changes", "[video_core][xclipse]") {
    Vulkan::VulkanDevicePolicy baseline{};
    baseline.identity.device_name = "Xclipse 940";
    baseline.identity.driver_name = "Samsung";
    baseline.identity.soc_model = "Exynos 2400";
    baseline.identity.vendor_id = 0x144d;
    baseline.identity.device_id = 0x0001;
    baseline.identity.driver_id = 0x0002;
    baseline.identity.driver_version = 0x00030004;
    baseline.xclipse.detected = true;
    baseline.xclipse.generation = 1;
    baseline.xclipse.model = 940;
    baseline.xclipse.wave32_validated = true;
    baseline.xclipse.wave64_validated = true;
    baseline.xclipse.allowed_wave_mask = 0x3;
    baseline.xclipse.preferred_compute_wave = 32;
    baseline.xclipse.descriptor_buffer_validated = true;
    baseline.xclipse.synchronization2_validated = true;
    baseline.optimization_probes.timestamp_queries = Vulkan::CapabilityState::Validated;
    baseline.optimization_probes.buffer_transfer = Vulkan::CapabilityState::Validated;
    baseline.optimization_probes.empty_submit_ns = 100;
    baseline.optimization_probes.copy_64k_ns = 200;
    baseline.optimization_probes.copy_1m_ns = 300;
    baseline.optimization_probes.copy_4m_ns = 400;

    const auto baseline_hash = Vulkan::ComputeVulkanPolicyHash(baseline);

    auto timing_changed = baseline;
    timing_changed.xclipse.wave32_probe_ns = 999'999;
    timing_changed.xclipse.wave64_probe_ns = 888'888;
    timing_changed.optimization_probes.empty_submit_ns = 10'000;
    timing_changed.optimization_probes.copy_64k_ns = 20'000;
    timing_changed.optimization_probes.copy_1m_ns = 30'000;
    timing_changed.optimization_probes.copy_4m_ns = 40'000;
    REQUIRE(Vulkan::ComputeVulkanPolicyHash(timing_changed) == baseline_hash);

    auto capability_changed = baseline;
    capability_changed.capabilities.timeline = Vulkan::CapabilityState::Validated;
    REQUIRE(Vulkan::ComputeVulkanPolicyHash(capability_changed) != baseline_hash);

    auto policy_changed = baseline;
    policy_changed.use_xclipse_sync_policy = true;
    REQUIRE(Vulkan::ComputeVulkanPolicyHash(policy_changed) != baseline_hash);

    auto descriptor_image_changed = baseline;
    descriptor_image_changed.xclipse.descriptor_buffer_image_validated = true;
    REQUIRE(Vulkan::ComputeVulkanPolicyHash(descriptor_image_changed) != baseline_hash);
}
