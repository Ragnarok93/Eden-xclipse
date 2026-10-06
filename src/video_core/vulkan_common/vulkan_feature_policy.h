// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vulkan/vulkan_core.h>

namespace Vulkan {

[[nodiscard]] constexpr bool CanUseBufferDeviceAddress(
    uint32_t api_version, bool khr_extension, VkBool32 feature) noexcept {
    return (api_version >= VK_API_VERSION_1_2 || khr_extension) && feature != VK_FALSE;
}

[[nodiscard]] constexpr bool CanUseDynamicBlendState(
    bool extension, const VkPhysicalDeviceExtendedDynamicState3FeaturesEXT& features) noexcept {
    return extension && features.extendedDynamicState3ColorBlendEnable &&
           features.extendedDynamicState3ColorBlendEquation &&
           features.extendedDynamicState3ColorWriteMask;
}

[[nodiscard]] constexpr bool CanEnableAlphaToOne(bool base_feature, bool requested) noexcept {
    return base_feature && requested;
}

[[nodiscard]] constexpr bool CanUseDynamicAlphaToOne(
    bool extension, VkBool32 dynamic_feature, VkBool32 base_feature) noexcept {
    return extension && dynamic_feature != VK_FALSE && base_feature != VK_FALSE;
}

[[nodiscard]] constexpr VkDeviceSize SelectDescriptorSize(
    const VkPhysicalDeviceDescriptorBufferPropertiesEXT& props, VkDescriptorType type,
    bool robust_buffer_access) noexcept {
    switch (type) {
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        return robust_buffer_access ? props.robustUniformBufferDescriptorSize
                                    : props.uniformBufferDescriptorSize;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        return robust_buffer_access ? props.robustStorageBufferDescriptorSize
                                    : props.storageBufferDescriptorSize;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        return robust_buffer_access ? props.robustUniformTexelBufferDescriptorSize
                                    : props.uniformTexelBufferDescriptorSize;
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        return robust_buffer_access ? props.robustStorageTexelBufferDescriptorSize
                                    : props.storageTexelBufferDescriptorSize;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return props.combinedImageSamplerDescriptorSize;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        return props.storageImageDescriptorSize;
    default:
        return 0;
    }
}

[[nodiscard]] constexpr VkLineRasterizationModeEXT SelectLineRasterizationMode(
    bool smooth_requested, bool rectangular_supported, bool smooth_supported) noexcept {
    if (smooth_requested && smooth_supported) {
        return VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT;
    }
    return rectangular_supported ? VK_LINE_RASTERIZATION_MODE_RECTANGULAR_EXT
                                 : VK_LINE_RASTERIZATION_MODE_DEFAULT_EXT;
}

} // namespace Vulkan
