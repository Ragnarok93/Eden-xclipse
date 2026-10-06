// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/vulkan_common/xclipse_optimization_probes.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <bc_decoder.h>

#include "common/common_types.h"
#include "video_core/host_shaders/bcn_decoder_r8_comp_spv.h"
#include "video_core/host_shaders/bcn_decoder_r8_snorm_comp_spv.h"
#include "video_core/host_shaders/bcn_decoder_rg8_comp_spv.h"
#include "video_core/host_shaders/bcn_decoder_rg8_snorm_comp_spv.h"
#include "video_core/host_shaders/bcn_bptc_decoder_rgba16f_comp_spv.h"
#include "video_core/host_shaders/bcn_bptc_decoder_rgba8_comp_spv.h"
#include "common/logging.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
namespace {

using Clock = std::chrono::steady_clock;

struct BufferResource {
    const vk::DeviceDispatch& dld;
    VkDevice device{};
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    void* mapped{};

    ~BufferResource() {
        if (mapped && memory) {
            dld.vkUnmapMemory(device, memory);
        }
        if (buffer) {
            dld.vkDestroyBuffer(device, buffer, nullptr);
        }
        if (memory) {
            dld.vkFreeMemory(device, memory, nullptr);
        }
    }
};

struct ImageResource {
    const vk::DeviceDispatch& dld;
    VkDevice device{};
    VkImage image{};
    VkDeviceMemory memory{};

    ~ImageResource() {
        if (image) {
            dld.vkDestroyImage(device, image, nullptr);
        }
        if (memory) {
            dld.vkFreeMemory(device, memory, nullptr);
        }
    }
};

struct QueryPoolResource {
    const vk::DeviceDispatch& dld;
    VkDevice device{};
    VkQueryPool pool{};

    ~QueryPoolResource() {
        if (pool) {
            dld.vkDestroyQueryPool(device, pool, nullptr);
        }
    }
};

struct FenceResource {
    const vk::DeviceDispatch& dld;
    VkDevice device{};
    VkFence fence{};

    ~FenceResource() {
        if (fence) {
            dld.vkDestroyFence(device, fence, nullptr);
        }
    }
};

[[nodiscard]] std::optional<u32> FindMemoryType(
    const VkPhysicalDeviceMemoryProperties& properties, u32 type_bits,
    VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) {
    const auto find = [&](VkMemoryPropertyFlags wanted) -> std::optional<u32> {
        for (u32 index = 0; index < properties.memoryTypeCount; ++index) {
            if ((type_bits & (1U << index)) == 0) {
                continue;
            }
            if ((properties.memoryTypes[index].propertyFlags & wanted) == wanted) {
                return index;
            }
        }
        return std::nullopt;
    };

    if (const auto preferred_type = find(required | preferred)) {
        return preferred_type;
    }
    return find(required);
}

[[nodiscard]] bool CreateBuffer(const vk::DeviceDispatch& dld, const vk::Device& logical,
                                VkDevice device,
                                VkPhysicalDeviceMemoryProperties memory_properties,
                                VkDeviceSize size, VkBufferUsageFlags usage,
                                VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred,
                                BufferResource& resource, bool map_memory) {
    const VkBufferCreateInfo buffer_ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    if (dld.vkCreateBuffer(device, &buffer_ci, nullptr, &resource.buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements requirements{};
    requirements = logical.GetBufferMemoryRequirements(resource.buffer);
    const auto memory_type =
        FindMemoryType(memory_properties, requirements.memoryTypeBits, required, preferred);
    if (!memory_type) {
        return false;
    }

    const VkMemoryAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = requirements.size,
        .memoryTypeIndex = *memory_type,
    };
    if (dld.vkAllocateMemory(device, &allocate_info, nullptr, &resource.memory) != VK_SUCCESS) {
        return false;
    }
    if (dld.vkBindBufferMemory(device, resource.buffer, resource.memory, 0) != VK_SUCCESS) {
        return false;
    }
    if (!map_memory) {
        return true;
    }
    return dld.vkMapMemory(device, resource.memory, 0, size, 0, &resource.mapped) == VK_SUCCESS;
}

[[nodiscard]] bool AllocateAndBindImage(
    const vk::DeviceDispatch& dld, VkDevice device,
    VkPhysicalDeviceMemoryProperties memory_properties, const VkImageCreateInfo& image_ci,
    VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, ImageResource& resource) {
    if (dld.vkCreateImage(device, &image_ci, nullptr, &resource.image) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements requirements{};
    dld.vkGetImageMemoryRequirements(device, resource.image, &requirements);
    const auto memory_type =
        FindMemoryType(memory_properties, requirements.memoryTypeBits, required, preferred);
    if (!memory_type) {
        return false;
    }

    const VkMemoryAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = requirements.size,
        .memoryTypeIndex = *memory_type,
    };
    if (dld.vkAllocateMemory(device, &allocate_info, nullptr, &resource.memory) != VK_SUCCESS) {
        return false;
    }
    return dld.vkBindImageMemory(device, resource.image, resource.memory, 0) == VK_SUCCESS;
}

[[nodiscard]] bool BeginCommandBuffer(const vk::DeviceDispatch& dld, VkCommandBuffer command_buffer) {
    const VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    return dld.vkBeginCommandBuffer(command_buffer, &begin_info) == VK_SUCCESS;
}

[[nodiscard]] bool SubmitAndWait(const vk::DeviceDispatch& dld, VkDevice device,
                                 vk::Queue queue, VkCommandBuffer command_buffer) {
    const VkFenceCreateInfo fence_ci{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };
    FenceResource fence{dld, device};
    if (dld.vkCreateFence(device, &fence_ci, nullptr, &fence.fence) != VK_SUCCESS) {
        return false;
    }

    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };
    const VkResult submit_result = queue.Submit(vk::Span<VkSubmitInfo>{submit_info}, fence.fence);
    if (submit_result != VK_SUCCESS) {
        return false;
    }
    return dld.vkWaitForFences(device, 1, &fence.fence, VK_TRUE, 1'000'000'000ULL) == VK_SUCCESS;
}

[[nodiscard]] bool RunEmptySubmitProbe(const Device& device, u64& elapsed_ns) {
    const auto& dld = device.GetDispatchLoader();
    const VkDevice raw_device = *device.GetLogical();
    const vk::Queue queue = device.GetGraphicsQueue();
    const VkCommandPoolCreateInfo pool_ci{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = device.GetGraphicsFamily(),
    };

    VkCommandPool pool{};
    if (dld.vkCreateCommandPool(raw_device, &pool_ci, nullptr, &pool) != VK_SUCCESS) {
        return false;
    }
    const auto cleanup_pool = [&] { dld.vkDestroyCommandPool(raw_device, pool, nullptr); };

    VkCommandBuffer command_buffer{};
    const VkCommandBufferAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (dld.vkAllocateCommandBuffers(raw_device, &allocate_info, &command_buffer) != VK_SUCCESS) {
        cleanup_pool();
        return false;
    }

    if (!BeginCommandBuffer(dld, command_buffer) ||
        dld.vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        cleanup_pool();
        return false;
    }

    const auto start = Clock::now();
    const bool valid = SubmitAndWait(dld, raw_device, queue, command_buffer);
    elapsed_ns = static_cast<u64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
    cleanup_pool();
    return valid;
}

[[nodiscard]] bool RunBufferTransferProbe(const Device& device,
                                          XclipseOptimizationProbeResults& results) {
    const auto& dld = device.GetDispatchLoader();
    const VkDevice raw_device = *device.GetLogical();
    const VkPhysicalDeviceMemoryProperties memory_properties =
        device.GetPhysical().GetMemoryProperties().memoryProperties;

    constexpr std::array<VkDeviceSize, 3> Sizes{
        64ULL * 1024ULL,
        1024ULL * 1024ULL,
        4ULL * 1024ULL * 1024ULL,
    };
    constexpr VkDeviceSize MaxSize = Sizes.back();

    BufferResource source{dld, raw_device};
    BufferResource destination{dld, raw_device};
    BufferResource readback{dld, raw_device};

    if (!CreateBuffer(dld, device.GetLogical(), raw_device, memory_properties, MaxSize,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      VK_MEMORY_PROPERTY_HOST_CACHED_BIT, source, true) ||
        !CreateBuffer(dld, device.GetLogical(), raw_device, memory_properties, MaxSize,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                      destination, false) ||
        !CreateBuffer(dld, device.GetLogical(), raw_device, memory_properties, MaxSize,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      VK_MEMORY_PROPERTY_HOST_CACHED_BIT, readback, true)) {
        return false;
    }

    auto* source_bytes = static_cast<u8*>(source.mapped);
    auto* readback_bytes = static_cast<u8*>(readback.mapped);
    for (VkDeviceSize index = 0; index < MaxSize; ++index) {
        source_bytes[index] = static_cast<u8>((index * 37U + 11U) & 0xFFU);
    }

    const auto queue_properties = device.GetPhysical().GetQueueFamilyProperties();
    const VkQueueFamilyProperties& graphics_family_properties =
        queue_properties[device.GetGraphicsFamily()];
    const bool timestamp_capable =
        graphics_family_properties.timestampValidBits != 0 &&
        device.GetPhysical().GetProperties().limits.timestampPeriod > 0.0f;
    if (timestamp_capable) {
        results.timestamp_queries = CapabilityState::Advertised;
        results.timestamp_valid_bits = graphics_family_properties.timestampValidBits;
        results.timestamp_period_ps = static_cast<u64>(std::llround(
            static_cast<double>(device.GetPhysical().GetProperties().limits.timestampPeriod) *
            1000.0));
    }

    for (std::size_t index = 0; index < Sizes.size(); ++index) {
        VkQueryPool query_pool{};
        QueryPoolResource query_pool_resource{dld, raw_device};
        if (timestamp_capable) {
            const VkQueryPoolCreateInfo query_ci{
                .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .queryType = VK_QUERY_TYPE_TIMESTAMP,
                .queryCount = 2,
                .pipelineStatistics = 0,
            };
            if (dld.vkCreateQueryPool(raw_device, &query_ci, nullptr, &query_pool) != VK_SUCCESS) {
                return false;
            }
            query_pool_resource.pool = query_pool;
        }

        const VkCommandPoolCreateInfo pool_ci{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = device.GetGraphicsFamily(),
        };
        VkCommandPool pool{};
        if (dld.vkCreateCommandPool(raw_device, &pool_ci, nullptr, &pool) != VK_SUCCESS) {
            return false;
        }

        VkCommandBuffer command_buffer{};
        const VkCommandBufferAllocateInfo allocate_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        if (dld.vkAllocateCommandBuffers(raw_device, &allocate_info, &command_buffer) != VK_SUCCESS ||
            !BeginCommandBuffer(dld, command_buffer)) {
            dld.vkDestroyCommandPool(raw_device, pool, nullptr);
            return false;
        }

        const VkMemoryBarrier host_write_barrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        };
        dld.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &host_write_barrier,
                                 0, nullptr, 0, nullptr);

        const VkBufferCopy copy_region{
            .srcOffset = 0,
            .dstOffset = 0,
            .size = Sizes[index],
        };
        dld.vkCmdCopyBuffer(command_buffer, source.buffer, destination.buffer, 1, &copy_region);

        const VkMemoryBarrier transfer_barrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        };
        dld.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &transfer_barrier, 0,
                                 nullptr, 0, nullptr);
        dld.vkCmdCopyBuffer(command_buffer, destination.buffer, readback.buffer, 1, &copy_region);

        const VkMemoryBarrier host_barrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        };
        dld.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host_barrier, 0, nullptr,
                                 0, nullptr);

        if (dld.vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
            dld.vkDestroyCommandPool(raw_device, pool, nullptr);
            return false;
        }

        const auto cpu_start = Clock::now();
        const bool submitted = SubmitAndWait(dld, raw_device, device.GetGraphicsQueue(), command_buffer);
        const u64 cpu_elapsed = static_cast<u64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - cpu_start).count());

        if (!submitted) {
            dld.vkDestroyCommandPool(raw_device, pool, nullptr);
            return false;
        }

        if (index == 0) {
            results.copy_64k_ns = cpu_elapsed;
        } else if (index == 1) {
            results.copy_1m_ns = cpu_elapsed;
        } else {
            results.copy_4m_ns = cpu_elapsed;
        }

        const auto* const expected = source_bytes;
        if (std::memcmp(readback_bytes, expected, static_cast<std::size_t>(Sizes[index])) != 0) {
            LOG_WARNING(Render_Vulkan, "XCLIPSE PROBE buffer transfer validation failed at {} bytes",
                        Sizes[index]);
            dld.vkDestroyCommandPool(raw_device, pool, nullptr);
            return false;
        }

        dld.vkDestroyCommandPool(raw_device, pool, nullptr);
    }

    return true;
}

[[nodiscard]] bool RunImageTransferProbe(const Device& device,
                                         XclipseOptimizationProbeResults& results) {
    const auto& dld = device.GetDispatchLoader();
    const VkDevice raw_device = *device.GetLogical();
    const VkPhysicalDeviceMemoryProperties memory_properties =
        device.GetPhysical().GetMemoryProperties().memoryProperties;

    constexpr u32 Width = 4;
    constexpr u32 Height = 4;
    constexpr VkDeviceSize Bytes = Width * Height * 4;

    BufferResource source{dld, raw_device};
    BufferResource readback{dld, raw_device};
    if (!CreateBuffer(dld, device.GetLogical(), raw_device, memory_properties, Bytes,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      VK_MEMORY_PROPERTY_HOST_CACHED_BIT, source, true) ||
        !CreateBuffer(dld, device.GetLogical(), raw_device, memory_properties, Bytes,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      VK_MEMORY_PROPERTY_HOST_CACHED_BIT, readback, true)) {
        return false;
    }

    auto* const source_bytes = static_cast<u8*>(source.mapped);
    auto* const readback_bytes = static_cast<u8*>(readback.mapped);
    for (u32 index = 0; index < Bytes; ++index) {
        source_bytes[index] = static_cast<u8>((index * 13U + 7U) & 0xFFU);
    }
    std::memset(readback_bytes, 0, Bytes);

    const VkImageCreateInfo image_ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {Width, Height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    ImageResource image{dld, raw_device};
    if (!AllocateAndBindImage(dld, raw_device, memory_properties, image_ci,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, image)) {
        return false;
    }

    const VkImageSubresourceRange range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    const VkImageMemoryBarrier to_dst{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image.image,
        .subresourceRange = range,
    };
    const VkImageMemoryBarrier to_src{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image.image,
        .subresourceRange = range,
    };
    const VkMemoryBarrier host_write_barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
    };
    const VkMemoryBarrier host_barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
    };
    const VkBufferImageCopy copy_region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {Width, Height, 1},
    };

    const VkCommandPoolCreateInfo pool_ci{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = device.GetGraphicsFamily(),
    };
    VkCommandPool pool{};
    if (dld.vkCreateCommandPool(raw_device, &pool_ci, nullptr, &pool) != VK_SUCCESS) {
        return false;
    }

    VkCommandBuffer command_buffer{};
    const VkCommandBufferAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (dld.vkAllocateCommandBuffers(raw_device, &allocate_info, &command_buffer) != VK_SUCCESS ||
        !BeginCommandBuffer(dld, command_buffer)) {
        dld.vkDestroyCommandPool(raw_device, pool, nullptr);
        return false;
    }

    dld.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &host_write_barrier,
                             0, nullptr, 0, nullptr);
    dld.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &to_dst);
    dld.vkCmdCopyBufferToImage(command_buffer, source.buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);
    dld.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &to_src);
    dld.vkCmdCopyImageToBuffer(command_buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer,
                               1, &copy_region);
    dld.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host_barrier, 0, nullptr,
                             0, nullptr);

    if (dld.vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        dld.vkDestroyCommandPool(raw_device, pool, nullptr);
        return false;
    }

    const bool valid = SubmitAndWait(dld, raw_device, device.GetGraphicsQueue(), command_buffer);
    results.image_transfer = CapabilityState::Advertised;
    if (valid && std::memcmp(source_bytes, readback_bytes, static_cast<std::size_t>(Bytes)) == 0) {
        results.image_transfer = CapabilityState::Validated;
    }

    dld.vkDestroyCommandPool(raw_device, pool, nullptr);
    if (results.image_transfer != CapabilityState::Validated) {
        return false;
    }

    results.storage_image_create = CapabilityState::Advertised;
    const VkImageCreateInfo storage_image_ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {Width, Height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    ImageResource storage_image{dld, raw_device};
    if (AllocateAndBindImage(dld, raw_device, memory_properties, storage_image_ci,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, storage_image)) {
        results.storage_image_create = CapabilityState::Validated;
    } else {
        results.storage_image_create = CapabilityState::Advertised;
    }
    return true;
}


struct RgtcProbeObjects {
    const vk::DeviceDispatch& dld;
    VkDevice device{};
    VkShaderModule shader{};
    VkDescriptorSetLayout descriptor_layout{};
    VkPipelineLayout pipeline_layout{};
    VkPipeline pipeline{};
    VkDescriptorPool descriptor_pool{};
    VkDescriptorSet descriptor_set{};
    VkImageView image_view{};
    VkCommandPool command_pool{};

    ~RgtcProbeObjects() {
        if (command_pool) {
            dld.vkDestroyCommandPool(device, command_pool, nullptr);
        }
        if (descriptor_pool) {
            dld.vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        }
        if (pipeline) {
            dld.vkDestroyPipeline(device, pipeline, nullptr);
        }
        if (pipeline_layout) {
            dld.vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        }
        if (descriptor_layout) {
            dld.vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
        }
        if (shader) {
            dld.vkDestroyShaderModule(device, shader, nullptr);
        }
        if (image_view) {
            dld.vkDestroyImageView(device, image_view, nullptr);
        }
    }
};

struct RgtcProbePushConstants {
    u32 format{};
    u32 layer_stride{};
    u32 block_size{};
    u32 x_shift{};
    u32 block_height{};
    u32 block_height_mask{};
};
static_assert(sizeof(RgtcProbePushConstants) == 24);

struct RgtcProbeCase {
    const char* name{};
    u32 format{};
    VkFormat output_format{};
    const u32* code{};
    size_t code_size{};
    u32 bytes_per_pixel{};
    bool is_bc5{};
    bool is_signed{};
    s32 left_r{};
    s32 left_g{};
    s32 right_r{};
    s32 right_g{};
};

[[nodiscard]] u8 EncodeProbeChannel(s32 value) {
    return static_cast<u8>(value & 0xff);
}

void WriteBc4ProbeBlock(u8* dst, s32 endpoint0, s32 endpoint1, u32 selector_phase) {
    u64 packed = static_cast<u64>(EncodeProbeChannel(endpoint0)) |
                 (static_cast<u64>(EncodeProbeChannel(endpoint1)) << 8);
    for (u32 texel = 0; texel < 16; ++texel) {
        const u64 selector = static_cast<u64>((texel + selector_phase) & 7u);
        packed |= selector << (16u + texel * 3u);
    }
    for (u32 byte = 0; byte < 8; ++byte) {
        dst[byte] = static_cast<u8>((packed >> (byte * 8u)) & 0xffu);
    }
}

[[nodiscard]] u32 RgtcProbeBlockLinearOffset(const RgtcProbeCase& probe, u32 block_x,
                                             u32 block_y) {
    const u32 bytes_per_block_log2 = probe.is_bc5 ? 4u : 3u;
    const u32 x = block_x << bytes_per_block_log2;
    const u32 gob_y = block_y >> 3u;
    const u32 swizzle =
        ((x & 32u) << 3u) | ((block_y & 6u) << 5u) | ((x & 16u) << 1u) |
        ((block_y & 1u) << 4u) | (x & 15u);
    return (gob_y << 9u) + ((x >> 6u) << 9u) + swizzle;
}

[[nodiscard]] std::array<u8, 16> MakeRgtcProbeBlock(const RgtcProbeCase& probe, u32 block_x,
                                                     u32 block_y) {
    std::array<u8, 16> block{};
    const bool reverse = ((block_x + block_y) & 1u) != 0;
    const s32 red0 = reverse ? probe.right_r : probe.left_r;
    const s32 red1 = reverse ? probe.left_r : probe.right_r;
    WriteBc4ProbeBlock(block.data(), red0, red1, block_x * 3u + block_y * 5u);
    if (probe.is_bc5) {
        const s32 green0 = reverse ? probe.right_g : probe.left_g;
        const s32 green1 = reverse ? probe.left_g : probe.right_g;
        WriteBc4ProbeBlock(block.data() + 8, green0, green1,
                           block_x * 5u + block_y * 3u + 2u);
    }
    return block;
}

struct BlockDecodeProbeCase {
    const char* name{};
    u32 format{};
    VkFormat output_format{};
    const u32* code{};
    size_t code_size{};
    u32 width{};
    u32 height{};
    u32 bytes_per_pixel{};
    u32 dispatch_x{};
    u32 dispatch_y{1};
    std::span<const u8> input{};
    std::span<const u8> expected{};
};

[[nodiscard]] bool RunBlockDecodeProbe(const Device& device, const BlockDecodeProbeCase& probe) {
    const VkDeviceSize input_bytes = probe.input.size_bytes();
    const VkDeviceSize output_bytes = probe.expected.size_bytes();
    const auto& dld = device.GetDispatchLoader();
    const VkDevice raw_device = *device.GetLogical();
    const auto memory_properties = device.GetPhysical().GetMemoryProperties().memoryProperties;
    const auto fail = [&](std::string_view stage, VkResult result = VK_ERROR_UNKNOWN) {
        LOG_WARNING(Render_Vulkan,
                    "XCLIPSE BCN DECODE PROBE format={} extent={}x{} stage={} result={}",
                    probe.name, probe.width, probe.height, stage, result);
        return false;
    };

    if (input_bytes == 0 || output_bytes == 0 || probe.width == 0 || probe.height == 0 ||
        probe.dispatch_x == 0 || probe.dispatch_y == 0) {
        return fail("invalid-probe");
    }
    if (!device.IsFormatSupported(probe.output_format,
                                  VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                                      VK_FORMAT_FEATURE_TRANSFER_SRC_BIT,
                                  FormatType::Optimal)) {
        return fail("output-format-unsupported");
    }

    BufferResource input{dld, raw_device};
    BufferResource readback{dld, raw_device};
    constexpr VkMemoryPropertyFlags HostProbeMemory =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!CreateBuffer(dld, device.GetLogical(), raw_device, memory_properties, input_bytes,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostProbeMemory, 0, input, true)) {
        return fail("input-buffer");
    }
    if (!CreateBuffer(dld, device.GetLogical(), raw_device, memory_properties, output_bytes,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT, HostProbeMemory,
                      VK_MEMORY_PROPERTY_HOST_CACHED_BIT, readback, true)) {
        return fail("readback-buffer");
    }
    std::memcpy(input.mapped, probe.input.data(), probe.input.size_bytes());
    std::memset(readback.mapped, 0xcd, probe.expected.size_bytes());

    const VkImageCreateInfo image_ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = probe.output_format,
        .extent = {probe.width, probe.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    ImageResource image{dld, raw_device};
    if (!AllocateAndBindImage(dld, raw_device, memory_properties, image_ci,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, image)) {
        return fail("output-image");
    }

    RgtcProbeObjects objects{dld, raw_device};
    const VkImageViewCreateInfo view_ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = image.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        .format = probe.output_format,
        .components{
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VkResult result =
        dld.vkCreateImageView(raw_device, &view_ci, nullptr, &objects.image_view);
    if (result != VK_SUCCESS) {
        return fail("image-view", result);
    }

    const VkShaderModuleCreateInfo shader_ci{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = probe.code_size,
        .pCode = probe.code,
    };
    result = dld.vkCreateShaderModule(raw_device, &shader_ci, nullptr, &objects.shader);
    if (result != VK_SUCCESS) {
        return fail("shader-module", result);
    }

    const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        },
    }};
    const VkDescriptorSetLayoutCreateInfo descriptor_layout_ci{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    };
    result = dld.vkCreateDescriptorSetLayout(raw_device, &descriptor_layout_ci, nullptr,
                                             &objects.descriptor_layout);
    if (result != VK_SUCCESS) {
        return fail("descriptor-layout", result);
    }

    const VkPushConstantRange push_range{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(RgtcProbePushConstants),
    };
    const VkPipelineLayoutCreateInfo pipeline_layout_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 1,
        .pSetLayouts = &objects.descriptor_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    result = dld.vkCreatePipelineLayout(raw_device, &pipeline_layout_ci, nullptr,
                                        &objects.pipeline_layout);
    if (result != VK_SUCCESS) {
        return fail("pipeline-layout", result);
    }

    const VkPipelineShaderStageCreateInfo stage_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = objects.shader,
        .pName = "main",
        .pSpecializationInfo = nullptr,
    };
    const VkComputePipelineCreateInfo pipeline_ci{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = stage_ci,
        .layout = objects.pipeline_layout,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };
    result = dld.vkCreateComputePipelines(raw_device, device.StaticPipelineCache(), 1,
                                          &pipeline_ci, nullptr, &objects.pipeline);
    if (result != VK_SUCCESS) {
        return fail("compute-pipeline", result);
    }

    const std::array<VkDescriptorPoolSize, 2> pool_sizes{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
    }};
    const VkDescriptorPoolCreateInfo descriptor_pool_ci{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .maxSets = 1,
        .poolSizeCount = static_cast<u32>(pool_sizes.size()),
        .pPoolSizes = pool_sizes.data(),
    };
    result = dld.vkCreateDescriptorPool(raw_device, &descriptor_pool_ci, nullptr,
                                        &objects.descriptor_pool);
    if (result != VK_SUCCESS) {
        return fail("descriptor-pool", result);
    }
    const VkDescriptorSetAllocateInfo descriptor_alloc{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = objects.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &objects.descriptor_layout,
    };
    result =
        dld.vkAllocateDescriptorSets(raw_device, &descriptor_alloc, &objects.descriptor_set);
    if (result != VK_SUCCESS) {
        return fail("descriptor-set", result);
    }

    const VkDescriptorBufferInfo input_info{
        .buffer = input.buffer,
        .offset = 0,
        .range = input_bytes,
    };
    const VkDescriptorImageInfo output_info{
        .sampler = VK_NULL_HANDLE,
        .imageView = objects.image_view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const std::array<VkWriteDescriptorSet, 2> writes{{
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = objects.descriptor_set,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo = nullptr,
            .pBufferInfo = &input_info,
            .pTexelBufferView = nullptr,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = objects.descriptor_set,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &output_info,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        },
    }};
    dld.vkUpdateDescriptorSets(raw_device, static_cast<u32>(writes.size()), writes.data(), 0,
                               nullptr);

    const VkCommandPoolCreateInfo command_pool_ci{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = device.GetGraphicsFamily(),
    };
    result = dld.vkCreateCommandPool(raw_device, &command_pool_ci, nullptr,
                                     &objects.command_pool);
    if (result != VK_SUCCESS) {
        return fail("command-pool", result);
    }
    VkCommandBuffer command_buffer{};
    const VkCommandBufferAllocateInfo command_alloc{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = objects.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    result = dld.vkAllocateCommandBuffers(raw_device, &command_alloc, &command_buffer);
    if (result != VK_SUCCESS || !BeginCommandBuffer(dld, command_buffer)) {
        return fail("command-buffer", result);
    }

    const VkBufferMemoryBarrier host_to_compute{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = input.buffer,
        .offset = 0,
        .size = input_bytes,
    };
    const VkImageMemoryBarrier to_general{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_NONE,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image.image,
        .subresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    dld.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
                             &host_to_compute, 1, &to_general);

    dld.vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, objects.pipeline);
    dld.vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                objects.pipeline_layout, 0, 1, &objects.descriptor_set, 0, nullptr);
    const RgtcProbePushConstants push{
        .format = probe.format,
        .layer_stride = 4096,
        .block_size = 512,
        .x_shift = 9,
        .block_height = 0,
        .block_height_mask = 0,
    };
    dld.vkCmdPushConstants(command_buffer, objects.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(push), &push);
    dld.vkCmdDispatch(command_buffer, probe.dispatch_x, probe.dispatch_y, 1);

    const VkImageMemoryBarrier to_transfer{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image.image,
        .subresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    dld.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &to_transfer);

    const VkBufferImageCopy copy{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {probe.width, probe.height, 1},
    };
    dld.vkCmdCopyImageToBuffer(command_buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);

    const VkBufferMemoryBarrier transfer_to_host{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = readback.buffer,
        .offset = 0,
        .size = output_bytes,
    };
    dld.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                             &transfer_to_host, 0, nullptr);

    result = dld.vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        return fail("command-end", result);
    }
    if (!SubmitAndWait(dld, raw_device, device.GetGraphicsQueue(), command_buffer)) {
        return fail("submit-readback");
    }
    if (std::memcmp(readback.mapped, probe.expected.data(), probe.expected.size_bytes()) != 0) {
        return fail("cpu-reference-mismatch");
    }
    LOG_INFO(Render_Vulkan,
             "XCLIPSE BCN DECODE PROBE format={} extent={}x{} result=validated",
             probe.name, probe.width, probe.height);
    return true;
}

[[nodiscard]] bool RunRgtcGeometryCase(const Device& device, const RgtcProbeCase& probe,
                                        u32 width, u32 height) {
    const u32 blocks_x = (width + 3u) / 4u;
    const u32 blocks_y = (height + 3u) / 4u;
    const u32 bytes_per_block = probe.is_bc5 ? 16u : 8u;

    u32 input_bytes = 0;
    for (u32 block_y = 0; block_y < blocks_y; ++block_y) {
        for (u32 block_x = 0; block_x < blocks_x; ++block_x) {
            input_bytes =
                std::max(input_bytes,
                         RgtcProbeBlockLinearOffset(probe, block_x, block_y) + bytes_per_block);
        }
    }

    std::vector<u8> input(input_bytes);
    std::vector<u8> expected(static_cast<size_t>(width) * height * probe.bytes_per_pixel);
    for (u32 block_y = 0; block_y < blocks_y; ++block_y) {
        for (u32 block_x = 0; block_x < blocks_x; ++block_x) {
            const auto block = MakeRgtcProbeBlock(probe, block_x, block_y);
            const u32 input_offset = RgtcProbeBlockLinearOffset(probe, block_x, block_y);
            std::memcpy(input.data() + input_offset, block.data(), bytes_per_block);

            const size_t x = static_cast<size_t>(block_x) * 4;
            const size_t y = static_cast<size_t>(block_y) * 4;
            u8* const expected_block =
                expected.data() + (y * width + x) * probe.bytes_per_pixel;
            if (probe.is_bc5) {
                bcn::DecodeBc5(block.data(), expected_block, x, y, width, height,
                               probe.is_signed);
            } else {
                bcn::DecodeBc4(block.data(), expected_block, x, y, width, height,
                               probe.is_signed);
            }
        }
    }

    if (probe.is_signed) {
        // The CPU decoder can represent the SNORM endpoint -1.0 as raw -128. The production
        // shader writes normalized floating-point values to an R8/RG8 SNORM storage image, for
        // which Vulkan's canonical 8-bit conversion stores -1.0 as -127. Both sample as -1.0;
        // canonicalize the CPU reference before requiring exact storage readback equality.
        for (u8& value : expected) {
            if (value == 0x80u) {
                value = 0x81u;
            }
        }
    }

    return RunBlockDecodeProbe(
        device,
        BlockDecodeProbeCase{
            .name = probe.name,
            .format = probe.format,
            .output_format = probe.output_format,
            .code = probe.code,
            .code_size = probe.code_size,
            .width = width,
            .height = height,
            .bytes_per_pixel = probe.bytes_per_pixel,
            .dispatch_x = (blocks_x + 7u) / 8u,
            .dispatch_y = (blocks_y + 7u) / 8u,
            .input = input,
            .expected = expected,
        });
}

[[nodiscard]] bool RunRgtcDecodeCase(const Device& device, const RgtcProbeCase& probe) {
    // A narrow partial block validates bounds handling. The larger case crosses both X and Y
    // GOB boundaries, exercises partial edge blocks, and varies endpoints/selectors so both
    // BC4/BC5 interpolation branches are compared against Eden's CPU reference.
    return RunRgtcGeometryCase(device, probe, 3, 3) &&
           RunRgtcGeometryCase(device, probe, 35, 33);
}

[[nodiscard]] u32 ProbeBlockLinearOffset(u32 block_index) {
    const u32 x = block_index << 4;
    const u32 swizzle =
        ((x & 32u) << 3u) | ((x & 16u) << 1u) | (x & 15u);
    return ((x >> 6u) << 9u) + swizzle;
}

[[nodiscard]] std::array<u8, 16> MakeBc7ProbeBlock(u32 mode) {
    std::array<u8, 16> block{};
    for (u32 index = 0; index < block.size(); ++index) {
        block[index] = static_cast<u8>((0x53u + mode * 29u + index * 17u) & 0xffu);
    }
    const u32 prefix_mask = mode == 7 ? 0xffu : ((1u << (mode + 1u)) - 1u);
    block[0] = static_cast<u8>((block[0] & ~prefix_mask) | (1u << mode));
    return block;
}

[[nodiscard]] std::array<u8, 16> MakeBc6ProbeBlock(u32 mode) {
    std::array<u8, 16> block{};
    for (u32 index = 0; index < block.size(); ++index) {
        block[index] = static_cast<u8>((0x31u + mode * 37u + index * 23u) & 0xffu);
    }
    if (mode < 2) {
        block[0] = static_cast<u8>((block[0] & ~0x3u) | mode);
    } else {
        const u32 encoded = mode - 2u;
        const u32 selector = 0x2u | (encoded & 0x1u) | ((encoded & 0xeu) << 1u);
        block[0] = static_cast<u8>((block[0] & 0xe0u) | selector);
    }
    return block;
}

[[nodiscard]] bool RunBc7DecodeProbe(const Device& device) {
    constexpr std::array<u32, 8> Modes{0, 1, 2, 3, 4, 5, 6, 7};
    constexpr u32 Height = 4;
    const u32 width = static_cast<u32>(Modes.size()) * 4;
    std::vector<u8> input(1024);
    std::vector<u8> expected(static_cast<size_t>(width) * Height * 4);

    for (u32 index = 0; index < Modes.size(); ++index) {
        const auto block = MakeBc7ProbeBlock(Modes[index]);
        const u32 offset = ProbeBlockLinearOffset(index);
        if (offset + block.size() > input.size()) {
            return false;
        }
        std::memcpy(input.data() + offset, block.data(), block.size());
        const size_t x = static_cast<size_t>(index) * 4;
        bcn::DecodeBc7(block.data(), expected.data() + x * 4, x, 0, width, Height);
    }
    return RunBlockDecodeProbe(
        device,
        BlockDecodeProbeCase{
            .name = "BC7",
            .format = 6,
            .output_format = VK_FORMAT_A8B8G8R8_UNORM_PACK32,
            .code = BCN_BPTC_DECODER_RGBA8_COMP_SPV,
            .code_size = sizeof(BCN_BPTC_DECODER_RGBA8_COMP_SPV),
            .width = width,
            .height = Height,
            .bytes_per_pixel = 4,
            .dispatch_x = static_cast<u32>((Modes.size() + 7) / 8),
            .input = input,
            .expected = expected,
        });
}

[[nodiscard]] bool RunBc6DecodeProbe(const Device& device, bool is_signed) {
    constexpr std::array<u32, 14> Modes{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16};
    constexpr u32 Height = 4;
    const u32 width = static_cast<u32>(Modes.size()) * 4;
    std::vector<u8> input(2048);
    std::vector<u8> expected(static_cast<size_t>(width) * Height * 8);

    for (u32 index = 0; index < Modes.size(); ++index) {
        const auto block = MakeBc6ProbeBlock(Modes[index]);
        const u32 offset = ProbeBlockLinearOffset(index);
        if (offset + block.size() > input.size()) {
            return false;
        }
        std::memcpy(input.data() + offset, block.data(), block.size());
        const size_t x = static_cast<size_t>(index) * 4;
        bcn::DecodeBc6(block.data(), expected.data() + x * 8, x, 0, width, Height, is_signed);
    }
    return RunBlockDecodeProbe(
        device,
        BlockDecodeProbeCase{
            .name = is_signed ? "BC6H_SFLOAT" : "BC6H_UFLOAT",
            .format = is_signed ? 5u : 4u,
            .output_format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .code = BCN_BPTC_DECODER_RGBA16F_COMP_SPV,
            .code_size = sizeof(BCN_BPTC_DECODER_RGBA16F_COMP_SPV),
            .width = width,
            .height = Height,
            .bytes_per_pixel = 8,
            .dispatch_x = static_cast<u32>((Modes.size() + 7) / 8),
            .input = input,
            .expected = expected,
        });
}

void CaptureStaticProfile(const Device& device, XclipseOptimizationProbeResults& results) {
    const auto queue_properties = device.GetPhysical().GetQueueFamilyProperties();
    results.queue_family_count = static_cast<u32>(queue_properties.size());
    for (const auto& queue : queue_properties) {
        if (queue.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            results.graphics_queue_count += queue.queueCount;
        }
        if ((queue.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(queue.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            results.dedicated_compute_queue_count += queue.queueCount;
        }
        if ((queue.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(queue.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))) {
            results.dedicated_transfer_queue_count += queue.queueCount;
        }
    }

    const auto properties = device.GetPhysical().GetProperties();
    results.max_memory_allocation_count = properties.limits.maxMemoryAllocationCount;
    results.max_compute_workgroup_invocations =
        properties.limits.maxComputeWorkGroupInvocations;
    results.max_image_dimension_2d = properties.limits.maxImageDimension2D;
    results.non_coherent_atom_size = properties.limits.nonCoherentAtomSize;
    results.buffer_image_granularity = properties.limits.bufferImageGranularity;
    results.optimal_buffer_copy_offset_alignment =
        properties.limits.optimalBufferCopyOffsetAlignment;
    results.optimal_buffer_copy_row_pitch_alignment =
        properties.limits.optimalBufferCopyRowPitchAlignment;

    const auto memory = device.GetPhysical().GetMemoryProperties();
    results.memory_type_count = memory.memoryProperties.memoryTypeCount;

    std::array<bool, VK_MAX_MEMORY_HEAPS> device_local_heaps{};
    std::array<bool, VK_MAX_MEMORY_HEAPS> host_visible_heaps{};
    for (u32 index = 0; index < memory.memoryProperties.memoryTypeCount; ++index) {
        const auto& type = memory.memoryProperties.memoryTypes[index];
        const auto flags = type.propertyFlags;
        if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            ++results.device_local_memory_type_count;
            if (!device_local_heaps[type.heapIndex]) {
                device_local_heaps[type.heapIndex] = true;
                results.device_local_heap_bytes +=
                    memory.memoryProperties.memoryHeaps[type.heapIndex].size;
            }
        }
        if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            if (!host_visible_heaps[type.heapIndex]) {
                host_visible_heaps[type.heapIndex] = true;
                results.host_visible_heap_bytes +=
                    memory.memoryProperties.memoryHeaps[type.heapIndex].size;
            }
            if ((flags & (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) != 0) {
                ++results.host_visible_coherent_memory_type_count;
            }
            if ((flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0) {
                ++results.host_visible_cached_memory_type_count;
            }
        }
    }

    const auto graphics_properties = queue_properties[device.GetGraphicsFamily()];
    results.timestamp_valid_bits = graphics_properties.timestampValidBits;
    results.timestamp_period_ps =
        static_cast<u64>(std::llround(static_cast<double>(properties.limits.timestampPeriod) * 1000.0));
    if (graphics_properties.timestampValidBits != 0 && properties.limits.timestampPeriod > 0.0f) {
        results.timestamp_queries = CapabilityState::Advertised;
    }
}

} // namespace


bool RunXclipseRgtcDecodeValidationProbe(const Device& device) {
    if (!device.IsXclipse() || device.HasBrokenCompute()) {
        return false;
    }

    const std::array probes{
        RgtcProbeCase{
            .name = "BC4_UNORM",
            .format = 0,
            .output_format = VK_FORMAT_R8_UNORM,
            .code = BCN_DECODER_R8_COMP_SPV,
            .code_size = sizeof(BCN_DECODER_R8_COMP_SPV),
            .bytes_per_pixel = 1,
            .is_bc5 = false,
            .is_signed = false,
            .left_r = 255,
            .left_g = 0,
            .right_r = 64,
            .right_g = 0,
        },
        RgtcProbeCase{
            .name = "BC4_SNORM",
            .format = 1,
            .output_format = VK_FORMAT_R8_SNORM,
            .code = BCN_DECODER_R8_SNORM_COMP_SPV,
            .code_size = sizeof(BCN_DECODER_R8_SNORM_COMP_SPV),
            .bytes_per_pixel = 1,
            .is_bc5 = false,
            .is_signed = true,
            .left_r = 64,
            .left_g = 0,
            .right_r = -64,
            .right_g = 0,
        },
        RgtcProbeCase{
            .name = "BC5_UNORM",
            .format = 2,
            .output_format = VK_FORMAT_R8G8_UNORM,
            .code = BCN_DECODER_RG8_COMP_SPV,
            .code_size = sizeof(BCN_DECODER_RG8_COMP_SPV),
            .bytes_per_pixel = 2,
            .is_bc5 = true,
            .is_signed = false,
            .left_r = 255,
            .left_g = 32,
            .right_r = 64,
            .right_g = 200,
        },
        RgtcProbeCase{
            .name = "BC5_SNORM",
            .format = 3,
            .output_format = VK_FORMAT_R8G8_SNORM,
            .code = BCN_DECODER_RG8_SNORM_COMP_SPV,
            .code_size = sizeof(BCN_DECODER_RG8_SNORM_COMP_SPV),
            .bytes_per_pixel = 2,
            .is_bc5 = true,
            .is_signed = true,
            .left_r = 64,
            .left_g = -64,
            .right_r = -32,
            .right_g = 32,
        },
    };

    u32 failures{};
    for (const auto& probe : probes) {
        if (!RunRgtcDecodeCase(device, probe)) {
            ++failures;
        }
    }
    LOG_INFO(Render_Vulkan, "XCLIPSE RGTC PROBES cases={} failures={} validated={}",
             probes.size(), failures, failures == 0);
    return failures == 0;
}


XclipseBptcDecodeValidation RunXclipseBptcDecodeValidationProbe(const Device& device) {
    XclipseBptcDecodeValidation result{};
    if (!device.IsXclipse() || device.HasBrokenCompute()) {
        return result;
    }

    const bool bc6_capable = device.IsFormatSupported(
        VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT, FormatType::Optimal);
    const bool bc7_capable = device.IsFormatSupported(
        VK_FORMAT_A8B8G8R8_UNORM_PACK32, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT, FormatType::Optimal);
    const auto run = [](const char* name, auto&& probe) {
        try {
            return probe();
        } catch (const vk::Exception& exception) {
            LOG_WARNING(Render_Vulkan, "XCLIPSE PROBE {} decode validation exception: {}", name,
                        exception.what());
            return false;
        }
    };

    bool bc6_unsigned{};
    bool bc6_signed{};
    if (bc6_capable) {
        bc6_unsigned = run("BC6H unsigned", [&] { return RunBc6DecodeProbe(device, false); });
        bc6_signed = run("BC6H signed", [&] { return RunBc6DecodeProbe(device, true); });
        result.bc6 = bc6_unsigned && bc6_signed;
    } else {
        LOG_INFO(Render_Vulkan,
                 "XCLIPSE PROBE BC6H decode skipped: RGBA16F storage images unsupported");
    }
    if (bc7_capable) {
        result.bc7 = run("BC7", [&] { return RunBc7DecodeProbe(device); });
    } else {
        LOG_INFO(Render_Vulkan,
                 "XCLIPSE PROBE BC7 decode skipped: RGBA8 storage images unsupported");
    }
    LOG_INFO(Render_Vulkan,
             "XCLIPSE BPTC PROBES bc6_capable={} bc6_unsigned={} bc6_signed={} "
             "bc6_validated={} bc7_capable={} bc7_validated={}",
             bc6_capable, bc6_unsigned, bc6_signed, result.bc6, bc7_capable, result.bc7);
    return result;
}

void RunXclipseOptimizationProbeSuite(const Device& device,
                                      XclipseOptimizationProbeResults& results) {
    if (!device.IsXclipse()) {
        return;
    }

    results.ResetTransferMeasurements();
    CaptureStaticProfile(device, results);

    results.empty_queue_submit =
        RunEmptySubmitProbe(device, results.empty_submit_ns)
            ? CapabilityState::Validated
            : CapabilityState::Advertised;

    if (RunBufferTransferProbe(device, results)) {
        results.buffer_transfer = CapabilityState::Validated;
        if (results.timestamp_timing_validated) {
            // Timestamp support is recorded from queue/limit capability. The current wrapper does not
        // expose vkCmdWriteTimestamp, so probe timing remains CPU-side until that API is surfaced.
        results.timestamp_queries = results.timestamp_queries == CapabilityState::Validated
                                        ? results.timestamp_queries
                                        : CapabilityState::Advertised;
        }
    } else {
        results.buffer_transfer = CapabilityState::Advertised;
    }

    try {
        (void)RunImageTransferProbe(device, results);
    } catch (const vk::Exception& exception) {
        LOG_WARNING(Render_Vulkan, "XCLIPSE PROBE image transfer exception: {}", exception.what());
        results.image_transfer = CapabilityState::Advertised;
    }

    LOG_INFO(Render_Vulkan,
             "XCLIPSE OPT PROBES queues={} gfx={} compute_dedicated={} transfer_dedicated={} "
             "memory_types={} device_local_types={} host_coherent_types={} host_cached_types={} "
             "device_local_heap={} host_visible_heap={} timestamps={} valid_bits={} period_ps={} "
             "empty_submit_ns={} copy64k_ns={} copy1m_ns={} copy4m_ns={} image_transfer={} "
             "storage_image_create={} r32_sample={} r32_compare={} r32_dref={} d32_dref={} mutable_r32_d32={}",
             results.queue_family_count, results.graphics_queue_count,
             results.dedicated_compute_queue_count, results.dedicated_transfer_queue_count,
             results.memory_type_count, results.device_local_memory_type_count,
             results.host_visible_coherent_memory_type_count,
             results.host_visible_cached_memory_type_count, results.device_local_heap_bytes,
             results.host_visible_heap_bytes, CapabilityStateName(results.timestamp_queries),
             results.timestamp_valid_bits, results.timestamp_period_ps, results.empty_submit_ns,
             results.copy_64k_ns, results.copy_1m_ns, results.copy_4m_ns,
             CapabilityStateName(results.image_transfer),
             CapabilityStateName(results.storage_image_create),
             CapabilityStateName(results.r32_sampled_image),
             CapabilityStateName(results.r32_compare_non_dref),
             CapabilityStateName(results.r32_dref_sample),
             CapabilityStateName(results.d32_compare_dref),
             CapabilityStateName(results.mutable_r32_d32_view));
}

} // namespace Vulkan
