// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "video_core/vulkan_common/vulkan_device_profile.h"

namespace Vulkan {

class Device;

/// Runs fail-closed startup micro-probes that collect Xclipse/SOC-specific optimization
/// variables without changing any production renderer policy. Results are stored in the
/// device policy for later engineering decisions and diagnostics.
void RunXclipseOptimizationProbeSuite(const Device& device,
                                      XclipseOptimizationProbeResults& results);

} // namespace Vulkan
