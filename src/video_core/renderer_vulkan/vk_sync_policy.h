// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vulkan/vulkan.h>

namespace Vulkan {

[[nodiscard]] VkPipelineStageFlags SelectUploadBarrierDestinationStages(
    bool narrow_xclipse_path) noexcept;

} // namespace Vulkan
