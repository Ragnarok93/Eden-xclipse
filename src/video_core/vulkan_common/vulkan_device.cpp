// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <bitset>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <span>
#include <thread>
#include "common/container/unordered_map.h"
#include "common/container/unordered_set.h"
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "common/assert.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/literals.h"
#include <ranges>
#include "common/settings.h"
#include "common/settings_enums.h"
#include "video_core/vulkan_common/nsight_aftermath_tracker.h"
#include "video_core/vulkan_common/vma.h"
#include "video_core/host_shaders/xclipse_subgroup_probe_comp_spv.h"
#include "video_core/host_shaders/xclipse_subgroup_op_probe_ballot_comp_spv.h"
#include "video_core/host_shaders/xclipse_subgroup_op_probe_shuffle_comp_spv.h"
#include "video_core/host_shaders/xclipse_subgroup_op_probe_arithmetic_comp_spv.h"
#include "video_core/host_shaders/xclipse_subgroup_op_probe_quad_comp_spv.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include "video_core/gpu_logging/gpu_logging.h"

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

#if defined(__ANDROID__) && defined(ARCHITECTURE_arm64)
#include <adrenotools/bcenabler.h>
#include <android/api-level.h>
#endif

namespace Vulkan {
using namespace Common::Literals;
namespace {
namespace Alternatives {
constexpr std::array STENCIL8_UINT{
    VK_FORMAT_D16_UNORM_S8_UINT,
    VK_FORMAT_D24_UNORM_S8_UINT,
    VK_FORMAT_D32_SFLOAT_S8_UINT,
    VK_FORMAT_UNDEFINED,
};

constexpr std::array DEPTH24_UNORM_STENCIL8_UINT{
    VK_FORMAT_D32_SFLOAT_S8_UINT,
    VK_FORMAT_D16_UNORM_S8_UINT,
    VK_FORMAT_UNDEFINED,
};

constexpr std::array DEPTH24_UNORM_DONTCARE8{
    VK_FORMAT_D32_SFLOAT,
    VK_FORMAT_D16_UNORM,
    VK_FORMAT_UNDEFINED,
};

constexpr std::array DEPTH16_UNORM_STENCIL8_UINT{
    VK_FORMAT_D24_UNORM_S8_UINT,
    VK_FORMAT_D32_SFLOAT_S8_UINT,
    VK_FORMAT_UNDEFINED,
};

constexpr std::array B5G6R5_UNORM_PACK16{
    VK_FORMAT_R5G6B5_UNORM_PACK16,
    VK_FORMAT_UNDEFINED,
};

constexpr std::array R4G4_UNORM_PACK8{
    VK_FORMAT_R8_UNORM,
    VK_FORMAT_UNDEFINED,
};

constexpr std::array R16G16B16_SFLOAT{
    VK_FORMAT_R16G16B16A16_SFLOAT,
    VK_FORMAT_UNDEFINED,
};

constexpr std::array R16G16B16_SSCALED{
    VK_FORMAT_R16G16B16A16_SSCALED,
    VK_FORMAT_UNDEFINED,
};

constexpr std::array R8G8B8_SSCALED{
    VK_FORMAT_R8G8B8A8_SSCALED,
    VK_FORMAT_UNDEFINED,
};

constexpr std::array VK_FORMAT_R32G32B32_SFLOAT{
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_UNDEFINED,
};

constexpr std::array VK_FORMAT_A4B4G4R4_UNORM_PACK16{
    VK_FORMAT_R4G4B4A4_UNORM_PACK16,
    VK_FORMAT_UNDEFINED,
};

constexpr std::array B10G11R11_UFLOAT_PACK32{
    VK_FORMAT_R16G16B16A16_SFLOAT,
    VK_FORMAT_A8B8G8R8_SRGB_PACK32,
    VK_FORMAT_UNDEFINED,
};

} // namespace Alternatives

template <typename T>
void SetNext(void**& next, T& data) {
    *next = &data;
    next = &data.pNext;
}

constexpr const VkFormat* GetFormatAlternatives(VkFormat format) {
    switch (format) {
    case VK_FORMAT_S8_UINT:
        return Alternatives::STENCIL8_UINT.data();
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return Alternatives::DEPTH24_UNORM_STENCIL8_UINT.data();
    case VK_FORMAT_X8_D24_UNORM_PACK32:
        return Alternatives::DEPTH24_UNORM_DONTCARE8.data();
    case VK_FORMAT_D16_UNORM_S8_UINT:
        return Alternatives::DEPTH16_UNORM_STENCIL8_UINT.data();
    case VK_FORMAT_B5G6R5_UNORM_PACK16:
        return Alternatives::B5G6R5_UNORM_PACK16.data();
    case VK_FORMAT_R4G4_UNORM_PACK8:
        return Alternatives::R4G4_UNORM_PACK8.data();
    case VK_FORMAT_R16G16B16_SFLOAT:
        return Alternatives::R16G16B16_SFLOAT.data();
    case VK_FORMAT_R16G16B16_SSCALED:
        return Alternatives::R16G16B16_SSCALED.data();
    case VK_FORMAT_R8G8B8_SSCALED:
        return Alternatives::R8G8B8_SSCALED.data();
    case VK_FORMAT_R32G32B32_SFLOAT:
        return Alternatives::VK_FORMAT_R32G32B32_SFLOAT.data();
    case VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT:
        return Alternatives::VK_FORMAT_A4B4G4R4_UNORM_PACK16.data();
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        return Alternatives::B10G11R11_UFLOAT_PACK32.data();
    default:
        return nullptr;
    }
}

VkFormatFeatureFlags GetFormatFeatures(VkFormatProperties properties, FormatType format_type) {
    switch (format_type) {
    case FormatType::Linear:
        return properties.linearTilingFeatures;
    case FormatType::Optimal:
        return properties.optimalTilingFeatures;
    case FormatType::Buffer:
        return properties.bufferFeatures;
    default:
        return {};
    }
}

constexpr std::array<VkFormat, BcnFormatCount> BCN_FORMATS{
    VK_FORMAT_BC1_RGB_UNORM_BLOCK,
    VK_FORMAT_BC1_RGB_SRGB_BLOCK,
    VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
    VK_FORMAT_BC1_RGBA_SRGB_BLOCK,
    VK_FORMAT_BC2_UNORM_BLOCK,
    VK_FORMAT_BC2_SRGB_BLOCK,
    VK_FORMAT_BC3_UNORM_BLOCK,
    VK_FORMAT_BC3_SRGB_BLOCK,
    VK_FORMAT_BC4_UNORM_BLOCK,
    VK_FORMAT_BC4_SNORM_BLOCK,
    VK_FORMAT_BC5_UNORM_BLOCK,
    VK_FORMAT_BC5_SNORM_BLOCK,
    VK_FORMAT_BC6H_UFLOAT_BLOCK,
    VK_FORMAT_BC6H_SFLOAT_BLOCK,
    VK_FORMAT_BC7_UNORM_BLOCK,
    VK_FORMAT_BC7_SRGB_BLOCK,
};

CapabilityState Advertised(bool available) {
    return available ? CapabilityState::Advertised : CapabilityState::Unsupported;
}

FormatCapabilitySnapshot CaptureOptimalFormatCapabilities(VkFormatProperties properties) {
    const VkFormatFeatureFlags flags = properties.optimalTilingFeatures;
    const auto has = [flags](VkFormatFeatureFlagBits feature) {
        return Advertised((flags & feature) == feature);
    };
    const bool sampled = (flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
    const bool transfer_src = (flags & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) != 0;
    const bool transfer_dst = (flags & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0;
    return {
        .image_create = Advertised(sampled && transfer_src && transfer_dst),
        .sampled = has(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT),
        .linear_filter = has(VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT),
        .storage_image = has(VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT),
        .transfer_src = has(VK_FORMAT_FEATURE_TRANSFER_SRC_BIT),
        .transfer_dst = has(VK_FORMAT_FEATURE_TRANSFER_DST_BIT),
        .blit_src = has(VK_FORMAT_FEATURE_BLIT_SRC_BIT),
        .blit_dst = has(VK_FORMAT_FEATURE_BLIT_DST_BIT),
    };
}

bool SupportsAdvertisedNativeBcnPath(const FormatCapabilitySnapshot& format) {
    return format.image_create != CapabilityState::Unsupported &&
           format.sampled != CapabilityState::Unsupported &&
           format.linear_filter != CapabilityState::Unsupported &&
           format.transfer_src != CapabilityState::Unsupported &&
           format.transfer_dst != CapabilityState::Unsupported;
}

bool SupportsValidatedNativeBcnPath(const FormatCapabilitySnapshot& format) {
    return format.image_create == CapabilityState::Validated &&
           format.sampled == CapabilityState::Validated &&
           format.linear_filter == CapabilityState::Validated &&
           format.transfer_src == CapabilityState::Validated &&
           format.transfer_dst == CapabilityState::Validated;
}

bool HasValidatedImageCreation(const FormatCapabilitySnapshot& format) {
    return format.image_create == CapabilityState::Validated &&
           SupportsAdvertisedNativeBcnPath(format);
}

bool IsXclipseBasicNativeBcFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_BC2_UNORM_BLOCK:
    case VK_FORMAT_BC2_SRGB_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:
    case VK_FORMAT_BC3_SRGB_BLOCK:
        return true;
    default:
        return false;
    }
}

bool SupportsXclipseRuntimeNativeBcnPath(VkFormat format,
                                         const FormatCapabilitySnapshot& capability) {
    // Samsung's Xclipse Vulkan format table exposes BC1-BC3 as the basic native family.
    // For those formats a successful image-create probe plus the advertised sampled/filter/
    // transfer usage is enough to retain the native path. BC4-BC7 stay on emulation until their
    // actual operations are validated; we never synthesize missing format properties.
    if (IsXclipseBasicNativeBcFormat(format)) {
        return HasValidatedImageCreation(capability);
    }
    return SupportsValidatedNativeBcnPath(capability);
}

::Common::unordered_map<VkFormat, VkFormatProperties> GetFormatProperties(vk::PhysicalDevice physical) {
    static constexpr std::array formats{
        VK_FORMAT_A1R5G5B5_UNORM_PACK16,
        VK_FORMAT_A2B10G10R10_SINT_PACK32,
        VK_FORMAT_A2B10G10R10_SNORM_PACK32,
        VK_FORMAT_A2B10G10R10_SSCALED_PACK32,
        VK_FORMAT_A2B10G10R10_UINT_PACK32,
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_FORMAT_A2B10G10R10_USCALED_PACK32,
        VK_FORMAT_A2R10G10B10_UNORM_PACK32,
        VK_FORMAT_A8B8G8R8_SINT_PACK32,
        VK_FORMAT_A8B8G8R8_SNORM_PACK32,
        VK_FORMAT_A8B8G8R8_SRGB_PACK32,
        VK_FORMAT_A8B8G8R8_UINT_PACK32,
        VK_FORMAT_A8B8G8R8_UNORM_PACK32,
        VK_FORMAT_ASTC_10x10_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x10_UNORM_BLOCK,
        VK_FORMAT_ASTC_10x5_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x5_UNORM_BLOCK,
        VK_FORMAT_ASTC_10x6_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x6_UNORM_BLOCK,
        VK_FORMAT_ASTC_10x8_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x8_UNORM_BLOCK,
        VK_FORMAT_ASTC_12x10_SRGB_BLOCK,
        VK_FORMAT_ASTC_12x10_UNORM_BLOCK,
        VK_FORMAT_ASTC_12x12_SRGB_BLOCK,
        VK_FORMAT_ASTC_12x12_UNORM_BLOCK,
        VK_FORMAT_ASTC_4x4_SRGB_BLOCK,
        VK_FORMAT_ASTC_4x4_UNORM_BLOCK,
        VK_FORMAT_ASTC_5x4_SRGB_BLOCK,
        VK_FORMAT_ASTC_5x4_UNORM_BLOCK,
        VK_FORMAT_ASTC_5x5_SRGB_BLOCK,
        VK_FORMAT_ASTC_5x5_UNORM_BLOCK,
        VK_FORMAT_ASTC_6x5_SRGB_BLOCK,
        VK_FORMAT_ASTC_6x5_UNORM_BLOCK,
        VK_FORMAT_ASTC_6x6_SRGB_BLOCK,
        VK_FORMAT_ASTC_6x6_UNORM_BLOCK,
        VK_FORMAT_ASTC_8x5_SRGB_BLOCK,
        VK_FORMAT_ASTC_8x5_UNORM_BLOCK,
        VK_FORMAT_ASTC_8x6_SRGB_BLOCK,
        VK_FORMAT_ASTC_8x6_UNORM_BLOCK,
        VK_FORMAT_ASTC_8x8_SRGB_BLOCK,
        VK_FORMAT_ASTC_8x8_UNORM_BLOCK,
        VK_FORMAT_B10G11R11_UFLOAT_PACK32,
        VK_FORMAT_B4G4R4A4_UNORM_PACK16,
        VK_FORMAT_B5G5R5A1_UNORM_PACK16,
        VK_FORMAT_B5G6R5_UNORM_PACK16,
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_BC1_RGBA_SRGB_BLOCK,
        VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
        VK_FORMAT_BC2_SRGB_BLOCK,
        VK_FORMAT_BC2_UNORM_BLOCK,
        VK_FORMAT_BC3_SRGB_BLOCK,
        VK_FORMAT_BC3_UNORM_BLOCK,
        VK_FORMAT_BC4_SNORM_BLOCK,
        VK_FORMAT_BC4_UNORM_BLOCK,
        VK_FORMAT_BC5_SNORM_BLOCK,
        VK_FORMAT_BC5_UNORM_BLOCK,
        VK_FORMAT_BC6H_SFLOAT_BLOCK,
        VK_FORMAT_BC6H_UFLOAT_BLOCK,
        VK_FORMAT_BC7_SRGB_BLOCK,
        VK_FORMAT_BC7_UNORM_BLOCK,
        VK_FORMAT_D16_UNORM,
        VK_FORMAT_D16_UNORM_S8_UINT,
        VK_FORMAT_X8_D24_UNORM_PACK32,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R16G16B16A16_SINT,
        VK_FORMAT_R16G16B16A16_SNORM,
        VK_FORMAT_R16G16B16A16_SSCALED,
        VK_FORMAT_R16G16B16A16_UINT,
        VK_FORMAT_R16G16B16A16_UNORM,
        VK_FORMAT_R16G16B16A16_USCALED,
        VK_FORMAT_R16G16B16_SFLOAT,
        VK_FORMAT_R16G16B16_SINT,
        VK_FORMAT_R16G16B16_SNORM,
        VK_FORMAT_R16G16B16_SSCALED,
        VK_FORMAT_R16G16B16_UINT,
        VK_FORMAT_R16G16B16_UNORM,
        VK_FORMAT_R16G16B16_USCALED,
        VK_FORMAT_R16G16_SFLOAT,
        VK_FORMAT_R16G16_SINT,
        VK_FORMAT_R16G16_SNORM,
        VK_FORMAT_R16G16_SSCALED,
        VK_FORMAT_R16G16_UINT,
        VK_FORMAT_R16G16_UNORM,
        VK_FORMAT_R16G16_USCALED,
        VK_FORMAT_R16_SFLOAT,
        VK_FORMAT_R16_SINT,
        VK_FORMAT_R16_SNORM,
        VK_FORMAT_R16_SSCALED,
        VK_FORMAT_R16_UINT,
        VK_FORMAT_R16_UNORM,
        VK_FORMAT_R16_USCALED,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_FORMAT_R32G32B32A32_SINT,
        VK_FORMAT_R32G32B32A32_UINT,
        VK_FORMAT_R32G32B32_SFLOAT,
        VK_FORMAT_R32G32B32_SINT,
        VK_FORMAT_R32G32B32_UINT,
        VK_FORMAT_R32G32_SFLOAT,
        VK_FORMAT_R32G32_SINT,
        VK_FORMAT_R32G32_UINT,
        VK_FORMAT_R32_SFLOAT,
        VK_FORMAT_R32_SINT,
        VK_FORMAT_R32_UINT,
        VK_FORMAT_R4G4B4A4_UNORM_PACK16,
        VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT,
        VK_FORMAT_R4G4_UNORM_PACK8,
        VK_FORMAT_R5G5B5A1_UNORM_PACK16,
        VK_FORMAT_R5G6B5_UNORM_PACK16,
        VK_FORMAT_R8G8B8A8_SINT,
        VK_FORMAT_R8G8B8A8_SNORM,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_R8G8B8A8_SSCALED,
        VK_FORMAT_R8G8B8A8_UINT,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R8G8B8A8_USCALED,
        VK_FORMAT_R8G8B8_SINT,
        VK_FORMAT_R8G8B8_SNORM,
        VK_FORMAT_R8G8B8_SSCALED,
        VK_FORMAT_R8G8B8_UINT,
        VK_FORMAT_R8G8B8_UNORM,
        VK_FORMAT_R8G8B8_USCALED,
        VK_FORMAT_R8G8_SINT,
        VK_FORMAT_R8G8_SNORM,
        VK_FORMAT_R8G8_SSCALED,
        VK_FORMAT_R8G8_UINT,
        VK_FORMAT_R8G8_UNORM,
        VK_FORMAT_R8G8_USCALED,
        VK_FORMAT_R8_SINT,
        VK_FORMAT_R8_SNORM,
        VK_FORMAT_R8_SSCALED,
        VK_FORMAT_R8_UINT,
        VK_FORMAT_R8_UNORM,
        VK_FORMAT_R8_USCALED,
        VK_FORMAT_S8_UINT,
        VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,
        VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK,
        VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK,
        VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK,
        VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK,
        VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK,
        VK_FORMAT_EAC_R11_UNORM_BLOCK,
        VK_FORMAT_EAC_R11_SNORM_BLOCK,
        VK_FORMAT_EAC_R11G11_UNORM_BLOCK,
        VK_FORMAT_EAC_R11G11_SNORM_BLOCK,
    };
    ::Common::unordered_map<VkFormat, VkFormatProperties> format_properties;
    for (const auto format : formats) {
        format_properties.emplace(format, physical.GetFormatProperties(format));
    }
    return format_properties;
}

#if defined(__ANDROID__) && defined(ARCHITECTURE_arm64)
void OverrideBcnFormats(::Common::unordered_map<VkFormat, VkFormatProperties>& format_properties) {
    // These properties are extracted from Adreno driver 512.687.0
    constexpr VkFormatFeatureFlags tiling_features{VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                   VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                                   VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                                                   VK_FORMAT_FEATURE_TRANSFER_DST_BIT};

    constexpr VkFormatFeatureFlags buffer_features{VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT};

    static constexpr std::array bcn_formats{
        VK_FORMAT_BC1_RGBA_SRGB_BLOCK, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_BC2_SRGB_BLOCK,
        VK_FORMAT_BC2_UNORM_BLOCK,     VK_FORMAT_BC3_SRGB_BLOCK,       VK_FORMAT_BC3_UNORM_BLOCK,
        VK_FORMAT_BC4_SNORM_BLOCK,     VK_FORMAT_BC4_UNORM_BLOCK,      VK_FORMAT_BC5_SNORM_BLOCK,
        VK_FORMAT_BC5_UNORM_BLOCK,     VK_FORMAT_BC6H_SFLOAT_BLOCK,    VK_FORMAT_BC6H_UFLOAT_BLOCK,
        VK_FORMAT_BC7_SRGB_BLOCK,      VK_FORMAT_BC7_UNORM_BLOCK,
    };

    for (const auto format : bcn_formats) {
        format_properties[format].linearTilingFeatures = tiling_features;
        format_properties[format].optimalTilingFeatures = tiling_features;
        format_properties[format].bufferFeatures = buffer_features;
    }
}
#endif

NvidiaArchitecture GetNvidiaArchitecture(vk::PhysicalDevice physical,
                                        const std::set<std::string, std::less<>>& exts) {
    VkPhysicalDeviceProperties2 physical_properties{};
    physical_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    physical_properties.pNext = nullptr;

    if (exts.contains(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME)) {
        VkPhysicalDeviceFragmentShadingRatePropertiesKHR shading_rate_props{};
        shading_rate_props.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR;
        physical_properties.pNext = &shading_rate_props;
        physical.GetProperties2(physical_properties);
        if (shading_rate_props.primitiveFragmentShadingRateWithMultipleViewports) {
            return NvidiaArchitecture::Arch_AmpereOrNewer;
        }
        return NvidiaArchitecture::Arch_Turing;
    }

    if (exts.contains(VK_EXT_BLEND_OPERATION_ADVANCED_EXTENSION_NAME)) {
        VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT advanced_blending_props{};
        advanced_blending_props.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_PROPERTIES_EXT;
        physical_properties.pNext = &advanced_blending_props;
        physical.GetProperties2(physical_properties);
        if (advanced_blending_props.advancedBlendMaxColorAttachments == 1) {
            return NvidiaArchitecture::Arch_Maxwell;
        }

        if (exts.contains(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME)) {
            VkPhysicalDeviceConservativeRasterizationPropertiesEXT conservative_raster_props{};
            conservative_raster_props.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT;
            physical_properties.pNext = &conservative_raster_props;
            physical.GetProperties2(physical_properties);
            if (conservative_raster_props.degenerateLinesRasterized) {
                return NvidiaArchitecture::Arch_Volta;
            }
            return NvidiaArchitecture::Arch_Pascal;
        }
    }

    return NvidiaArchitecture::Arch_KeplerOrOlder;
}

std::vector<const char*> ExtensionListForVulkan(
    const std::set<std::string, std::less<>>& extensions) {
    std::vector<const char*> output;
    output.reserve(extensions.size());
    for (const auto& extension : extensions) {
        output.push_back(extension.c_str());
    }
    return output;
}

constexpr std::array<char, 8> STATIC_CACHE_MAGIC_NUMBER{'e', 'd', 'e', 'n', 's', 't', 'p', 'c'};
constexpr u32 STATIC_CACHE_VERSION = 2;

std::filesystem::path StaticPipelineCacheFilename() {
    const auto shader_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir);
    if (!Common::FS::CreateDir(shader_dir)) {
        return {};
    }
    return shader_dir / "vulkan_static_pipelines.bin";
}

} // Anonymous namespace

void Device::RemoveExtension(bool& extension, const std::string& extension_name) {
    extension = false;
    loaded_extensions.erase(extension_name);
}

void Device::RemoveExtensionIfUnsuitable(bool is_suitable, const std::string& extension_name) {
    if (loaded_extensions.contains(extension_name) && !is_suitable) {
        LOG_WARNING(Render_Vulkan, "Removing unsuitable extension {}", extension_name);
        this->RemoveExtension(is_suitable, extension_name);
    }
}

template <typename Feature>
void Device::RemoveExtensionFeature(bool& extension, Feature& feature,
                                    const std::string& extension_name) {
    // Unload extension.
    this->RemoveExtension(extension, extension_name);

    // Save sType and pNext for chain.
    VkStructureType sType = feature.sType;
    void* pNext = feature.pNext;

    // Clear feature struct and restore chain.
    feature = {};
    feature.sType = sType;
    feature.pNext = pNext;
}

template <typename Feature>
void Device::RemoveExtensionFeatureIfUnsuitable(bool is_suitable, Feature& feature,
                                                const std::string& extension_name) {
    if (loaded_extensions.contains(extension_name) && !is_suitable) {
        LOG_WARNING(Render_Vulkan, "Removing features for unsuitable extension {}", extension_name);
        this->RemoveExtensionFeature(is_suitable, feature, extension_name);
    }
}

void Device::BuildDevicePolicy() {
    auto& identity = device_policy.identity;
    identity.device_name = properties.properties.deviceName;
    identity.driver_name = properties.driver.driverName;
    identity.vendor_id = properties.properties.vendorID;
    identity.device_id = properties.properties.deviceID;
    identity.driver_id = static_cast<std::uint32_t>(properties.driver.driverID);
    identity.driver_version = properties.properties.driverVersion;
    std::copy_n(properties.properties.pipelineCacheUUID, identity.pipeline_cache_uuid.size(),
                identity.pipeline_cache_uuid.begin());

#if defined(__ANDROID__)
    char soc_model[PROP_VALUE_MAX]{};
    if (__system_property_get("ro.soc.model", soc_model) > 0) {
        identity.soc_model = soc_model;
    }
#endif

    auto& caps = device_policy.capabilities;
    caps.timeline = Advertised(features.timeline_semaphore.timelineSemaphore != VK_FALSE);
    caps.synchronization2 = Advertised(features.synchronization2.synchronization2 != VK_FALSE);
    caps.descriptor_buffer =
        Advertised(extensions.descriptor_buffer &&
                   features.descriptor_buffer.descriptorBuffer != VK_FALSE);
    caps.sparse_binding =
        Advertised(features.features.sparseBinding != VK_FALSE && graphics_family_sparse_binding);

    const VkSubgroupFeatureFlags subgroup_ops = properties.subgroup_properties.supportedOperations;
    caps.subgroup_ballot =
        Advertised((subgroup_ops & VK_SUBGROUP_FEATURE_BALLOT_BIT) != 0);
    caps.subgroup_shuffle =
        Advertised((subgroup_ops & VK_SUBGROUP_FEATURE_SHUFFLE_BIT) != 0);
    caps.subgroup_arithmetic =
        Advertised((subgroup_ops & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) != 0);
    caps.subgroup_quad =
        Advertised((subgroup_ops & VK_SUBGROUP_FEATURE_QUAD_BIT) != 0);
    caps.required_subgroup_size =
        Advertised(features.subgroup_size_control.subgroupSizeControl != VK_FALSE &&
                   properties.subgroup_size_control.requiredSubgroupSizeStages != 0);

    caps.subgroup_size = properties.subgroup_properties.subgroupSize;
    caps.subgroup_supported_stages = properties.subgroup_properties.supportedStages;
    caps.subgroup_supported_operations = properties.subgroup_properties.supportedOperations;
    caps.min_subgroup_size = properties.subgroup_size_control.minSubgroupSize;
    caps.max_subgroup_size = properties.subgroup_size_control.maxSubgroupSize;
    caps.required_subgroup_size_stages =
        properties.subgroup_size_control.requiredSubgroupSizeStages;

    for (std::size_t index = 0; index < BCN_FORMATS.size(); ++index) {
        caps.bcn[index] = CaptureOptimalFormatCapabilities(
            physical.GetFormatProperties(BCN_FORMATS[index]));
    }

    device_policy.xclipse = DetectXclipseHardware(identity);
    device_policy.use_xclipse_sync_policy =
        device_policy.xclipse.detected && Settings::values.xclipse_sync_policy.GetValue();
    UpdateXclipseBcnProfile();
    device_policy.policy_hash = ComputeVulkanPolicyHash(device_policy);
}

void Device::UpdateXclipseBcnProfile() {
    auto& xclipse = device_policy.xclipse;
    if (!xclipse.detected) {
        return;
    }
    const auto& caps = device_policy.capabilities;
    const auto native = [&caps](std::initializer_list<BcnFormat> formats) {
        return std::ranges::all_of(formats, [&caps](BcnFormat format) {
            const std::size_t index = static_cast<std::size_t>(format);
            return SupportsXclipseRuntimeNativeBcnPath(BCN_FORMATS[index], caps.bcn[index]);
        });
    };
    xclipse.bc1_native =
        native({BcnFormat::BC1_RGB_UNORM, BcnFormat::BC1_RGB_SRGB,
                BcnFormat::BC1_RGBA_UNORM, BcnFormat::BC1_RGBA_SRGB});
    xclipse.bc2_native = native({BcnFormat::BC2_UNORM, BcnFormat::BC2_SRGB});
    xclipse.bc3_native = native({BcnFormat::BC3_UNORM, BcnFormat::BC3_SRGB});
    xclipse.bc4_native = native({BcnFormat::BC4_UNORM, BcnFormat::BC4_SNORM});
    xclipse.bc5_native = native({BcnFormat::BC5_UNORM, BcnFormat::BC5_SNORM});
    xclipse.bc6_native = native({BcnFormat::BC6H_UFLOAT, BcnFormat::BC6H_SFLOAT});
    xclipse.bc7_native = native({BcnFormat::BC7_UNORM, BcnFormat::BC7_SRGB});
}

void Device::RunXclipseSubgroupValidationProbes() {
    auto& caps = device_policy.capabilities;
    auto& xclipse = device_policy.xclipse;

    const bool compute_stage_advertised =
        (caps.subgroup_supported_stages & VK_SHADER_STAGE_COMPUTE_BIT) != 0;
    const bool subgroup_basic_advertised =
        (caps.subgroup_supported_operations & VK_SUBGROUP_FEATURE_BASIC_BIT) != 0;
    const bool required_size_supported =
        caps.required_subgroup_size != CapabilityState::Unsupported &&
        (caps.required_subgroup_size_stages & VK_SHADER_STAGE_COMPUTE_BIT) != 0;
    if (!required_size_supported) {
        return;
    }
    if (!compute_stage_advertised || !subgroup_basic_advertised) {
        LOG_INFO(Render_Vulkan,
                 "XCLIPSE PROBE subgroup properties are internally inconsistent "
                 "(stages=0x{:x} ops=0x{:x}); attempting bounded execution validation",
                 caps.subgroup_supported_stages, caps.subgroup_supported_operations);
    }

    constexpr u32 ProbeInvocations = 64;
    constexpr u32 MaxProbeWordsPerInvocation = 3;
    constexpr VkDeviceSize ProbeBytes =
        sizeof(u32) * ProbeInvocations * MaxProbeWordsPerInvocation;
    constexpr u64 ProbeTimeoutNs = 1'000'000'000ULL;

    VkBuffer raw_buffer = VK_NULL_HANDLE;
    const VkBufferCreateInfo buffer_ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = ProbeBytes,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    vk::Check(dld.vkCreateBuffer(*logical, &buffer_ci, nullptr, &raw_buffer));
    vk::Handle<VkBuffer, VkDevice, vk::DeviceDispatch> probe_buffer{raw_buffer, *logical, dld};

    const VkMemoryRequirements requirements =
        logical.GetBufferMemoryRequirements(*probe_buffer);
    const VkPhysicalDeviceMemoryProperties memory_properties =
        physical.GetMemoryProperties().memoryProperties;

    u32 memory_type = VK_MAX_MEMORY_TYPES;
    constexpr VkMemoryPropertyFlags RequiredMemoryFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (u32 index = 0; index < memory_properties.memoryTypeCount; ++index) {
        const bool compatible = (requirements.memoryTypeBits & (1U << index)) != 0;
        const VkMemoryPropertyFlags flags = memory_properties.memoryTypes[index].propertyFlags;
        if (compatible && (flags & RequiredMemoryFlags) == RequiredMemoryFlags) {
            memory_type = index;
            break;
        }
    }
    if (memory_type == VK_MAX_MEMORY_TYPES) {
        LOG_WARNING(Render_Vulkan,
                    "XCLIPSE PROBE subgroup validation skipped: no coherent host-visible memory");
        return;
    }

    auto memory = logical.AllocateMemory({
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    });
    vk::Check(dld.vkBindBufferMemory(*logical, *probe_buffer, *memory, 0));
    const u32* const output = reinterpret_cast<const u32*>(memory.Map(0, ProbeBytes));
    struct MemoryUnmapGuard {
        const vk::DeviceMemory& memory;
        ~MemoryUnmapGuard() {
            memory.Unmap();
        }
    } unmap_guard{memory};

    const VkDescriptorSetLayoutBinding binding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .pImmutableSamplers = nullptr,
    };
    const auto descriptor_layout = logical.CreateDescriptorSetLayout({
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = 1,
        .pBindings = &binding,
    });
    const VkDescriptorPoolSize pool_size{
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
    };
    const auto descriptor_pool = logical.CreateDescriptorPool({
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    });
    const VkDescriptorSetLayout raw_layout = *descriptor_layout;
    const auto descriptor_sets = descriptor_pool.Allocate({
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = *descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &raw_layout,
    });
    if (descriptor_sets.IsOutOfPoolMemory()) {
        LOG_WARNING(Render_Vulkan,
                    "XCLIPSE PROBE subgroup validation skipped: descriptor allocation failed");
        return;
    }
    const VkDescriptorSet descriptor_set = descriptor_sets[0];
    const VkDescriptorBufferInfo buffer_info{
        .buffer = *probe_buffer,
        .offset = 0,
        .range = ProbeBytes,
    };
    const VkWriteDescriptorSet descriptor_write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = descriptor_set,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pImageInfo = nullptr,
        .pBufferInfo = &buffer_info,
        .pTexelBufferView = nullptr,
    };
    dld.vkUpdateDescriptorSets(*logical, 1, &descriptor_write, 0, nullptr);

    const auto pipeline_layout = logical.CreatePipelineLayout({
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 1,
        .pSetLayouts = &raw_layout,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr,
    });
    auto command_pool = logical.CreateCommandPool({
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = graphics_family,
    });

    const auto run_probe =
        [&](const u32* code, std::size_t code_size, u32 wave_size,
            auto&& validate_output) -> bool {
        const auto shader = logical.CreateShaderModule({
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = code_size,
            .pCode = code,
        });
        const VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT subgroup_size_ci{
            .sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT,
            .pNext = nullptr,
            .requiredSubgroupSize = wave_size,
        };
        const VkPipelineShaderStageCreateInfo stage_ci{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = &subgroup_size_ci,
            .flags = 0,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = *shader,
            .pName = "main",
            .pSpecializationInfo = nullptr,
        };

        vk::Pipeline pipeline;
        try {
            pipeline = logical.CreateComputePipeline({
                .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = stage_ci,
                .layout = *pipeline_layout,
                .basePipelineHandle = VK_NULL_HANDLE,
                .basePipelineIndex = -1,
            });
        } catch (const vk::Exception& exception) {
            LOG_INFO(Render_Vulkan,
                     "XCLIPSE PROBE wave{} pipeline rejected: {}", wave_size,
                     exception.what());
            return false;
        }

        auto command_buffers = command_pool.Allocate(1);
        vk::CommandBuffer command_buffer{command_buffers[0], dld};
        command_buffer.Begin({
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
        });
        command_buffer.FillBuffer(*probe_buffer, 0, ProbeBytes, 0);
        const VkMemoryBarrier clear_barrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        };
        command_buffer.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                       clear_barrier);
        command_buffer.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline);
        command_buffer.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline_layout, 0,
                                          descriptor_set, {});
        command_buffer.Dispatch(1, 1, 1);
        const VkMemoryBarrier host_barrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        };
        command_buffer.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_PIPELINE_STAGE_HOST_BIT, 0, host_barrier);
        command_buffer.End();

        auto fence = logical.CreateFence({
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
        });
        const VkCommandBuffer raw_command = *command_buffer;
        const VkSubmitInfo submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &raw_command,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr,
        };
        const VkResult submit_result = graphics_queue.Submit(submit_info, *fence);
        const VkResult wait_result =
            submit_result == VK_SUCCESS ? fence.Wait(ProbeTimeoutNs) : submit_result;
        if (submit_result != VK_SUCCESS || wait_result != VK_SUCCESS) {
            LOG_WARNING(Render_Vulkan,
                        "XCLIPSE PROBE wave{} submit={} wait={}", wave_size,
                        submit_result, wait_result);
            return false;
        }
        return validate_output(output);
    };

    const auto validate_wave = [&](u32 wave_size) {
        std::array<u32, ProbeInvocations> lane_counts{};
        bool valid = true;
        for (u32 invocation = 0; invocation < ProbeInvocations; ++invocation) {
            const u32 base = invocation * 2U;
            const u32 subgroup_size = output[base + 0U];
            const u32 lane = output[base + 1U];
            valid &= subgroup_size == wave_size && lane < wave_size;
            if (lane < lane_counts.size()) {
                ++lane_counts[lane];
            }
        }
        const u32 expected_lane_occurrences = ProbeInvocations / wave_size;
        for (u32 lane = 0; lane < ProbeInvocations; ++lane) {
            const u32 expected = lane < wave_size ? expected_lane_occurrences : 0U;
            valid &= lane_counts[lane] == expected;
        }
        return valid;
    };

    const auto run_wave = [&](u32 wave_size) {
        if (wave_size < caps.min_subgroup_size || wave_size > caps.max_subgroup_size ||
            (ProbeInvocations % wave_size) != 0) {
            return false;
        }
        const bool valid = run_probe(XCLIPSE_SUBGROUP_PROBE_COMP_SPV,
                                     sizeof(XCLIPSE_SUBGROUP_PROBE_COMP_SPV), wave_size,
                                     [&](const u32*) { return validate_wave(wave_size); });
        LOG_INFO(Render_Vulkan, "XCLIPSE PROBE Wave{} result={}", wave_size,
                 valid ? "validated" : "failed");
        return valid;
    };

    xclipse.wave32_validated = run_wave(32);
    xclipse.wave64_validated = run_wave(64);
    xclipse.allowed_wave_mask = (xclipse.wave32_validated ? 0x1U : 0U) |
                                (xclipse.wave64_validated ? 0x2U : 0U);

    if (xclipse.wave32_validated != xclipse.wave64_validated) {
        xclipse.preferred_compute_wave = xclipse.wave32_validated ? 32U : 64U;
    } else {
        // Both valid still requires benchmark evidence before preferring one.
        xclipse.preferred_compute_wave = 0;
    }
    if (xclipse.allowed_wave_mask != 0) {
        caps.required_subgroup_size = CapabilityState::Validated;
    }

    u32 operation_wave = 0;
    if (caps.subgroup_size == 32 && xclipse.wave32_validated) {
        operation_wave = 32;
    } else if (caps.subgroup_size == 64 && xclipse.wave64_validated) {
        operation_wave = 64;
    } else if (xclipse.wave32_validated) {
        operation_wave = 32;
    } else if (xclipse.wave64_validated) {
        operation_wave = 64;
    }
    if (operation_wave == 0) {
        device_policy.policy_hash = ComputeVulkanPolicyHash(device_policy);
        return;
    }

    enum class OperationProbe {
        Ballot,
        Shuffle,
        Arithmetic,
        Quad,
    };
    const auto run_operation =
        [&](OperationProbe operation, CapabilityState advertised_state, const u32* code,
            std::size_t code_size) -> CapabilityState {
        if (advertised_state == CapabilityState::Unsupported) {
            return CapabilityState::Unsupported;
        }
        const bool valid = run_probe(
            code, code_size, operation_wave, [&](const u32* words) {
                for (u32 invocation = 0; invocation < ProbeInvocations; ++invocation) {
                    const u32 base = invocation * 3U;
                    const u32 subgroup_size = words[base + 0U];
                    const u32 value = words[base + 1U];
                    const u32 lane = words[base + 2U];
                    if (subgroup_size != operation_wave || lane >= operation_wave) {
                        return false;
                    }
                    switch (operation) {
                    case OperationProbe::Ballot:
                    case OperationProbe::Arithmetic:
                        if (value != operation_wave) {
                            return false;
                        }
                        break;
                    case OperationProbe::Shuffle:
                        if (value != 0U) {
                            return false;
                        }
                        break;
                    case OperationProbe::Quad:
                        if (value != (lane ^ 1U)) {
                            return false;
                        }
                        break;
                    }
                }
                return true;
            });
        return valid ? CapabilityState::Validated : CapabilityState::Advertised;
    };

    caps.subgroup_ballot =
        run_operation(OperationProbe::Ballot, caps.subgroup_ballot,
                      XCLIPSE_SUBGROUP_OP_PROBE_BALLOT_COMP_SPV,
                      sizeof(XCLIPSE_SUBGROUP_OP_PROBE_BALLOT_COMP_SPV));
    caps.subgroup_shuffle =
        run_operation(OperationProbe::Shuffle, caps.subgroup_shuffle,
                      XCLIPSE_SUBGROUP_OP_PROBE_SHUFFLE_COMP_SPV,
                      sizeof(XCLIPSE_SUBGROUP_OP_PROBE_SHUFFLE_COMP_SPV));
    caps.subgroup_arithmetic =
        run_operation(OperationProbe::Arithmetic, caps.subgroup_arithmetic,
                      XCLIPSE_SUBGROUP_OP_PROBE_ARITHMETIC_COMP_SPV,
                      sizeof(XCLIPSE_SUBGROUP_OP_PROBE_ARITHMETIC_COMP_SPV));
    caps.subgroup_quad =
        run_operation(OperationProbe::Quad, caps.subgroup_quad,
                      XCLIPSE_SUBGROUP_OP_PROBE_QUAD_COMP_SPV,
                      sizeof(XCLIPSE_SUBGROUP_OP_PROBE_QUAD_COMP_SPV));

    LOG_INFO(Render_Vulkan,
             "XCLIPSE PROBE subgroup ops wave={} ballot={} shuffle={} arithmetic={} quad={}",
             operation_wave, CapabilityStateName(caps.subgroup_ballot),
             CapabilityStateName(caps.subgroup_shuffle),
             CapabilityStateName(caps.subgroup_arithmetic),
             CapabilityStateName(caps.subgroup_quad));

    device_policy.policy_hash = ComputeVulkanPolicyHash(device_policy);
}

void Device::RunXclipseValidationProbes() {
    if (!device_policy.xclipse.detected || !Settings::values.xclipse_validation_probes.GetValue()) {
        UpdateXclipseBcnProfile();
        device_policy.policy_hash = ComputeVulkanPolicyHash(device_policy);
        return;
    }

    auto& caps = device_policy.capabilities;

    // Validate exact BC image creation without allocating memory or advertising unsupported use.
    for (std::size_t index = 0; index < BCN_FORMATS.size(); ++index) {
        auto& format_caps = caps.bcn[index];
        if (format_caps.image_create == CapabilityState::Unsupported) {
            continue;
        }
        const VkImageCreateInfo image_ci{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = BCN_FORMATS[index],
            .extent = {4, 4, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        VkImage image = VK_NULL_HANDLE;
        const VkResult result = dld.vkCreateImage(*logical, &image_ci, nullptr, &image);
        if (result == VK_SUCCESS) {
            format_caps.image_create = CapabilityState::Validated;
            dld.vkDestroyImage(*logical, image, nullptr);
        } else {
            LOG_WARNING(Render_Vulkan,
                        "XCLIPSE PROBE BC format={} image_create failed result={}",
                        BCN_FORMATS[index], result);
        }
    }

    // Exercise a real queue submission before pipeline caches are loaded. This validates the
    // selected synchronization API without using queue-idle or persistent resources.
    const bool probe_sync2 = caps.synchronization2 == CapabilityState::Advertised;
    const bool probe_timeline = caps.timeline == CapabilityState::Advertised;
    if (probe_sync2 || probe_timeline) {
        try {
            const VkCommandPoolCreateInfo pool_ci{
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .pNext = nullptr,
                .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                .queueFamilyIndex = graphics_family,
            };
            auto command_pool = logical.CreateCommandPool(pool_ci);
            auto command_buffers = command_pool.Allocate(1);
            vk::CommandBuffer command_buffer{command_buffers[0], dld};
            command_buffer.Begin({
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .pNext = nullptr,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                .pInheritanceInfo = nullptr,
            });
            command_buffer.End();

            const VkFenceCreateInfo fence_ci{
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
            };
            auto fence = logical.CreateFence(fence_ci);

            vk::Semaphore timeline;
            if (probe_timeline) {
                const VkSemaphoreTypeCreateInfo type_ci{
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                    .pNext = nullptr,
                    .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                    .initialValue = 0,
                };
                const VkSemaphoreCreateInfo semaphore_ci{
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                    .pNext = &type_ci,
                    .flags = 0,
                };
                timeline = logical.CreateSemaphore(semaphore_ci);
            }

            VkResult submit_result = VK_SUCCESS;
            if (probe_sync2) {
                const VkCommandBufferSubmitInfo command_info{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                    .pNext = nullptr,
                    .commandBuffer = *command_buffer,
                    .deviceMask = 0,
                };
                const VkSemaphoreSubmitInfo signal_info{
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                    .pNext = nullptr,
                    .semaphore = probe_timeline ? *timeline : VK_NULL_HANDLE,
                    .value = probe_timeline ? 1U : 0U,
                    .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    .deviceIndex = 0,
                };
                const VkSubmitInfo2 submit_info{
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                    .pNext = nullptr,
                    .flags = 0,
                    .waitSemaphoreInfoCount = 0,
                    .pWaitSemaphoreInfos = nullptr,
                    .commandBufferInfoCount = 1,
                    .pCommandBufferInfos = &command_info,
                    .signalSemaphoreInfoCount = probe_timeline ? 1U : 0U,
                    .pSignalSemaphoreInfos = probe_timeline ? &signal_info : nullptr,
                };
                submit_result = graphics_queue.Submit2(submit_info, *fence);
            } else {
                const u64 signal_value = 1;
                const VkTimelineSemaphoreSubmitInfo timeline_info{
                    .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
                    .pNext = nullptr,
                    .waitSemaphoreValueCount = 0,
                    .pWaitSemaphoreValues = nullptr,
                    .signalSemaphoreValueCount = 1,
                    .pSignalSemaphoreValues = &signal_value,
                };
                const VkSemaphore signal_semaphore = *timeline;
                const VkCommandBuffer raw_command = *command_buffer;
                const VkSubmitInfo submit_info{
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .pNext = &timeline_info,
                    .waitSemaphoreCount = 0,
                    .pWaitSemaphores = nullptr,
                    .pWaitDstStageMask = nullptr,
                    .commandBufferCount = 1,
                    .pCommandBuffers = &raw_command,
                    .signalSemaphoreCount = 1,
                    .pSignalSemaphores = &signal_semaphore,
                };
                submit_result = graphics_queue.Submit(submit_info, *fence);
            }

            constexpr u64 ProbeTimeoutNs = 1'000'000'000ULL;
            const VkResult wait_result =
                submit_result == VK_SUCCESS ? fence.Wait(ProbeTimeoutNs) : submit_result;
            if (submit_result == VK_SUCCESS && wait_result == VK_SUCCESS) {
                if (probe_sync2) {
                    caps.synchronization2 = CapabilityState::Validated;
                }
                if (probe_timeline && timeline.GetCounter() >= 1) {
                    caps.timeline = CapabilityState::Validated;
                }
            } else {
                LOG_WARNING(Render_Vulkan,
                            "XCLIPSE PROBE sync submit={} wait={} sync2={} timeline={}",
                            submit_result, wait_result, probe_sync2, probe_timeline);
            }
        } catch (const vk::Exception& exception) {
            LOG_WARNING(Render_Vulkan, "XCLIPSE PROBE synchronization exception: {}",
                        exception.what());
        }
    }

    try {
        RunXclipseSubgroupValidationProbes();
    } catch (const vk::Exception& exception) {
        LOG_WARNING(Render_Vulkan, "XCLIPSE PROBE subgroup validation exception: {}",
                    exception.what());
    }

    UpdateXclipseBcnProfile();
    device_policy.policy_hash = ComputeVulkanPolicyHash(device_policy);
}

void Device::LogDevicePolicy() const {
    if (!device_policy.xclipse.detected) {
        return;
    }

    std::string pipeline_uuid;
    pipeline_uuid.reserve(device_policy.identity.pipeline_cache_uuid.size() * 2);
    for (const auto byte : device_policy.identity.pipeline_cache_uuid) {
        fmt::format_to(std::back_inserter(pipeline_uuid), "{:02x}", byte);
    }

    const auto& identity = device_policy.identity;
    const auto& caps = device_policy.capabilities;
    const auto& xclipse = device_policy.xclipse;
    const auto bcn_state = [&caps](std::initializer_list<BcnFormat> formats) -> std::string_view {
        const bool runtime_native =
            std::ranges::all_of(formats, [&caps](BcnFormat format) {
                const std::size_t index = static_cast<std::size_t>(format);
                return SupportsXclipseRuntimeNativeBcnPath(BCN_FORMATS[index], caps.bcn[index]);
            });
        if (runtime_native) {
            return "native";
        }
        const bool image_create_validated =
            std::ranges::all_of(formats, [&caps](BcnFormat format) {
                return HasValidatedImageCreation(
                    caps.bcn[static_cast<std::size_t>(format)]);
            });
        if (image_create_validated) {
            return "image-create-validated/ops-unvalidated";
        }
        const bool advertised = std::ranges::all_of(formats, [&caps](BcnFormat format) {
            return SupportsAdvertisedNativeBcnPath(
                caps.bcn[static_cast<std::size_t>(format)]);
        });
        return advertised ? "advertised/unvalidated" : "unsupported";
    };

    LOG_INFO(Render_Vulkan,
             "XCLIPSE PROFILE model=Xclipse{} soc={} driver={} driver_id={} device_id=0x{:x} "
             "driver_version={} pipeline_uuid={} policy_hash={:016x}",
             xclipse.model, identity.soc_model.empty() ? "unknown" : identity.soc_model,
             identity.driver_name, identity.driver_id, identity.device_id, identity.driver_version,
             pipeline_uuid, device_policy.policy_hash);
    LOG_INFO(Render_Vulkan,
             "XCLIPSE FEATURES BC1={} BC2={} BC3={} BC4={} BC5={} BC6={} BC7={} "
             "wave32={} wave64={} allowed_wave_mask=0x{:x} preferred_compute_wave={} "
             "sync2={} timeline={} descriptor_buffer={} sparse={}",
             bcn_state({BcnFormat::BC1_RGB_UNORM, BcnFormat::BC1_RGB_SRGB,
                        BcnFormat::BC1_RGBA_UNORM, BcnFormat::BC1_RGBA_SRGB}),
             bcn_state({BcnFormat::BC2_UNORM, BcnFormat::BC2_SRGB}),
             bcn_state({BcnFormat::BC3_UNORM, BcnFormat::BC3_SRGB}),
             bcn_state({BcnFormat::BC4_UNORM, BcnFormat::BC4_SNORM}),
             bcn_state({BcnFormat::BC5_UNORM, BcnFormat::BC5_SNORM}),
             bcn_state({BcnFormat::BC6H_UFLOAT, BcnFormat::BC6H_SFLOAT}),
             bcn_state({BcnFormat::BC7_UNORM, BcnFormat::BC7_SRGB}),
             xclipse.wave32_validated ? "validated" : "unvalidated",
             xclipse.wave64_validated ? "validated" : "unvalidated",
             xclipse.allowed_wave_mask, xclipse.preferred_compute_wave,
             CapabilityStateName(caps.synchronization2), CapabilityStateName(caps.timeline),
             CapabilityStateName(caps.descriptor_buffer), CapabilityStateName(caps.sparse_binding));
    LOG_INFO(Render_Vulkan,
             "XCLIPSE SUBGROUP required_size={} ballot={} shuffle={} arithmetic={} quad={}",
             CapabilityStateName(caps.required_subgroup_size),
             CapabilityStateName(caps.subgroup_ballot),
             CapabilityStateName(caps.subgroup_shuffle),
             CapabilityStateName(caps.subgroup_arithmetic),
             CapabilityStateName(caps.subgroup_quad));
}

void Device::LogXclipseTelemetry() const {
    if (!xclipse_telemetry.Enabled()) {
        return;
    }
    const auto t = xclipse_telemetry.Snapshot();
    const double commands_per_submit =
        t.queue_submits != 0 ? static_cast<double>(t.commands_submitted) /
                                  static_cast<double>(t.queue_submits)
                            : 0.0;
    const double average_compile_ms =
        t.pipeline_creates != 0
            ? static_cast<double>(t.pipeline_compile_ns_total) /
                  static_cast<double>(t.pipeline_creates) / 1'000'000.0
            : 0.0;
    LOG_INFO(Render_Vulkan,
             "XCLIPSE PIPELINE creates={} graphics={} compute={} cache_hits={} cache_misses={} "
             "failures={} policy_violations={} compile_avg_ms={:.3f} compile_max_ms={:.3f}",
             t.pipeline_creates, t.graphics_pipeline_creates, t.compute_pipeline_creates,
             t.pipeline_cache_hits, t.pipeline_cache_misses, t.pipeline_failures,
             t.pipeline_policy_violations, average_compile_ms,
             static_cast<double>(t.pipeline_compile_ns_max) / 1'000'000.0);
    LOG_INFO(Render_Vulkan,
             "XCLIPSE SYNC submits={} commands_per_submit={:.2f} sync2_submits={} legacy_submits={} "
             "host_waits={} timeline_waits={} scheduler_finishes={} all_commands_barriers={} "
             "transfer_consumer_barriers={} policy_enabled={}",
             t.queue_submits, commands_per_submit, t.sync2_submits, t.legacy_submits, t.host_waits,
             t.timeline_waits, t.scheduler_finishes, t.all_commands_barriers,
             t.transfer_consumer_barriers, device_policy.use_xclipse_sync_policy);
    LOG_INFO(Render_Vulkan,
             "XCLIPSE DESCRIPTORS set_allocations={} buffer_allocations={} descriptor_bytes={} "
             "ring_wraps={} stalls={}",
             t.descriptor_set_allocations, t.descriptor_buffer_allocations, t.descriptor_bytes,
             t.descriptor_buffer_wraps, t.descriptor_stalls);
    LOG_INFO(Render_Vulkan,
             "XCLIPSE BCN gpu_dispatches={} compressed_bytes={} gpu_fallbacks={}",
             t.bcn_gpu_decode_dispatches, t.bcn_gpu_decode_bytes, t.bcn_gpu_decode_fallbacks);
    LOG_INFO(Render_Vulkan, "XCLIPSE MEMORY budget={} resident={}", device_access_memory,
             CanReportMemoryUsage() ? GetDeviceMemoryUsage() : 0);
}

Device::Device(VkInstance instance_, vk::PhysicalDevice physical_, VkSurfaceKHR surface,
               const vk::InstanceDispatch& dld_)
    : instance{instance_}, dld{dld_}, physical{physical_},
    format_properties(GetFormatProperties(physical)) {
    // Get suitability and device properties.
    const bool is_suitable = GetSuitability(surface != VkSurfaceKHR{});

    const VkDriverId driver_id = properties.driver.driverID;

    const bool is_radv = driver_id == VK_DRIVER_ID_MESA_RADV;
    const bool is_amd_driver =
        driver_id == VK_DRIVER_ID_AMD_PROPRIETARY || driver_id == VK_DRIVER_ID_AMD_OPEN_SOURCE;
    const bool is_amd = is_amd_driver || is_radv;

    const bool is_intel_windows = driver_id == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS;
    const bool is_intel_anv = driver_id == VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA;

    const bool is_nvidia = driver_id == VK_DRIVER_ID_NVIDIA_PROPRIETARY;
    const bool is_mvk = driver_id == VK_DRIVER_ID_MOLTENVK;
    const bool is_qualcomm = driver_id == VK_DRIVER_ID_QUALCOMM_PROPRIETARY;
    const bool is_turnip = driver_id == VK_DRIVER_ID_MESA_TURNIP;

    if (!is_suitable)
        LOG_WARNING(Render_Vulkan, "Unsuitable driver - continuing anyways");

    if (is_nvidia) {
        nvidia_arch = GetNvidiaArchitecture(physical, supported_extensions);
    }

    SetupFamilies(surface);
    BuildDevicePolicy();
    xclipse_telemetry.SetEnabled(device_policy.xclipse.detected &&
                                 Settings::values.xclipse_runtime_telemetry.GetValue());
    const auto queue_cis = GetDeviceQueueCreateInfos();

    // GetSuitability has already configured the linked list of features for us.
    // Reuse it here.
    const void* first_next = &features2;

    VkDeviceDiagnosticsConfigCreateInfoNV diagnostics_nv{};
    const bool use_diagnostics_nv = Settings::values.enable_nsight_aftermath && extensions.device_diagnostics_config;
    if (use_diagnostics_nv) {
        nsight_aftermath_tracker = std::make_unique<NsightAftermathTracker>();

        diagnostics_nv = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV,
            .pNext = &features2,
            .flags = VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV |
                     VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV |
                     VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_AUTOMATIC_CHECKPOINTS_BIT_NV,
        };
        first_next = &diagnostics_nv;
    }

    is_blit_depth24_stencil8_supported = TestDepthStencilBlits(VK_FORMAT_D24_UNORM_S8_UINT);
    is_blit_depth32_stencil8_supported = TestDepthStencilBlits(VK_FORMAT_D32_SFLOAT_S8_UINT);
    is_optimal_astc_supported = ComputeIsOptimalAstcSupported();
    is_warp_potentially_bigger = !extensions.subgroup_size_control ||
                                 properties.subgroup_size_control.maxSubgroupSize > GuestWarpSize;

    is_integrated = properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    is_virtual = properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
    is_non_gpu = properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_OTHER ||
                 properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;

    supports_d24_depth =
        IsFormatSupported(VK_FORMAT_D24_UNORM_S8_UINT,
                          VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, FormatType::Optimal);

    supports_conditional_barriers = !(is_intel_anv || is_intel_windows);

    CollectPhysicalMemoryInfo();
    CollectToolingInfo();

    if (is_qualcomm) {
        must_emulate_scaled_formats = true;
        LOG_WARNING(Render_Vulkan, "Qualcomm drivers require scaled vertex format emulation.");
        has_broken_descriptor_aliasing = true;
        LOG_WARNING(Render_Vulkan, "Qualcomm drivers have broken descriptor aliasing.");
        LOG_WARNING(Render_Vulkan, "Qualcomm drivers have broken color write enable.");
        RemoveExtensionFeature(extensions.color_write_enable, features.color_write_enable,
                               VK_EXT_COLOR_WRITE_ENABLE_EXTENSION_NAME);
        LOG_WARNING(Render_Vulkan, "Qualcomm drivers have broken shader atomic int64.");
        RemoveExtensionFeature(extensions.shader_atomic_int64, features.shader_atomic_int64,
                               VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME);
        features.shader_atomic_int64.shaderBufferInt64Atomics = false;
        features.shader_atomic_int64.shaderSharedInt64Atomics = false;
        features.features.shaderInt64 = false;
        LOG_WARNING(Render_Vulkan, "Qualcomm drivers have broken workgroup memory explicit layout.");
        RemoveExtensionFeature(extensions.workgroup_memory_explicit_layout,
                               features.workgroup_memory_explicit_layout,
                               VK_KHR_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_EXTENSION_NAME);

#if defined(__ANDROID__) && defined(ARCHITECTURE_arm64)
        // BCn patching only safe on Android 9+ (API 28+). Older versions crash on driver load.
        const auto major = (properties.properties.driverVersion >> 24) << 2;
        const auto minor = (properties.properties.driverVersion >> 12) & 0xFFFU;
        const auto vendor = properties.properties.vendorID;
        const auto patch_status = adrenotools_get_bcn_type(major, minor, vendor);
        const int api_level = android_get_device_api_level();

        bool should_patch_bcn = api_level >= 28;
        const bool bcn_debug_override = Settings::values.patch_old_qcom_drivers.GetValue();
        if (bcn_debug_override != should_patch_bcn) {
            LOG_WARNING(Render_Vulkan,
                "BCn patch debug override active: {} (auto-detected: {})",
                bcn_debug_override, should_patch_bcn);
            should_patch_bcn = bcn_debug_override;
        }

        if (patch_status == ADRENOTOOLS_BCN_PATCH) {
            if (should_patch_bcn) {
                LOG_INFO(Render_Vulkan,
                    "Patching Adreno driver to support BCn texture formats "
                    "(Android API {}, Driver {}.{})", api_level, major, minor);
                if (adrenotools_patch_bcn(
                        reinterpret_cast<void*>(dld.vkGetPhysicalDeviceFormatProperties))) {
                    OverrideBcnFormats(format_properties);
                } else {
                    LOG_ERROR(Render_Vulkan, "BCn patch failed! Driver code may now crash");
                }
            } else {
                LOG_WARNING(Render_Vulkan,
                    "BCn texture patching skipped for stability (Android API {} < 28). "
                    "Driver version {}.{} would support patching, but may crash on older Android.",
                    api_level, major, minor);
            }
        } else if (patch_status == ADRENOTOOLS_BCN_BLOB) {
            LOG_INFO(Render_Vulkan, "Adreno driver supports BCn textures natively (no patch needed)");
        } else {
            LOG_INFO(Render_Vulkan,
                "Adreno driver does not support BCn texture patching (Android API {}, Driver {}.{})",
                api_level, major, minor);
        }
#endif
    }

    if (is_nvidia) {
        const auto arch = GetNvidiaArch();
        if (arch >= NvidiaArchitecture::Arch_AmpereOrNewer) {
            LOG_WARNING(Render_Vulkan, "Ampere and newer have broken float16 math");
            features.shader_float16_int8.shaderFloat16 = false;
        }

        // Use hardware depth/stencil blits instead when available
        if (!extensions.shader_stencil_export) {
            LOG_INFO(Render_Vulkan,
                     "NVIDIA: VK_EXT_shader_stencil_export not supported, using hardware blits "
                     "for depth/stencil operations");
            LOG_INFO(Render_Vulkan, "  D24S8 hardware blit support: {}",
                     is_blit_depth24_stencil8_supported);
            LOG_INFO(Render_Vulkan, "  D32S8 hardware blit support: {}",
                     is_blit_depth32_stencil8_supported);

            if (!is_blit_depth24_stencil8_supported && !is_blit_depth32_stencil8_supported) {
                LOG_WARNING(Render_Vulkan,
                            "NVIDIA: Neither shader export nor hardware blits available for "
                            "depth/stencil. Performance may be degraded.");
            }
        }
    }

    sets_per_pool = 64;
    if (is_amd_driver) {
        // AMD drivers need a higher amount of Sets per Pool in certain circumstances like in XC2.
        sets_per_pool = 96;

        // Disable VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT on AMD GCN4 and lower as it is broken.
        if (!features.shader_float16_int8.shaderFloat16) {
            LOG_WARNING(Render_Vulkan,
                        "AMD GCN4 and earlier have broken VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT");
            has_broken_cube_compatibility = true;
        }

        // AMD drivers (2026+) have broken float16 math on DKCR
        if (features.shader_float16_int8.shaderFloat16) {
            LOG_WARNING(Render_Vulkan,
                        "AMD drivers (2026+) have broken float16 math");
            features.shader_float16_int8.shaderFloat16 = false;
        }
    }

    if (extensions.sampler_filter_minmax && is_amd) {
        // Disable ext_sampler_filter_minmax on AMD GCN4 and lower as it is broken.
        if (!features.shader_float16_int8.shaderFloat16) {
            LOG_WARNING(Render_Vulkan,
                        "AMD GCN4 and earlier have broken VK_EXT_sampler_filter_minmax");
            RemoveExtension(extensions.sampler_filter_minmax,
                            VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME);
        }
    }

    if (features.shader_float16_int8.shaderFloat16 && is_intel_windows) {
        // Intel's compiler crashes when using fp16 on Astral Chain, disable it for the time being.
        LOG_WARNING(Render_Vulkan, "Intel has broken float16 math");
        features.shader_float16_int8.shaderFloat16 = false;
    }

    has_broken_compute =
        CheckBrokenCompute(properties.driver.driverID, properties.properties.driverVersion) &&
        !Settings::values.enable_compute_pipelines.GetValue();

    if (is_mvk) {
        LOG_WARNING(Render_Vulkan,
                    "MVK driver breaks when using more than 16 vertex attributes/bindings");
        properties.properties.limits.maxVertexInputAttributes =
            (std::min)(properties.properties.limits.maxVertexInputAttributes, 16U);
        properties.properties.limits.maxVertexInputBindings =
            (std::min)(properties.properties.limits.maxVertexInputBindings, 16U);
    }

    if (is_turnip || is_qualcomm) {
        LOG_WARNING(Render_Vulkan, "Driver requires higher-than-reported binding limits");
        properties.properties.limits.maxVertexInputBindings = 32;
    }

    const auto dyna_state = Settings::values.dyna_state.GetValue();
    switch (dyna_state) {
    case Settings::ExtendedDynamicState::Disabled:
        // Level 0: Disable all extended dynamic state extensions
        RemoveExtensionFeature(extensions.extended_dynamic_state, features.extended_dynamic_state,
                              VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
        RemoveExtensionFeature(extensions.extended_dynamic_state2, features.extended_dynamic_state2,
                              VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME);
        RemoveExtensionFeature(extensions.extended_dynamic_state3, features.extended_dynamic_state3,
                              VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        dynamic_state3_blending = false;
        dynamic_state3_enables = false;
        break;
    case Settings::ExtendedDynamicState::EDS1:
        // Level 1: Enable EDS1, disable EDS2 and EDS3
        RemoveExtensionFeature(extensions.extended_dynamic_state2, features.extended_dynamic_state2,
                              VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME);
        RemoveExtensionFeature(extensions.extended_dynamic_state3, features.extended_dynamic_state3,
                              VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        dynamic_state3_blending = false;
        dynamic_state3_enables = false;
        break;
    case Settings::ExtendedDynamicState::EDS2:
        // Level 2: Enable EDS1 + EDS2, disable EDS3
        RemoveExtensionFeature(extensions.extended_dynamic_state3, features.extended_dynamic_state3,
                              VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        dynamic_state3_blending = false;
        dynamic_state3_enables = false;
        break;
    case Settings::ExtendedDynamicState::EDS3:
    default:
        // Level 3: Enable all (EDS1 + EDS2 + EDS3)
        break;
    }

    // VK_EXT_vertex_input_dynamic_state
    if (!Settings::values.vertex_input_dynamic_state.GetValue()) {
        RemoveExtensionFeature(extensions.vertex_input_dynamic_state, features.vertex_input_dynamic_state, VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME);
    }

    // Descriptors feature list
    {
        auto& descriptor_indexing = features.descriptor_indexing;
        descriptor_indexing.shaderInputAttachmentArrayDynamicIndexing = false;
        descriptor_indexing.shaderUniformTexelBufferArrayDynamicIndexing = false;
        descriptor_indexing.shaderStorageTexelBufferArrayDynamicIndexing = false;
        descriptor_indexing.shaderUniformBufferArrayNonUniformIndexing = false;
        descriptor_indexing.shaderStorageBufferArrayNonUniformIndexing = false;
        descriptor_indexing.shaderInputAttachmentArrayNonUniformIndexing = false;
        descriptor_indexing.descriptorBindingUniformBufferUpdateAfterBind = false;
        descriptor_indexing.descriptorBindingSampledImageUpdateAfterBind = false;
        descriptor_indexing.descriptorBindingStorageImageUpdateAfterBind = false;
        descriptor_indexing.descriptorBindingStorageBufferUpdateAfterBind = false;
        descriptor_indexing.descriptorBindingUniformTexelBufferUpdateAfterBind = false;
        descriptor_indexing.descriptorBindingStorageTexelBufferUpdateAfterBind = false;
        descriptor_indexing.descriptorBindingUpdateUnusedWhilePending = false;
        descriptor_indexing.descriptorBindingVariableDescriptorCount = false;
        descriptor_indexing.runtimeDescriptorArray = false;
    }

    // VK_EXT_descriptor_buffer requires VK_KHR_buffer_device_address
    if (extensions.descriptor_buffer && !features.buffer_device_address.bufferDeviceAddress) {
        LOG_WARNING(Render_Vulkan, "Descriptor buffer needs buffer device address, disabling.");
        RemoveExtensionFeature(extensions.descriptor_buffer, features.descriptor_buffer,
                               VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
    }
    if (!extensions.descriptor_buffer) {
        RemoveExtensionFeature(extensions.buffer_device_address, features.buffer_device_address,
                               VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    }

    logical = vk::Device::Create(physical, queue_cis, ExtensionListForVulkan(loaded_extensions), first_next, dld);

    graphics_queue = logical.GetQueue(graphics_family);
    present_queue = logical.GetQueue(present_family);

    RunXclipseValidationProbes();

    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = dld.vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = dld.vkGetDeviceProcAddr;

    VmaAllocatorCreateFlags flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
    if (extensions.memory_budget) {
        flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    }
    if (extensions.buffer_device_address) {
        flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    }
    const VmaAllocatorCreateInfo allocator_info{
            .flags = flags,
            .physicalDevice = physical,
            .device = *logical,
            .preferredLargeHeapBlockSize = is_integrated
                                           ? (64u * 1024u * 1024u)
                                           : (256u * 1024u * 1024u),
            .pAllocationCallbacks = nullptr,
            .pDeviceMemoryCallbacks = nullptr,
            .pHeapSizeLimit = nullptr,
            .pVulkanFunctions = &functions,
            .instance = instance,
            .vulkanApiVersion = ApiVersion(),
            .pTypeExternalMemoryHandleTypes = nullptr,
    };

    vk::Check(vmaCreateAllocator(&allocator_info, &allocator));

    LogDevicePolicy();

    owns_static_pipeline_cache = surface != VkSurfaceKHR{};
    LoadStaticPipelineCache();

    // Initialize GPU logging if enabled
    InitializeGPULogging();
}

Device::~Device() {
    SaveStaticPipelineCache();
    LogXclipseTelemetry();
    ShutdownGPULogging();
    vmaDestroyAllocator(allocator);
}

void Device::LoadStaticPipelineCache() {
    const auto create = [this](size_t size, const void* data) {
        static_pipeline_cache = logical.CreatePipelineCache({
            .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .initialDataSize = size,
            .pInitialData = data,
        });
    };
    if (!owns_static_pipeline_cache) {
        create(0, nullptr);
        return;
    }
    const auto filename = StaticPipelineCacheFilename();
    if (filename.empty()) {
        create(0, nullptr);
        return;
    }
    std::vector<char> data;
    try {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            create(0, nullptr);
            return;
        }
        file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        const size_t total = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);
        std::array<char, 8> magic{};
        u32 version{};
        u64 policy_hash{};
        constexpr size_t header_size =
            STATIC_CACHE_MAGIC_NUMBER.size() + sizeof(version) + sizeof(policy_hash);
        if (total < header_size) {
            create(0, nullptr);
            return;
        }
        file.read(magic.data(), magic.size())
            .read(reinterpret_cast<char*>(&version), sizeof(version))
            .read(reinterpret_cast<char*>(&policy_hash), sizeof(policy_hash));
        if (magic != STATIC_CACHE_MAGIC_NUMBER || version != STATIC_CACHE_VERSION ||
            policy_hash != device_policy.policy_hash) {
            LOG_INFO(Render_Vulkan,
                     "Ignoring static Vulkan pipeline cache: identity mismatch "
                     "(version={} expected={} policy={:016x} expected_policy={:016x})",
                     version, STATIC_CACHE_VERSION, policy_hash, device_policy.policy_hash);
            create(0, nullptr);
            return;
        }
        data.resize(total - header_size);
        file.read(data.data(), static_cast<std::streamsize>(data.size()));
    } catch (const std::ios_base::failure& e) {
        create(0, nullptr);
        return;
    }
    create(data.size(), data.empty() ? nullptr : data.data());
}

void Device::SaveStaticPipelineCache() const {
    if (!owns_static_pipeline_cache || !static_pipeline_cache) {
        return;
    }
    const auto filename = StaticPipelineCacheFilename();
    if (filename.empty()) {
        return;
    }
    size_t size = 0;
    std::vector<char> data;
    static_pipeline_cache.Read(&size, nullptr);
    if (size == 0) {
        return;
    }
    data.resize(size);
    static_pipeline_cache.Read(&size, data.data());
    try {
        std::ofstream file(filename, std::ios::binary | std::ios::trunc);
        file.exceptions(std::ofstream::failbit);
        if (!file.is_open()) {
            return;
        }
        const u64 policy_hash = device_policy.policy_hash;
        file.write(STATIC_CACHE_MAGIC_NUMBER.data(), STATIC_CACHE_MAGIC_NUMBER.size())
            .write(reinterpret_cast<const char*>(&STATIC_CACHE_VERSION),
                   sizeof(STATIC_CACHE_VERSION))
            .write(reinterpret_cast<const char*>(&policy_hash), sizeof(policy_hash))
            .write(data.data(), static_cast<std::streamsize>(size));
    } catch (const std::ios_base::failure& e) {
        Common::FS::RemoveFile(filename);
    }
}

VkFormat Device::GetSupportedFormat(VkFormat wanted_format, VkFormatFeatureFlags wanted_usage,
                                    FormatType format_type) const {
    if (IsFormatSupported(wanted_format, wanted_usage, format_type)) {
        return wanted_format;
    }
    // The wanted format is not supported by hardware, search for alternatives
    const VkFormat* alternatives = GetFormatAlternatives(wanted_format);
    if (alternatives == nullptr) {
        LOG_ERROR(Render_Vulkan,
                  "Format={} with usage={} and type={} has no defined alternatives and host "
                  "hardware does not support it",
                  wanted_format, wanted_usage, format_type);
        return wanted_format;
    }

    std::size_t i = 0;
    for (VkFormat alternative = *alternatives; alternative; alternative = alternatives[++i]) {
        if (!IsFormatSupported(alternative, wanted_usage, format_type)) {
            continue;
        }
        LOG_DEBUG(Render_Vulkan,
                  "Emulating format={} with alternative format={} with usage={} and type={}",
                  wanted_format, alternative, wanted_usage, format_type);
        return alternative;
    }

    // No alternatives found, panic
    LOG_ERROR(Render_Vulkan,
              "Format={} with usage={} and type={} is not supported by the host hardware and "
              "doesn't support any of the alternatives",
              wanted_format, wanted_usage, format_type);
    return wanted_format;
}

void Device::ReportLoss() const {
    LOG_CRITICAL(Render_Vulkan, "Device loss occurred!");

    // Wait for the log to flush and for Nsight Aftermath to dump the results
    std::this_thread::sleep_for(std::chrono::seconds{15});
}

void Device::SaveShader(std::span<const u32> spirv) const {
    if (nsight_aftermath_tracker) {
        nsight_aftermath_tracker->SaveShader(spirv);
    }
}

bool Device::ComputeIsOptimalAstcSupported() const {
    static constexpr std::array<VkFormat, 28> astc_formats = {
        VK_FORMAT_ASTC_4x4_UNORM_BLOCK,   VK_FORMAT_ASTC_4x4_SRGB_BLOCK,
        VK_FORMAT_ASTC_5x4_UNORM_BLOCK,   VK_FORMAT_ASTC_5x4_SRGB_BLOCK,
        VK_FORMAT_ASTC_5x5_UNORM_BLOCK,   VK_FORMAT_ASTC_5x5_SRGB_BLOCK,
        VK_FORMAT_ASTC_6x5_UNORM_BLOCK,   VK_FORMAT_ASTC_6x5_SRGB_BLOCK,
        VK_FORMAT_ASTC_6x6_UNORM_BLOCK,   VK_FORMAT_ASTC_6x6_SRGB_BLOCK,
        VK_FORMAT_ASTC_8x5_UNORM_BLOCK,   VK_FORMAT_ASTC_8x5_SRGB_BLOCK,
        VK_FORMAT_ASTC_8x6_UNORM_BLOCK,   VK_FORMAT_ASTC_8x6_SRGB_BLOCK,
        VK_FORMAT_ASTC_8x8_UNORM_BLOCK,   VK_FORMAT_ASTC_8x8_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x5_UNORM_BLOCK,  VK_FORMAT_ASTC_10x5_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x6_UNORM_BLOCK,  VK_FORMAT_ASTC_10x6_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x8_UNORM_BLOCK,  VK_FORMAT_ASTC_10x8_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x10_UNORM_BLOCK, VK_FORMAT_ASTC_10x10_SRGB_BLOCK,
        VK_FORMAT_ASTC_12x10_UNORM_BLOCK, VK_FORMAT_ASTC_12x10_SRGB_BLOCK,
        VK_FORMAT_ASTC_12x12_UNORM_BLOCK, VK_FORMAT_ASTC_12x12_SRGB_BLOCK,
    };
    if (!features.features.textureCompressionASTC_LDR) {
        return false;
    }
    const VkFormatFeatureFlags format_feature_usage{
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT};
    for (const auto format : astc_formats) {
        const auto physical_format_properties{physical.GetFormatProperties(format)};
        if ((physical_format_properties.optimalTilingFeatures & format_feature_usage) !=
            format_feature_usage) {
            return false;
        }
    }
    return true;
}

bool Device::TestDepthStencilBlits(VkFormat format) const {
    static constexpr VkFormatFeatureFlags required_features =
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
    const auto test_features = [](VkFormatProperties props) {
        return (props.optimalTilingFeatures & required_features) == required_features;
    };
    return test_features(format_properties.at(format));
}

bool Device::IsFormatSupported(VkFormat wanted_format, VkFormatFeatureFlags wanted_usage,
                               FormatType format_type) const {
    const auto it = format_properties.find(wanted_format);
    if (it == format_properties.end()) {
        UNIMPLEMENTED_MSG("Unimplemented format query={}", wanted_format);
        return true;
    }
    const auto supported_usage = GetFormatFeatures(it->second, format_type);
    return (supported_usage & wanted_usage) == wanted_usage;
}

bool Device::IsOptimalBcnSupported(VkFormat format) const {
    if (!device_policy.xclipse.detected) {
        return features.features.textureCompressionBC;
    }

    const auto it = std::ranges::find(BCN_FORMATS, format);
    if (it == BCN_FORMATS.end()) {
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(std::distance(BCN_FORMATS.begin(), it));
    return SupportsXclipseRuntimeNativeBcnPath(format, device_policy.capabilities.bcn[index]);
}

std::string Device::GetDriverName() const {
    return vk::GetDriverName(properties.driver);
}

bool Device::ShouldBoostClocks() const {
    const auto driver_id = properties.driver.driverID;
    const auto vendor_id = properties.properties.vendorID;
    const auto device_id = properties.properties.deviceID;

    const bool validated_driver =
        driver_id == VK_DRIVER_ID_AMD_PROPRIETARY || driver_id == VK_DRIVER_ID_AMD_OPEN_SOURCE ||
        driver_id == VK_DRIVER_ID_MESA_RADV || driver_id == VK_DRIVER_ID_NVIDIA_PROPRIETARY ||
        driver_id == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS ||
        driver_id == VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA ||
        driver_id == VK_DRIVER_ID_QUALCOMM_PROPRIETARY || driver_id == VK_DRIVER_ID_MESA_TURNIP ||
        driver_id == VK_DRIVER_ID_SAMSUNG_PROPRIETARY;

    const bool is_steam_deck = (vendor_id == 0x1002 && device_id == 0x163F) ||
                               (vendor_id == 0x1002 && device_id == 0x1435);

    const bool is_debugging = this->HasDebuggingToolAttached();

    return validated_driver && !is_steam_deck && !is_debugging;
}

bool Device::HasTimelineSemaphore() const {
    if (GetDriverID() == VK_DRIVER_ID_MESA_TURNIP) {
        return false;
    }
    return features.timeline_semaphore.timelineSemaphore;
}

bool Device::MustEmulateBGR565() const {
    return Settings::values.emulate_bgr565.GetValue();
}

bool Device::GetSuitability(bool requires_swapchain) {
    // Assume we will be suitable.
    bool suitable = true;

    // Configure properties.
    VkPhysicalDeviceVulkan12Features features_1_2{};
    VkPhysicalDeviceVulkan13Features features_1_3{};

    // Configure properties.
    properties.properties = physical.GetProperties();

    // Set instance version.
    instance_version = properties.properties.apiVersion;

    // Minimum of API version 1.1 is required. (This is well-supported.)
    ASSERT(instance_version >= VK_API_VERSION_1_1);

    // Get available extensions.
    auto extension_properties = physical.EnumerateDeviceExtensionProperties();

    // Get the set of supported extensions.
    supported_extensions.clear();
    for (const VkExtensionProperties& property : extension_properties) {
        supported_extensions.insert(property.extensionName);
    }

    // Generate list of extensions to load.
    loaded_extensions.clear();

#define EXTENSION(prefix, macro_name, var_name)                                                    \
    if (supported_extensions.contains(VK_##prefix##_##macro_name##_EXTENSION_NAME)) {              \
            loaded_extensions.insert(VK_##prefix##_##macro_name##_EXTENSION_NAME);                     \
            extensions.var_name = true;                                                                \
    }
#define FEATURE_EXTENSION(prefix, struct_name, macro_name, var_name)                               \
    if (supported_extensions.contains(VK_##prefix##_##macro_name##_EXTENSION_NAME)) {              \
            loaded_extensions.insert(VK_##prefix##_##macro_name##_EXTENSION_NAME);                     \
            extensions.var_name = true;                                                                \
    }

    if (instance_version < VK_API_VERSION_1_2) {
        FOR_EACH_VK_FEATURE_1_2(FEATURE_EXTENSION);
    }
    if (instance_version < VK_API_VERSION_1_3) {
        FOR_EACH_VK_FEATURE_1_3(FEATURE_EXTENSION);
    }

    FOR_EACH_VK_FEATURE_EXT(FEATURE_EXTENSION);
    FOR_EACH_VK_EXTENSION(EXTENSION);

    extensions.depth_stencil_resolve =
        extensions.depth_stencil_resolve &&
        (instance_version >= VK_API_VERSION_1_2 || extensions.create_renderpass2);
    RemoveExtensionIfUnsuitable(extensions.depth_stencil_resolve,
                                VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);

    if (supported_extensions.contains(VK_KHR_ROBUSTNESS_2_EXTENSION_NAME)) {
        loaded_extensions.erase(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
        loaded_extensions.insert(VK_KHR_ROBUSTNESS_2_EXTENSION_NAME);
        extensions.robustness_2 = true;
    } else if (supported_extensions.contains(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME)) {
        loaded_extensions.insert(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
        extensions.robustness_2 = true;
    } else {
        extensions.robustness_2 = false;
    }

#undef FEATURE_EXTENSION
#undef EXTENSION

// Some extensions are mandatory. Check those.
#define CHECK_EXTENSION(extension_name)                                                            \
    if (!loaded_extensions.contains(extension_name)) {                                             \
            LOG_ERROR(Render_Vulkan, "Missing required extension {}", extension_name);                 \
            suitable = false;                                                                          \
    }

#define LOG_EXTENSION(extension_name)                                                              \
    if (!loaded_extensions.contains(extension_name)) {                                             \
            LOG_INFO(Render_Vulkan, "Device doesn't support extension {}", extension_name);            \
    }

    FOR_EACH_VK_RECOMMENDED_EXTENSION(LOG_EXTENSION);
    FOR_EACH_VK_MANDATORY_EXTENSION(CHECK_EXTENSION);

    if (requires_swapchain) {
        CHECK_EXTENSION(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    if (instance_version < VK_API_VERSION_1_2) {
        CHECK_EXTENSION(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    }

#undef LOG_EXTENSION
#undef CHECK_EXTENSION

    // Generate the linked list of features to test.
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    // Set next pointer.
    void** next = &features2.pNext;

    // Vulkan 1.2 and 1.3 features
    if (instance_version >= VK_API_VERSION_1_2) {
        features_1_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features_1_3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

        features_1_2.pNext = &features_1_3;

        *next = &features_1_2;
    }

// Test all features we know about. If the feature is not available in core at our
// current API version, and was not enabled by an extension, skip testing the feature.
// We set the structure sType explicitly here as it is zeroed by the constructor.
#define FEATURE(prefix, struct_name, macro_name, var_name)                                         \
    features.var_name.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_##macro_name##_FEATURES;           \
        SetNext(next, features.var_name);

#define EXT_FEATURE(prefix, struct_name, macro_name, var_name)                                     \
    if (extensions.var_name) {                                                                     \
            features.var_name.sType =                                                                  \
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_##macro_name##_FEATURES_##prefix;                    \
            SetNext(next, features.var_name);                                                          \
    }

    FOR_EACH_VK_FEATURE_1_1(FEATURE);
    FOR_EACH_VK_FEATURE_EXT(EXT_FEATURE);
    if (instance_version >= VK_API_VERSION_1_2) {
        FOR_EACH_VK_FEATURE_1_2(FEATURE);
    } else {
        FOR_EACH_VK_FEATURE_1_2(EXT_FEATURE);
    }
    if (instance_version >= VK_API_VERSION_1_3) {
        FOR_EACH_VK_FEATURE_1_3(FEATURE);
    } else {
        FOR_EACH_VK_FEATURE_1_3(EXT_FEATURE);
    }

#undef EXT_FEATURE
#undef FEATURE

    // Perform the feature test.
    physical.GetFeatures2(features2);

    // Base Vulkan 1.0 features are always valid regardless of instance version.
    features.features = features2.features;

// Some features are mandatory. Check those.
#define CHECK_FEATURE(feature, name)                                                               \
    if (!features.feature.name) {                                                                  \
        if (IsMoltenVK() && (strcmp(#name, "geometryShader") == 0 ||                               \
                            strcmp(#name, "logicOp") == 0 ||                                       \
                            strcmp(#name, "shaderCullDistance") == 0 ||                            \
                            strcmp(#name, "wideLines") == 0)) {                                    \
            LOG_INFO(Render_Vulkan, "MoltenVK missing feature {} - using fallback", #name);       \
        } else {                                                                                    \
            LOG_ERROR(Render_Vulkan, "Missing required feature {}", #name);                        \
            suitable = false;                                                                       \
        }                                                                                           \
    }

#define LOG_FEATURE(feature, name)                                                                 \
    if (!features.feature.name) {                                                                  \
            LOG_INFO(Render_Vulkan, "Device doesn't support feature {}", #name);                       \
    }

// Optional features are enabled silently without any logging
#define OPTIONAL_FEATURE(feature, name) (void)features.feature.name;

    FOR_EACH_VK_OPTIONAL_FEATURE(OPTIONAL_FEATURE);
    FOR_EACH_VK_RECOMMENDED_FEATURE(LOG_FEATURE);
    FOR_EACH_VK_MANDATORY_FEATURE(CHECK_FEATURE);

#undef OPTIONAL_FEATURE
#undef LOG_FEATURE
#undef CHECK_FEATURE

    // Generate linked list of properties.
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

    // Set next pointer.
    next = &properties2.pNext;

    // Get driver info.
    properties.driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    SetNext(next, properties.driver);

    // Retrieve subgroup properties.
    properties.subgroup_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
    SetNext(next, properties.subgroup_properties);

    // Retrieve relevant extension properties.
    if (extensions.shader_float_controls) {
        properties.float_controls.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES;
        SetNext(next, properties.float_controls);
    }
    if (extensions.push_descriptor) {
        properties.push_descriptor.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR;
        SetNext(next, properties.push_descriptor);
    }
    if (extensions.depth_stencil_resolve || instance_version >= VK_API_VERSION_1_2) {
        properties.depth_stencil_resolve.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES;
        SetNext(next, properties.depth_stencil_resolve);
    }
    if (extensions.descriptor_buffer) {
        properties.descriptor_buffer.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;
        SetNext(next, properties.descriptor_buffer);
    }
    if (extensions.subgroup_size_control || features.subgroup_size_control.subgroupSizeControl) {
        properties.subgroup_size_control.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES;
        SetNext(next, properties.subgroup_size_control);
    }
    if (extensions.transform_feedback) {
        properties.transform_feedback.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT;
        SetNext(next, properties.transform_feedback);
    }
    if (extensions.maintenance5) {
        properties.maintenance5.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_PROPERTIES_KHR;
        SetNext(next, properties.maintenance5);
    }
    if (extensions.custom_border_color) {
        properties.custom_border_color.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_PROPERTIES_EXT;
        SetNext(next, properties.custom_border_color);
    }

    // Perform the property fetch.
    physical.GetProperties2(properties2);

    // Store base properties
    properties.properties = properties2.properties;

    // Unload extensions if feature support is insufficient.
    RemoveUnsuitableExtensions();

    // Check limits.
    struct Limit {
        u32 minimum;
        u32 value;
        const char* name;
    };

    const VkPhysicalDeviceLimits& limits{properties.properties.limits};
    const std::array limits_report{
                                   Limit{65536, limits.maxUniformBufferRange, "maxUniformBufferRange"},
                                   Limit{16, limits.maxViewports, "maxViewports"},
                                   Limit{8, limits.maxColorAttachments, "maxColorAttachments"},
                                   Limit{8, limits.maxClipDistances, "maxClipDistances"},
                                   };

    for (const auto& [min, value, name] : limits_report) {
        if (value < min) {
            LOG_ERROR(Render_Vulkan, "{} has to be {} or greater but it is {}", name, min, value);
            suitable = false;
        }
    }

    // VK_DYNAMIC_STATE

    // Driver detection variables for workarounds in GetSuitability
    const VkDriverId driver_id = properties.driver.driverID;

    // VK_EXT_extended_dynamic_state2 below this will appear drivers that need workarounds.

    // VK_EXT_extended_dynamic_state3 below this will appear drivers that need workarounds.

    // Samsung: Broken extendedDynamicState3ColorBlendEquation
    // Disable blend equation dynamic state, force static pipeline state
    if (extensions.extended_dynamic_state3 &&
        (driver_id == VK_DRIVER_ID_SAMSUNG_PROPRIETARY)) {
        LOG_WARNING(Render_Vulkan,
                    "Samsung: Disabling broken extendedDynamicState3ColorBlendEquation");
        features.extended_dynamic_state3.extendedDynamicState3ColorBlendEnable = false;
        features.extended_dynamic_state3.extendedDynamicState3ColorBlendEquation = false;
    }

    // Intel Windows < 27.20.100.0: Broken VertexInputDynamicState
    // Same for NVIDIA Proprietary < 580.119.02, unknown when VIDS was first NOT broken
    // Disable VertexInputDynamicState on old Intel Windows drivers
    if (extensions.vertex_input_dynamic_state) {
        const u32 version = (properties.properties.driverVersion << 3) >> 3;
        if ((driver_id == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS && version < VK_MAKE_API_VERSION(27, 20, 100, 0))
        || (driver_id == VK_DRIVER_ID_NVIDIA_PROPRIETARY && version < VK_MAKE_API_VERSION(580, 119, 02, 0))) {
            LOG_WARNING(Render_Vulkan, "Disabling broken VK_EXT_vertex_input_dynamic_state");
            RemoveExtensionFeature(extensions.vertex_input_dynamic_state, features.vertex_input_dynamic_state, VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME);
        }
    }

    if (u32(Settings::values.dyna_state.GetValue()) == 0) {
        LOG_INFO(Render_Vulkan, "Extended Dynamic State disabled by user setting, clearing all EDS features");
        features.extended_dynamic_state.extendedDynamicState = false;
        features.extended_dynamic_state2.extendedDynamicState2 = false;
        features.extended_dynamic_state3.extendedDynamicState3ColorBlendEnable = false;
        features.extended_dynamic_state3.extendedDynamicState3ColorBlendEquation = false;
        features.extended_dynamic_state3.extendedDynamicState3ColorWriteMask = false;
        features.extended_dynamic_state3.extendedDynamicState3DepthClampEnable = false;
        features.extended_dynamic_state3.extendedDynamicState3LogicOpEnable = false;
    }

    // Return whether we were suitable.
    return suitable;
}

void Device::RemoveUnsuitableExtensions() {
    // VK_EXT_color_write_enable
    extensions.color_write_enable = features.color_write_enable.colorWriteEnable;
    RemoveExtensionFeatureIfUnsuitable(extensions.color_write_enable, features.color_write_enable,
                                       VK_EXT_COLOR_WRITE_ENABLE_EXTENSION_NAME);

    // VK_EXT_custom_border_color
    if (extensions.custom_border_color) {
        extensions.custom_border_color =
            features.custom_border_color.customBorderColors &&
            features.custom_border_color.customBorderColorWithoutFormat;
    }
    RemoveExtensionFeatureIfUnsuitable(extensions.custom_border_color, features.custom_border_color,
                                       VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME);

    // VK_EXT_border_color_swizzle
    if (extensions.border_color_swizzle) {
        extensions.border_color_swizzle =
            extensions.custom_border_color && features.border_color_swizzle.borderColorSwizzle;
    }
    RemoveExtensionFeatureIfUnsuitable(extensions.border_color_swizzle,
                                       features.border_color_swizzle,
                                       VK_EXT_BORDER_COLOR_SWIZZLE_EXTENSION_NAME);

    // VK_EXT_depth_bias_control
    extensions.depth_bias_control =
        features.depth_bias_control.depthBiasControl &&
        features.depth_bias_control.leastRepresentableValueForceUnormRepresentation;
    RemoveExtensionFeatureIfUnsuitable(extensions.depth_bias_control, features.depth_bias_control,
                                       VK_EXT_DEPTH_BIAS_CONTROL_EXTENSION_NAME);

    // VK_EXT_depth_clip_control
    extensions.depth_clip_control = features.depth_clip_control.depthClipControl;
    RemoveExtensionFeatureIfUnsuitable(extensions.depth_clip_control, features.depth_clip_control,
                                       VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME);

    // VK_EXT_descriptor_buffer
    extensions.descriptor_buffer = features.descriptor_buffer.descriptorBuffer;
    RemoveExtensionFeatureIfUnsuitable(extensions.descriptor_buffer, features.descriptor_buffer,
                                       VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);

    // VK_EXT_extended_dynamic_state
    extensions.extended_dynamic_state = features.extended_dynamic_state.extendedDynamicState;
    RemoveExtensionFeatureIfUnsuitable(extensions.extended_dynamic_state,
                                       features.extended_dynamic_state,
                                       VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);

    // VK_EXT_extended_dynamic_state2
    extensions.extended_dynamic_state2 = features.extended_dynamic_state2.extendedDynamicState2;
    RemoveExtensionFeatureIfUnsuitable(extensions.extended_dynamic_state2,
                                       features.extended_dynamic_state2,
                                       VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME);

    // VK_EXT_extended_dynamic_state3
    const bool supports_color_blend_enable =
        features.extended_dynamic_state3.extendedDynamicState3ColorBlendEnable;
    const bool supports_color_blend_equation =
        features.extended_dynamic_state3.extendedDynamicState3ColorBlendEquation;
    const bool supports_color_write_mask =
        features.extended_dynamic_state3.extendedDynamicState3ColorWriteMask;
    dynamic_state3_blending = supports_color_blend_enable && supports_color_blend_equation &&
                              supports_color_write_mask;

    const bool supports_depth_clamp_enable =
        features.extended_dynamic_state3.extendedDynamicState3DepthClampEnable;
    const bool supports_logic_op_enable =
        features.extended_dynamic_state3.extendedDynamicState3LogicOpEnable;
    const bool supports_line_raster_mode =
        features.extended_dynamic_state3.extendedDynamicState3LineRasterizationMode &&
        extensions.line_rasterization && features.line_rasterization.rectangularLines;
    const bool supports_conservative_raster_mode =
        features.extended_dynamic_state3.extendedDynamicState3ConservativeRasterizationMode &&
        extensions.conservative_rasterization;
    const bool supports_line_stipple_enable =
        features.extended_dynamic_state3.extendedDynamicState3LineStippleEnable &&
        extensions.line_rasterization && features.line_rasterization.stippledRectangularLines;
    const bool supports_alpha_to_coverage =
        features.extended_dynamic_state3.extendedDynamicState3AlphaToCoverageEnable;
    const bool supports_alpha_to_one =
        features.extended_dynamic_state3.extendedDynamicState3AlphaToOneEnable &&
        features.features.alphaToOne;

    dynamic_state3_depth_clamp_enable = supports_depth_clamp_enable;
    dynamic_state3_logic_op_enable = supports_logic_op_enable;
    dynamic_state3_line_raster_mode = supports_line_raster_mode;
    dynamic_state3_conservative_raster_mode = supports_conservative_raster_mode;
    dynamic_state3_line_stipple_enable = supports_line_stipple_enable;
    dynamic_state3_alpha_to_coverage = supports_alpha_to_coverage;
    dynamic_state3_alpha_to_one = supports_alpha_to_one;

    dynamic_state3_enables = dynamic_state3_depth_clamp_enable || dynamic_state3_logic_op_enable ||
                             dynamic_state3_line_raster_mode ||
                             dynamic_state3_conservative_raster_mode ||
                             dynamic_state3_line_stipple_enable ||
                             dynamic_state3_alpha_to_coverage || dynamic_state3_alpha_to_one;

    extensions.extended_dynamic_state3 = dynamic_state3_blending || dynamic_state3_enables;
    if (!extensions.extended_dynamic_state3) {
        dynamic_state3_blending = false;
        dynamic_state3_enables = false;
        dynamic_state3_depth_clamp_enable = false;
        dynamic_state3_logic_op_enable = false;
        dynamic_state3_line_raster_mode = false;
        dynamic_state3_conservative_raster_mode = false;
        dynamic_state3_line_stipple_enable = false;
        dynamic_state3_alpha_to_coverage = false;
        dynamic_state3_alpha_to_one = false;
    }
    RemoveExtensionFeatureIfUnsuitable(extensions.extended_dynamic_state3,
                                       features.extended_dynamic_state3,
                                       VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);

    // VK_EXT_robustness2
    features.robustness2.robustBufferAccess2 = VK_FALSE;
    features.robustness2.robustImageAccess2 = VK_FALSE;
    extensions.robustness_2 = features.robustness2.nullDescriptor;

    const char* robustness2_extension_name =
        loaded_extensions.contains(VK_KHR_ROBUSTNESS_2_EXTENSION_NAME)
            ? VK_KHR_ROBUSTNESS_2_EXTENSION_NAME
            : VK_EXT_ROBUSTNESS_2_EXTENSION_NAME;

    RemoveExtensionFeatureIfUnsuitable(extensions.robustness_2, features.robustness2,
                                       robustness2_extension_name);

    // Image robustness
    extensions.robust_image_access = features.robust_image_access.robustImageAccess;
    RemoveExtensionFeatureIfUnsuitable(extensions.robust_image_access,
                                       features.robust_image_access,
                                       VK_EXT_IMAGE_ROBUSTNESS_EXTENSION_NAME);

    // VK_KHR_shader_atomic_int64
    extensions.shader_atomic_int64 = features.shader_atomic_int64.shaderBufferInt64Atomics &&
                                     features.shader_atomic_int64.shaderSharedInt64Atomics;
    RemoveExtensionFeatureIfUnsuitable(extensions.shader_atomic_int64, features.shader_atomic_int64,
                                       VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME);

    // VK_EXT_shader_demote_to_helper_invocation
    extensions.shader_demote_to_helper_invocation =
        features.shader_demote_to_helper_invocation.shaderDemoteToHelperInvocation;
    RemoveExtensionFeatureIfUnsuitable(extensions.shader_demote_to_helper_invocation,
                                       features.shader_demote_to_helper_invocation,
                                       VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME);

    // VK_EXT_subgroup_size_control
    extensions.subgroup_size_control =
        features.subgroup_size_control.subgroupSizeControl &&
        properties.subgroup_size_control.minSubgroupSize <= GuestWarpSize &&
        properties.subgroup_size_control.maxSubgroupSize >= GuestWarpSize;
    RemoveExtensionFeatureIfUnsuitable(extensions.subgroup_size_control,
                                       features.subgroup_size_control,
                                       VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME);

    // VK_EXT_transform_feedback
    extensions.transform_feedback =
        features.transform_feedback.transformFeedback &&
        properties.transform_feedback.maxTransformFeedbackBuffers > 0;
    RemoveExtensionFeatureIfUnsuitable(extensions.transform_feedback, features.transform_feedback,
                                       VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME);

    // VK_EXT_vertex_input_dynamic_state
    extensions.vertex_input_dynamic_state =
        features.vertex_input_dynamic_state.vertexInputDynamicState;
    RemoveExtensionFeatureIfUnsuitable(extensions.vertex_input_dynamic_state,
                                       features.vertex_input_dynamic_state,
                                       VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME);

    // VK_KHR_pipeline_executable_properties
    if (Settings::values.renderer_shader_feedback.GetValue()) {
        extensions.pipeline_executable_properties =
            features.pipeline_executable_properties.pipelineExecutableInfo;
        RemoveExtensionFeatureIfUnsuitable(extensions.pipeline_executable_properties,
                                           features.pipeline_executable_properties,
                                           VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME);
    } else {
        RemoveExtensionFeature(extensions.pipeline_executable_properties,
                               features.pipeline_executable_properties,
                               VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME);
    }

    // VK_KHR_shader_quad_control
    extensions.shader_quad_control = features.shader_quad_control.shaderQuadControl;
    RemoveExtensionFeatureIfUnsuitable(extensions.shader_quad_control, features.shader_quad_control,
                                       VK_KHR_SHADER_QUAD_CONTROL_EXTENSION_NAME);

    // VK_KHR_workgroup_memory_explicit_layout
    extensions.workgroup_memory_explicit_layout =
        features.workgroup_memory_explicit_layout.workgroupMemoryExplicitLayout &&
        features.workgroup_memory_explicit_layout.workgroupMemoryExplicitLayoutScalarBlockLayout;
    RemoveExtensionFeatureIfUnsuitable(extensions.workgroup_memory_explicit_layout,
                                       features.workgroup_memory_explicit_layout,
                                       VK_KHR_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_EXTENSION_NAME);

    // VK_KHR_maintenance1
    extensions.maintenance1 = loaded_extensions.contains(VK_KHR_MAINTENANCE_1_EXTENSION_NAME);
    RemoveExtensionIfUnsuitable(extensions.maintenance1, VK_KHR_MAINTENANCE_1_EXTENSION_NAME);

    // VK_KHR_maintenance2
    extensions.maintenance2 = loaded_extensions.contains(VK_KHR_MAINTENANCE_2_EXTENSION_NAME);
    RemoveExtensionIfUnsuitable(extensions.maintenance2, VK_KHR_MAINTENANCE_2_EXTENSION_NAME);

    // VK_KHR_maintenance3
    extensions.maintenance3 = loaded_extensions.contains(VK_KHR_MAINTENANCE_3_EXTENSION_NAME);
    RemoveExtensionIfUnsuitable(extensions.maintenance3, VK_KHR_MAINTENANCE_3_EXTENSION_NAME);

    // VK_KHR_maintenance4
    extensions.maintenance4 = features.maintenance4.maintenance4;
    RemoveExtensionFeatureIfUnsuitable(extensions.maintenance4, features.maintenance4,
                                       VK_KHR_MAINTENANCE_4_EXTENSION_NAME);

    // VK_KHR_maintenance5
    extensions.maintenance5 = features.maintenance5.maintenance5;
    RemoveExtensionFeatureIfUnsuitable(extensions.maintenance5, features.maintenance5,
                                       VK_KHR_MAINTENANCE_5_EXTENSION_NAME);

    // VK_KHR_maintenance6
    extensions.maintenance6 = features.maintenance6.maintenance6;
    RemoveExtensionFeatureIfUnsuitable(extensions.maintenance6, features.maintenance6,
                                       VK_KHR_MAINTENANCE_6_EXTENSION_NAME);

    // VK_KHR_maintenance7
    extensions.maintenance7 = loaded_extensions.contains(VK_KHR_MAINTENANCE_7_EXTENSION_NAME);
    RemoveExtensionIfUnsuitable(extensions.maintenance7, VK_KHR_MAINTENANCE_7_EXTENSION_NAME);

    // VK_KHR_maintenance8
    extensions.maintenance8 = loaded_extensions.contains(VK_KHR_MAINTENANCE_8_EXTENSION_NAME);
    RemoveExtensionIfUnsuitable(extensions.maintenance8, VK_KHR_MAINTENANCE_8_EXTENSION_NAME);

    // VK_KHR_synchronization2
    extensions.synchronization2 = features.synchronization2.synchronization2;
    RemoveExtensionFeatureIfUnsuitable(extensions.synchronization2, features.synchronization2,
                                       VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
}

void Device::SetupFamilies(VkSurfaceKHR surface) {
    const std::vector queue_family_properties = physical.GetQueueFamilyProperties();
    std::optional<u32> graphics;
    std::optional<u32> present;
    for (u32 index = 0; index < static_cast<u32>(queue_family_properties.size()); ++index) {
        if (graphics && (present || !surface)) {
            break;
        }
        const VkQueueFamilyProperties& queue_family = queue_family_properties[index];
        if (queue_family.queueCount == 0) {
            continue;
        }
        if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics = index;
        }
        if (surface && physical.GetSurfaceSupportKHR(index, surface)) {
            present = index;
        }
    }
    if (!graphics) {
        LOG_ERROR(Render_Vulkan, "Device lacks a graphics queue");
        throw vk::Exception(VK_ERROR_FEATURE_NOT_PRESENT);
    }
    if (surface && !present) {
        LOG_ERROR(Render_Vulkan, "Device lacks a present queue");
        throw vk::Exception(VK_ERROR_FEATURE_NOT_PRESENT);
    }
    if (graphics) {
        graphics_family = *graphics;
        graphics_family_sparse_binding =
            (queue_family_properties[*graphics].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) != 0;
    }
    if (present) {
        present_family = *present;
    }
}

bool Device::TryReserveCustomBorderColorSamplers(size_t count) const {
    const size_t limit = properties.custom_border_color.maxCustomBorderColorSamplers;
    if (limit == 0) {
        return true;
    }
    size_t used = custom_border_color_samplers_used.load(std::memory_order_relaxed);
    while (used + count <= limit) {
        if (custom_border_color_samplers_used.compare_exchange_weak(
                used, used + count, std::memory_order_relaxed, std::memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

void Device::ReleaseCustomBorderColorSamplers(size_t count) const {
    if (count == 0) {
        return;
    }
    custom_border_color_samplers_used.fetch_sub(count, std::memory_order_relaxed);
}

u64 Device::GetDeviceMemoryUsage() const {
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget;
    budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
    budget.pNext = nullptr;
    physical.GetMemoryProperties(&budget);
    u64 result{};
    for (const size_t heap : valid_heap_memory) {
        result += budget.heapUsage[heap];
    }
    return result;
}

void Device::CollectPhysicalMemoryInfo() {
    // Calculate limits using memory budget
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
    budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
    const auto mem_info =
        physical.GetMemoryProperties(extensions.memory_budget ? &budget : nullptr);
    const auto& mem_properties = mem_info.memoryProperties;
    const size_t num_properties = mem_properties.memoryHeapCount;
    device_access_memory = 0;
    u64 device_initial_usage = 0;
    u64 local_memory = 0;
    for (size_t element = 0; element < num_properties; ++element) {
        const bool is_heap_local =
            (mem_properties.memoryHeaps[element].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
        if (!is_integrated && !is_heap_local) {
            continue;
        }
        valid_heap_memory.push_back(element);
        if (is_heap_local) {
            local_memory += mem_properties.memoryHeaps[element].size;
        }
        if (extensions.memory_budget) {
            device_initial_usage += budget.heapUsage[element];
            device_access_memory += budget.heapBudget[element];
            continue;
        }
        device_access_memory += mem_properties.memoryHeaps[element].size;
    }
    if (is_integrated) {
        const s64 available_memory = static_cast<s64>(device_access_memory - device_initial_usage);
        const u64 memory_size = Settings::values.vram_usage_mode.GetValue() == Settings::VramUsageMode::Aggressive ? 6_GiB : 4_GiB;
        device_access_memory = static_cast<u64>(std::max<s64>(std::min<s64>(available_memory - 8_GiB, memory_size), std::min<s64>(local_memory, memory_size)));
    } else {
        const u64 reserve_memory = std::min<u64>(device_access_memory / 8, 1_GiB);
        device_access_memory -= reserve_memory;
        if (Settings::values.vram_usage_mode.GetValue() != Settings::VramUsageMode::Aggressive) {
            // Account for resolution scaling in memory limits
            const size_t normal_memory = 6_GiB;
            const size_t scaler_memory = 1_GiB * Settings::values.resolution_info.ScaleUp(1);
            device_access_memory = std::min<u64>(device_access_memory, normal_memory + scaler_memory);
        }
    }
}

void Device::CollectToolingInfo() {
    if (!extensions.tooling_info) {
        return;
    }
    auto tools{physical.GetPhysicalDeviceToolProperties()};
    for (const VkPhysicalDeviceToolProperties& tool : tools) {
        const std::string_view name = tool.name;
        LOG_INFO(Render_Vulkan, "Attached debugging tool: {}", name);
        has_renderdoc = has_renderdoc || name == "RenderDoc";
        has_nsight_graphics = has_nsight_graphics || name == "NVIDIA Nsight Graphics";
        has_radeon_gpu_profiler = has_radeon_gpu_profiler || name == "Radeon GPU Profiler";
    }
#ifdef _WIN32
    if (has_renderdoc) {
        LOG_INFO(Render_Vulkan,
                 "Windows default RenderDoc output folder: %LOCALAPPDATA%\\Temp\\RenderDoc");
    }
#endif
}

std::vector<VkDeviceQueueCreateInfo> Device::GetDeviceQueueCreateInfos() const {
    static constexpr float QUEUE_PRIORITY = 1.0f;

    ::Common::unordered_set<u32> unique_queue_families{graphics_family, present_family};
    std::vector<VkDeviceQueueCreateInfo> queue_cis;
    queue_cis.reserve(unique_queue_families.size());

    for (const u32 queue_family : unique_queue_families) {
        auto& ci = queue_cis.emplace_back(VkDeviceQueueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = queue_family,
            .queueCount = 1,
            .pQueuePriorities = nullptr,
        });
        ci.pQueuePriorities = &QUEUE_PRIORITY;
    }

    return queue_cis;
}

void Device::InitializeGPULogging() {
    // Get log level from settings - Off is the disable.
    const auto log_level = static_cast<GPU::Logging::LogLevel>(
        static_cast<u32>(Settings::values.gpu_log_level.GetValue()));
    if (log_level == GPU::Logging::LogLevel::Off) {
        return;
    }

    // Detect driver type
    const auto driver_id = GetDriverID();
    GPU::Logging::DriverType detected_driver = GPU::Logging::DriverType::Unknown;

    if (driver_id == VK_DRIVER_ID_MESA_TURNIP) {
        detected_driver = GPU::Logging::DriverType::Turnip;
    } else if (driver_id == VK_DRIVER_ID_QUALCOMM_PROPRIETARY) {
        detected_driver = GPU::Logging::DriverType::Qualcomm;
    }

    // Initialize GPU logger
    GPU::Logging::GPULogger::GetInstance().Initialize(log_level, detected_driver);

    // Configure feature flags
    GPU::Logging::GPULogger::GetInstance().EnableVulkanCallTracking(
        Settings::values.gpu_log_vulkan_calls.GetValue());
    GPU::Logging::GPULogger::GetInstance().EnableMemoryTracking(
        Settings::values.gpu_log_memory_tracking.GetValue());
    GPU::Logging::GPULogger::GetInstance().EnableDriverDebugInfo(
        Settings::values.gpu_log_driver_debug.GetValue());
    GPU::Logging::GPULogger::GetInstance().SetRingBufferSize(
        Settings::values.gpu_log_ring_buffer_size.GetValue());

    // Log comprehensive driver and extension information
    if (Settings::values.gpu_log_driver_debug.GetValue()) {
        std::string driver_info;

        // Device information
        const auto& props = properties.properties;
        driver_info += fmt::format("Device: {}\n", props.deviceName);
        driver_info += fmt::format("Driver Name: {}\n", properties.driver.driverName);
        driver_info += fmt::format("Driver Info: {}\n", properties.driver.driverInfo);

        // Version information
        const u32 driver_version = props.driverVersion;
        const u32 api_version = props.apiVersion;
        driver_info += fmt::format("Driver Version: {}.{}.{}\n",
            VK_API_VERSION_MAJOR(driver_version),
            VK_API_VERSION_MINOR(driver_version),
            VK_API_VERSION_PATCH(driver_version));
        driver_info += fmt::format("Vulkan API Version: {}.{}.{}\n",
            VK_API_VERSION_MAJOR(api_version),
            VK_API_VERSION_MINOR(api_version),
            VK_API_VERSION_PATCH(api_version));
        driver_info += fmt::format("Driver ID: {}\n", static_cast<u32>(driver_id));

        // Vendor and device IDs
        driver_info += fmt::format("Vendor ID: {:#04x}\n", props.vendorID);
        driver_info += fmt::format("Device ID: {:#04x}\n", props.deviceID);

        // Extensions - separate QCOM extensions from others
        driver_info += "\n=== Loaded Vulkan Extensions ===\n";
        std::vector<std::string> qcom_exts;
        std::vector<std::string> other_exts;

        for (const auto& ext : loaded_extensions) {
            if (ext.find("QCOM") != std::string::npos || ext.find("qcom") != std::string::npos) {
                qcom_exts.push_back(ext);
            } else {
                other_exts.push_back(ext);
            }
        }

        // Log QCOM extensions first
        if (!qcom_exts.empty()) {
            driver_info += "\nQualcomm Proprietary Extensions:\n";
            for (const auto& ext : qcom_exts) {
                driver_info += fmt::format("  - {}\n", ext);
            }
        }

        // Log other extensions
        if (!other_exts.empty()) {
            driver_info += "\nStandard Extensions:\n";
            for (const auto& ext : other_exts) {
                driver_info += fmt::format("  - {}\n", ext);
            }
        }

        driver_info += fmt::format("\nTotal Extensions Loaded: {}\n", loaded_extensions.size());

        GPU::Logging::GPULogger::GetInstance().LogDriverDebugInfo(driver_info);
    }
}

void Device::ShutdownGPULogging() {
    if (GPU::Logging::GPULogger::GetInstance().IsInitialized()) {
        GPU::Logging::GPULogger::GetInstance().Shutdown();
    }
}

} // namespace Vulkan
