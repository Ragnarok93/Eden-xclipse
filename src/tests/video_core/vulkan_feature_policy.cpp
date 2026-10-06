// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "video_core/vulkan_common/vulkan_feature_policy.h"

// Fixture values extracted from vp_gpuinfo_samsung_sm_s731u_24_0_534_android_16_0.json.
// SHA256: e78e75a1e58b1aad40a6cc5af9bd5bab0cf0b75b2a167b8b952f9e96881202bc
// This report is a test fixture, never a runtime capability override.
namespace {
constexpr VkPhysicalDeviceExtendedDynamicState3FeaturesEXT S25FeEds3{
    .extendedDynamicState3TessellationDomainOrigin = VK_TRUE,
    .extendedDynamicState3DepthClampEnable = VK_TRUE,
    .extendedDynamicState3PolygonMode = VK_TRUE,
    .extendedDynamicState3RasterizationSamples = VK_TRUE,
    .extendedDynamicState3SampleMask = VK_TRUE,
    .extendedDynamicState3AlphaToCoverageEnable = VK_TRUE,
    .extendedDynamicState3AlphaToOneEnable = VK_FALSE,
    .extendedDynamicState3LogicOpEnable = VK_TRUE,
    .extendedDynamicState3ColorBlendEnable = VK_TRUE,
    .extendedDynamicState3ColorBlendEquation = VK_TRUE,
    .extendedDynamicState3ColorWriteMask = VK_TRUE,
    .extendedDynamicState3RasterizationStream = VK_FALSE,
    .extendedDynamicState3ConservativeRasterizationMode = VK_TRUE,
    .extendedDynamicState3ExtraPrimitiveOverestimationSize = VK_TRUE,
    .extendedDynamicState3DepthClipEnable = VK_TRUE,
    .extendedDynamicState3SampleLocationsEnable = VK_TRUE,
    .extendedDynamicState3ColorBlendAdvanced = VK_FALSE,
    .extendedDynamicState3ProvokingVertexMode = VK_TRUE,
    .extendedDynamicState3LineRasterizationMode = VK_TRUE,
    .extendedDynamicState3LineStippleEnable = VK_TRUE,
    .extendedDynamicState3DepthClipNegativeOneToOne = VK_TRUE,
    .extendedDynamicState3ViewportWScalingEnable = VK_FALSE,
    .extendedDynamicState3ViewportSwizzle = VK_FALSE,
    .extendedDynamicState3CoverageToColorEnable = VK_FALSE,
    .extendedDynamicState3CoverageToColorLocation = VK_FALSE,
    .extendedDynamicState3CoverageModulationMode = VK_FALSE,
    .extendedDynamicState3CoverageModulationTableEnable = VK_FALSE,
    .extendedDynamicState3CoverageModulationTable = VK_FALSE,
    .extendedDynamicState3CoverageReductionMode = VK_FALSE,
    .extendedDynamicState3RepresentativeFragmentTestEnable = VK_FALSE,
    .extendedDynamicState3ShadingRateImageEnable = VK_FALSE,
};
constexpr VkPhysicalDeviceLineRasterizationFeaturesEXT S25FeLines{
    .rectangularLines = VK_FALSE,
    .bresenhamLines = VK_TRUE,
    .smoothLines = VK_FALSE,
    .stippledRectangularLines = VK_FALSE,
    .stippledBresenhamLines = VK_TRUE,
    .stippledSmoothLines = VK_FALSE,
};
constexpr VkPhysicalDeviceVulkan12Features S25FeCore12{
    .samplerMirrorClampToEdge = VK_TRUE,
    .drawIndirectCount = VK_TRUE,
    .storageBuffer8BitAccess = VK_TRUE,
    .uniformAndStorageBuffer8BitAccess = VK_TRUE,
    .storagePushConstant8 = VK_FALSE,
    .shaderBufferInt64Atomics = VK_TRUE,
    .shaderSharedInt64Atomics = VK_TRUE,
    .shaderFloat16 = VK_TRUE,
    .shaderInt8 = VK_TRUE,
    .descriptorIndexing = VK_TRUE,
    .shaderInputAttachmentArrayDynamicIndexing = VK_TRUE,
    .shaderUniformTexelBufferArrayDynamicIndexing = VK_TRUE,
    .shaderStorageTexelBufferArrayDynamicIndexing = VK_TRUE,
    .shaderUniformBufferArrayNonUniformIndexing = VK_TRUE,
    .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
    .shaderStorageBufferArrayNonUniformIndexing = VK_TRUE,
    .shaderStorageImageArrayNonUniformIndexing = VK_TRUE,
    .shaderInputAttachmentArrayNonUniformIndexing = VK_TRUE,
    .shaderUniformTexelBufferArrayNonUniformIndexing = VK_TRUE,
    .shaderStorageTexelBufferArrayNonUniformIndexing = VK_TRUE,
    .descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE,
    .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
    .descriptorBindingStorageImageUpdateAfterBind = VK_TRUE,
    .descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE,
    .descriptorBindingUniformTexelBufferUpdateAfterBind = VK_TRUE,
    .descriptorBindingStorageTexelBufferUpdateAfterBind = VK_TRUE,
    .descriptorBindingUpdateUnusedWhilePending = VK_TRUE,
    .descriptorBindingPartiallyBound = VK_TRUE,
    .descriptorBindingVariableDescriptorCount = VK_TRUE,
    .runtimeDescriptorArray = VK_TRUE,
    .samplerFilterMinmax = VK_FALSE,
    .scalarBlockLayout = VK_TRUE,
    .imagelessFramebuffer = VK_TRUE,
    .uniformBufferStandardLayout = VK_TRUE,
    .shaderSubgroupExtendedTypes = VK_TRUE,
    .separateDepthStencilLayouts = VK_TRUE,
    .hostQueryReset = VK_TRUE,
    .timelineSemaphore = VK_TRUE,
    .bufferDeviceAddress = VK_TRUE,
    .bufferDeviceAddressCaptureReplay = VK_TRUE,
    .bufferDeviceAddressMultiDevice = VK_FALSE,
    .vulkanMemoryModel = VK_TRUE,
    .vulkanMemoryModelDeviceScope = VK_TRUE,
    .vulkanMemoryModelAvailabilityVisibilityChains = VK_FALSE,
    .shaderOutputViewportIndex = VK_TRUE,
    .shaderOutputLayer = VK_TRUE,
    .subgroupBroadcastDynamicId = VK_TRUE,
};
} // namespace

TEST_CASE("S25FE: core BDA requires the queried feature, not an extension name", "[video_core]") {
    REQUIRE(Vulkan::CanUseBufferDeviceAddress(VK_API_VERSION_1_3, false,
                                               S25FeCore12.bufferDeviceAddress));
    REQUIRE_FALSE(Vulkan::CanUseBufferDeviceAddress(VK_API_VERSION_1_3, true, VK_FALSE));
    REQUIRE_FALSE(Vulkan::CanUseBufferDeviceAddress(VK_API_VERSION_1_1, false, VK_TRUE));
    REQUIRE(Vulkan::CanUseBufferDeviceAddress(VK_API_VERSION_1_1, true, VK_TRUE));
    REQUIRE_FALSE(S25FeCore12.samplerFilterMinmax);
}

TEST_CASE("S25FE: Samsung blend mask overrides advertised EDS3 support", "[video_core]") {
    auto enabled = S25FeEds3;
    REQUIRE(Vulkan::CanUseDynamicBlendState(true, enabled));
    enabled.extendedDynamicState3ColorBlendEnable = VK_FALSE;
    enabled.extendedDynamicState3ColorBlendEquation = VK_FALSE;
    REQUIRE_FALSE(Vulkan::CanUseDynamicBlendState(true, enabled));
    REQUIRE_FALSE(Vulkan::CanUseDynamicBlendState(false, S25FeEds3));
    REQUIRE_FALSE(S25FeEds3.extendedDynamicState3AlphaToOneEnable);
}

TEST_CASE("S25FE: advertised line extension cannot imply rectangular or smooth lines", "[video_core]") {
    REQUIRE_FALSE(S25FeLines.rectangularLines);
    REQUIRE_FALSE(S25FeLines.smoothLines);
    for (bool smooth_requested : {false, true}) {
        REQUIRE(Vulkan::SelectLineRasterizationMode(
                    smooth_requested, S25FeLines.rectangularLines, S25FeLines.smoothLines) ==
                VK_LINE_RASTERIZATION_MODE_DEFAULT_EXT);
    }
    REQUIRE(Vulkan::SelectLineRasterizationMode(false, true, false) ==
            VK_LINE_RASTERIZATION_MODE_RECTANGULAR_EXT);
    REQUIRE(Vulkan::SelectLineRasterizationMode(true, true, false) ==
            VK_LINE_RASTERIZATION_MODE_RECTANGULAR_EXT);
    REQUIRE(Vulkan::SelectLineRasterizationMode(true, false, true) ==
            VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT);
}
