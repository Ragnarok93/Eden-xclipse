// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/vk_image_copy_validation.h"

TEST_CASE("Vulkan image copy bounds reject oversized reinterpret regions",
          "[video_core][vulkan][xclipse]") {
    VideoCommon::ImageInfo src{};
    src.type = VideoCommon::ImageType::e2D;
    src.size = {8, 8, 1};
    src.resources = {.levels = 1, .layers = 1};

    VideoCommon::ImageInfo dst{};
    dst.type = VideoCommon::ImageType::e2D;
    dst.size = {32, 32, 1};
    dst.resources = {.levels = 1, .layers = 1};

    VideoCommon::ImageCopy oversized{};
    oversized.extent = {32, 32, 1};

    const auto bad = Vulkan::ValidateImageCopyBounds(src, dst, oversized);
    REQUIRE_FALSE(bad.src_in_bounds);
    REQUIRE(bad.dst_in_bounds);

    VideoCommon::ImageCopy original{};
    original.extent = {8, 8, 1};
    REQUIRE(Vulkan::ValidateImageCopyBounds(src, dst, original).InBounds());
}

TEST_CASE("Vulkan image copy bounds validate mip and layer ranges",
          "[video_core][vulkan][xclipse]") {
    VideoCommon::ImageInfo info{};
    info.type = VideoCommon::ImageType::e2D;
    info.size = {64, 32, 1};
    info.resources = {.levels = 4, .layers = 2};

    VideoCommon::ImageCopy copy{};
    copy.src_subresource = {.base_level = 2, .base_layer = 1, .num_layers = 1};
    copy.dst_subresource = copy.src_subresource;
    copy.extent = {16, 8, 1};
    REQUIRE(Vulkan::ValidateImageCopyBounds(info, info, copy).InBounds());

    copy.extent.width = 17;
    REQUIRE_FALSE(Vulkan::ValidateImageCopyBounds(info, info, copy).InBounds());

    copy.extent.width = 16;
    copy.src_subresource.base_layer = 2;
    REQUIRE_FALSE(Vulkan::ValidateImageCopyBounds(info, info, copy).src_in_bounds);
}

TEST_CASE("Vulkan 3D image copy validates z extent and fixed array layer",
          "[video_core][vulkan][xclipse]") {
    VideoCommon::ImageInfo info{};
    info.type = VideoCommon::ImageType::e3D;
    info.size = {16, 16, 8};
    info.resources = {.levels = 2, .layers = 1};

    VideoCommon::ImageCopy copy{};
    copy.src_subresource = {.base_level = 1, .base_layer = 0, .num_layers = 1};
    copy.dst_subresource = copy.src_subresource;
    copy.src_offset = {0, 0, 2};
    copy.dst_offset = {0, 0, 2};
    copy.extent = {8, 8, 2};
    REQUIRE(Vulkan::ValidateImageCopyBounds(info, info, copy).InBounds());

    copy.extent.depth = 3;
    REQUIRE_FALSE(Vulkan::ValidateImageCopyBounds(info, info, copy).InBounds());
}


TEST_CASE("Vulkan image copy bounds reject zero extents",
          "[video_core][vulkan][xclipse]") {
    VideoCommon::ImageInfo info{};
    info.type = VideoCommon::ImageType::e2D;
    info.size = {32, 32, 1};
    info.resources = {.levels = 1, .layers = 1};

    VideoCommon::ImageCopy copy{};
    copy.extent = {32, 32, 1};
    REQUIRE(Vulkan::ValidateImageCopyBounds(info, info, copy).InBounds());

    copy.extent.width = 0;
    REQUIRE_FALSE(Vulkan::ValidateImageCopyBounds(info, info, copy).InBounds());
    copy.extent = {32, 0, 1};
    REQUIRE_FALSE(Vulkan::ValidateImageCopyBounds(info, info, copy).InBounds());
    copy.extent = {32, 32, 0};
    REQUIRE_FALSE(Vulkan::ValidateImageCopyBounds(info, info, copy).InBounds());
}


TEST_CASE("Vulkan buffer-image copy nonempty predicate includes layer count",
          "[video_core][vulkan][xclipse]") {
    REQUIRE(Vulkan::ImageCopyExtentIsNonEmpty({1, 1, 1}, 1));
    REQUIRE_FALSE(Vulkan::ImageCopyExtentIsNonEmpty({0, 1, 1}, 1));
    REQUIRE_FALSE(Vulkan::ImageCopyExtentIsNonEmpty({1, 0, 1}, 1));
    REQUIRE_FALSE(Vulkan::ImageCopyExtentIsNonEmpty({1, 1, 0}, 1));
    REQUIRE_FALSE(Vulkan::ImageCopyExtentIsNonEmpty({1, 1, 1}, 0));
}
