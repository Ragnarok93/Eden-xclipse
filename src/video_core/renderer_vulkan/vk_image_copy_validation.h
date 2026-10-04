// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "video_core/texture_cache/image_info.h"
#include "video_core/texture_cache/types.h"
#include "video_core/texture_cache/util.h"

namespace Vulkan {

struct ImageCopyBounds {
    VideoCommon::Extent3D src_mip_size{};
    VideoCommon::Extent3D dst_mip_size{};
    bool src_in_bounds{};
    bool dst_in_bounds{};

    [[nodiscard]] bool InBounds() const noexcept {
        return src_in_bounds && dst_in_bounds;
    }
};

[[nodiscard]] inline bool ImageCopyExtentIsNonEmpty(VideoCommon::Extent3D extent,
                                                    s32 num_layers = 1) noexcept {
    return extent.width != 0 && extent.height != 0 && extent.depth != 0 && num_layers > 0;
}

[[nodiscard]] inline bool ImageCopyExtentFits(VideoCommon::Offset3D offset,
                                              VideoCommon::Extent3D extent,
                                              VideoCommon::Extent3D limit) noexcept {
    if (!ImageCopyExtentIsNonEmpty(extent) ||
        offset.x < 0 || offset.y < 0 || offset.z < 0) {
        return false;
    }
    return static_cast<u64>(offset.x) + extent.width <= limit.width &&
           static_cast<u64>(offset.y) + extent.height <= limit.height &&
           static_cast<u64>(offset.z) + extent.depth <= limit.depth;
}

[[nodiscard]] inline bool ImageCopyLayersFit(const VideoCommon::ImageInfo& info,
                                             const VideoCommon::SubresourceLayers& layers) noexcept {
    if (layers.base_level < 0 || layers.base_level >= info.resources.levels ||
        layers.base_layer < 0 || layers.num_layers <= 0) {
        return false;
    }
    if (info.type == VideoCommon::ImageType::e3D) {
        return layers.base_layer == 0 && layers.num_layers == 1;
    }
    return layers.base_layer + layers.num_layers <= info.resources.layers;
}

[[nodiscard]] inline ImageCopyBounds ValidateImageCopyBounds(
    const VideoCommon::ImageInfo& src, const VideoCommon::ImageInfo& dst,
    const VideoCommon::ImageCopy& copy) {
    ImageCopyBounds result{};
    if (copy.src_subresource.base_level >= 0 &&
        copy.src_subresource.base_level < src.resources.levels) {
        result.src_mip_size =
            VideoCommon::MipSize(src.size, static_cast<u32>(copy.src_subresource.base_level));
        result.src_in_bounds = ImageCopyLayersFit(src, copy.src_subresource) &&
                               ImageCopyExtentFits(copy.src_offset, copy.extent,
                                                   result.src_mip_size);
    }
    if (copy.dst_subresource.base_level >= 0 &&
        copy.dst_subresource.base_level < dst.resources.levels) {
        result.dst_mip_size =
            VideoCommon::MipSize(dst.size, static_cast<u32>(copy.dst_subresource.base_level));
        result.dst_in_bounds = ImageCopyLayersFit(dst, copy.dst_subresource) &&
                               ImageCopyExtentFits(copy.dst_offset, copy.extent,
                                                   result.dst_mip_size);
    }
    return result;
}

} // namespace Vulkan
