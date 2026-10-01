// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/renderer_vulkan/vk_pipeline_policy.h"

namespace Vulkan {
namespace {

void AddIssue(PipelinePolicyReport& report, PipelinePolicyIssue issue) noexcept {
    report.issues |= issue;
    ++report.issue_count;
}

bool HasDynamicState(const VkPipelineDynamicStateCreateInfo* state, VkDynamicState target) noexcept {
    if (!state || state->dynamicStateCount == 0 || !state->pDynamicStates) {
        return false;
    }
    for (u32 index = 0; index < state->dynamicStateCount; ++index) {
        if (state->pDynamicStates[index] == target) {
            return true;
        }
    }
    return false;
}

bool BadSpecializationInfo(const VkSpecializationInfo* info) noexcept {
    if (!info) {
        return false;
    }
    if (info->mapEntryCount == 0) {
        return true;
    }
    if (!info->pMapEntries) {
        return true;
    }
    if (info->dataSize != 0 && !info->pData) {
        return true;
    }
    return false;
}

template <typename Predicate>
bool PNextContains(const void* pnext, Predicate&& predicate, bool& hit_limit) noexcept {
    auto* current = static_cast<const VkBaseInStructure*>(pnext);
    for (u32 depth = 0; current && depth < 32; ++depth) {
        if (predicate(current)) {
            return true;
        }
        current = current->pNext;
    }
    hit_limit = current != nullptr;
    return false;
}

} // namespace

PipelinePolicyReport InspectGraphicsPipeline(const VulkanDevicePolicy& policy,
                                             const VkGraphicsPipelineCreateInfo& create_info) noexcept {
    PipelinePolicyReport report{};

    const auto* dynamic_state = create_info.pDynamicState;
    if (dynamic_state && dynamic_state->dynamicStateCount == 0 &&
        dynamic_state->pDynamicStates != nullptr) {
        AddIssue(report, PipelinePolicyIssue::ZeroDynamicStateCountHasPointer);
    }

    if (create_info.pViewportState) {
        if (HasDynamicState(dynamic_state, VK_DYNAMIC_STATE_VIEWPORT) &&
            create_info.pViewportState->pViewports != nullptr) {
            AddIssue(report, PipelinePolicyIssue::DynamicViewportHasStaticPointer);
        }
        if (HasDynamicState(dynamic_state, VK_DYNAMIC_STATE_SCISSOR) &&
            create_info.pViewportState->pScissors != nullptr) {
            AddIssue(report, PipelinePolicyIssue::DynamicScissorHasStaticPointer);
        }
    }

    if (create_info.pColorBlendState &&
        create_info.pColorBlendState->attachmentCount == 0 &&
        create_info.pColorBlendState->pAttachments != nullptr) {
        AddIssue(report, PipelinePolicyIssue::ZeroBlendCountHasPointer);
    }

    for (u32 stage = 0; create_info.pStages && stage < create_info.stageCount; ++stage) {
        if (BadSpecializationInfo(create_info.pStages[stage].pSpecializationInfo)) {
            AddIssue(report, PipelinePolicyIssue::InvalidSpecializationInfo);
        }
    }

    if (!policy.xclipse.detected) {
        return report;
    }

    if (create_info.renderPass != VK_NULL_HANDLE && create_info.pNext) {
        bool hit_limit = false;
        const bool has_rendering = PNextContains(
            create_info.pNext,
            [](const VkBaseInStructure* node) {
                return node->sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            },
            hit_limit);
        if (has_rendering) {
            AddIssue(report, PipelinePolicyIssue::LegacyRenderPassHasDynamicRendering);
        }
        if (hit_limit) {
            AddIssue(report, PipelinePolicyIssue::PNextTraversalLimit);
        }
    }

    for (u32 stage = 0; create_info.pStages && stage < create_info.stageCount; ++stage) {
        auto* current = static_cast<const VkBaseInStructure*>(create_info.pStages[stage].pNext);
        bool found_required_subgroup = false;
        u32 depth = 0;
        for (; current && depth < 32; ++depth) {
            if (current->sType ==
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT) {
                const auto* required =
                    reinterpret_cast<const VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT*>(
                        current);
                found_required_subgroup = true;
                if (policy.capabilities.required_subgroup_size != CapabilityState::Validated) {
                    AddIssue(report, PipelinePolicyIssue::RequiredSubgroupUnvalidated);
                }
                const u32 min_size = policy.capabilities.min_subgroup_size;
                const u32 max_size = policy.capabilities.max_subgroup_size;
                if (min_size != 0 &&
                    (required->requiredSubgroupSize < min_size ||
                     required->requiredSubgroupSize > max_size)) {
                    AddIssue(report, PipelinePolicyIssue::RequiredSubgroupOutOfRange);
                }
                break;
            }
            current = current->pNext;
        }
        if (!found_required_subgroup && current != nullptr && depth == 32) {
            AddIssue(report, PipelinePolicyIssue::PNextTraversalLimit);
        }
    }

    return report;
}

} // namespace Vulkan
