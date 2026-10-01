// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/renderer_vulkan/vk_sync_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

VkPipelineStageFlags SelectUploadBarrierDestinationStages(bool narrow_xclipse_path) noexcept {
    return narrow_xclipse_path ? vk::PIPELINE_STAGE_GRAPHICS_COMPUTE_TRANSFER
                               : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
}

} // namespace Vulkan
