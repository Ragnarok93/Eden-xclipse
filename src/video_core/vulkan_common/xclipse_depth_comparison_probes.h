// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "video_core/vulkan_common/vulkan_device_profile.h"

namespace Vulkan {

class Device;

void RunXclipseDepthComparisonProbes(const Device& device,
                                     XclipseOptimizationProbeResults& results);

} // namespace Vulkan
