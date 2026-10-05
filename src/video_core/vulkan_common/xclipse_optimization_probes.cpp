// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/vulkan_common/xclipse_optimization_probes.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <optional>

#include "common/common_types.h"
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

[[nodiscard]] bool CreateBuffer(const vk::DeviceDispatch& dld, VkDevice device,
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

[[nodiscard]] bool SubmitAndWait(const vk::DeviceDispatch& dld, const vk::Device& logical,
                                 VkDevice device, vk::Queue queue, VkCommandBuffer command_buffer) {
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
    const VkResult submit_result = queue.Submit(vk::Span{submit_info}, fence.fence);
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
    const bool valid = SubmitAndWait(dld, device.GetLogical(), raw_device, queue, command_buffer);
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
        !CreateBuffer(dld, raw_device, memory_properties, MaxSize,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                      destination, false) ||
        !CreateBuffer(dld, raw_device, memory_properties, MaxSize,
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
        const bool submitted = SubmitAndWait(dld, device.GetLogical(), raw_device,
                                         device.GetGraphicsQueue(), command_buffer);
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
    if (!CreateBuffer(dld, raw_device, memory_properties, Bytes,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      VK_MEMORY_PROPERTY_HOST_CACHED_BIT, source, true) ||
        !CreateBuffer(dld, raw_device, memory_properties, Bytes,
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

    const bool valid = SubmitAndWait(dld, device.GetLogical(), raw_device,
                                     device.GetGraphicsQueue(), command_buffer);
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

void RunXclipseOptimizationProbeSuite(const Device& device,
                                      XclipseOptimizationProbeResults& results) {
    if (!device.IsXclipse()) {
        return;
    }

    results = {};
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
             "storage_image_create={}",
             results.queue_family_count, results.graphics_queue_count,
             results.dedicated_compute_queue_count, results.dedicated_transfer_queue_count,
             results.memory_type_count, results.device_local_memory_type_count,
             results.host_visible_coherent_memory_type_count,
             results.host_visible_cached_memory_type_count, results.device_local_heap_bytes,
             results.host_visible_heap_bytes, CapabilityStateName(results.timestamp_queries),
             results.timestamp_valid_bits, results.timestamp_period_ps, results.empty_submit_ns,
             results.copy_64k_ns, results.copy_1m_ns, results.copy_4m_ns,
             CapabilityStateName(results.image_transfer),
             CapabilityStateName(results.storage_image_create));
}

} // namespace Vulkan
