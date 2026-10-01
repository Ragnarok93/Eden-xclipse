// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vulkan/vulkan.h>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_device_profile.h"

namespace Vulkan {

enum class PipelinePolicyIssue : u32 {
    None = 0,
    DynamicViewportHasStaticPointer = 1U << 0,
    DynamicScissorHasStaticPointer = 1U << 1,
    ZeroBlendCountHasPointer = 1U << 2,
    ZeroDynamicStateCountHasPointer = 1U << 3,
    InvalidSpecializationInfo = 1U << 4,
    LegacyRenderPassHasDynamicRendering = 1U << 5,
    RequiredSubgroupOutOfRange = 1U << 6,
    RequiredSubgroupUnvalidated = 1U << 7,
    PNextTraversalLimit = 1U << 8,
};

constexpr PipelinePolicyIssue operator|(PipelinePolicyIssue lhs, PipelinePolicyIssue rhs) noexcept {
    return static_cast<PipelinePolicyIssue>(static_cast<u32>(lhs) | static_cast<u32>(rhs));
}

constexpr PipelinePolicyIssue& operator|=(PipelinePolicyIssue& lhs, PipelinePolicyIssue rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

struct PipelinePolicyReport {
    PipelinePolicyIssue issues{PipelinePolicyIssue::None};
    u32 issue_count{};

    [[nodiscard]] bool Clean() const noexcept {
        return issues == PipelinePolicyIssue::None;
    }
};

[[nodiscard]] PipelinePolicyReport InspectGraphicsPipeline(
    const VulkanDevicePolicy& policy, const VkGraphicsPipelineCreateInfo& create_info) noexcept;

} // namespace Vulkan
