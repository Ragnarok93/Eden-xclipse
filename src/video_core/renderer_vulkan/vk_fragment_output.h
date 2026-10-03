// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include <vulkan/vulkan_core.h>

#include "shader_recompiler/runtime_info.h"
#include "video_core/surface.h"

namespace Vulkan {

[[nodiscard]] inline Shader::AttributeType FragmentOutputTypeForPixelFormat(
    VideoCore::Surface::PixelFormat format) {
    if (format == VideoCore::Surface::PixelFormat::Invalid) {
        return Shader::AttributeType::Disabled;
    }
    if (!VideoCore::Surface::IsPixelFormatInteger(format)) {
        return Shader::AttributeType::Float;
    }
    return VideoCore::Surface::IsPixelFormatSignedInteger(format)
               ? Shader::AttributeType::SignedInt
               : Shader::AttributeType::UnsignedInt;
}

[[nodiscard]] inline Shader::AttributeType FragmentOutputTypeForVkFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_A8B8G8R8_SINT_PACK32:
    case VK_FORMAT_R8_SINT:
    case VK_FORMAT_R8G8_SINT:
    case VK_FORMAT_R16_SINT:
    case VK_FORMAT_R16G16_SINT:
    case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32G32_SINT:
    case VK_FORMAT_R32G32B32A32_SINT:
        return Shader::AttributeType::SignedInt;
    case VK_FORMAT_A8B8G8R8_UINT_PACK32:
    case VK_FORMAT_A2B10G10R10_UINT_PACK32:
    case VK_FORMAT_R8_UINT:
    case VK_FORMAT_R8G8_UINT:
    case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32G32_UINT:
    case VK_FORMAT_R32G32B32A32_UINT:
        return Shader::AttributeType::UnsignedInt;
    case VK_FORMAT_UNDEFINED:
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return Shader::AttributeType::Disabled;
    default:
        return Shader::AttributeType::Float;
    }
}

template <size_t NumAttachments>
[[nodiscard]] inline std::array<Shader::AttributeType, NumAttachments>
MakeFragmentColorOutputTypes(
    const std::array<VideoCore::Surface::PixelFormat, NumAttachments>& formats,
    bool dual_source_blend) {
    std::array<Shader::AttributeType, NumAttachments> output_types{};
    for (size_t index = 0; index < NumAttachments; ++index) {
        output_types[index] = FragmentOutputTypeForPixelFormat(formats[index]);
    }
    if constexpr (NumAttachments > 1) {
        if (dual_source_blend && output_types[0] != Shader::AttributeType::Disabled) {
            output_types[1] = output_types[0];
        }
    }
    return output_types;
}

[[nodiscard]] constexpr const char* FragmentOutputTypeName(Shader::AttributeType type) noexcept {
    switch (type) {
    case Shader::AttributeType::Float:
        return "float";
    case Shader::AttributeType::SignedInt:
        return "sint";
    case Shader::AttributeType::UnsignedInt:
        return "uint";
    case Shader::AttributeType::SignedScaled:
        return "signed-scaled";
    case Shader::AttributeType::UnsignedScaled:
        return "unsigned-scaled";
    case Shader::AttributeType::Disabled:
        return "disabled";
    }
    return "unknown";
}

} // namespace Vulkan
