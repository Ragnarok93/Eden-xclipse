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

/// Executes Eden's production BC4/BC5 decoder shaders against storage images and validates
/// readback. The result is fail-closed and is suitable for enabling the RGTC GPU decode path.
[[nodiscard]] bool RunXclipseRgtcDecodeValidationProbe(const Device& device);

} // namespace Vulkan
