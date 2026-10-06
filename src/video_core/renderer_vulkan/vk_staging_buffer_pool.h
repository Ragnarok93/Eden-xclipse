// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <chrono>
#include <climits>
#include <optional>
#include <vector>

#include "common/common_types.h"

#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include "video_core/vulkan_common/xclipse_memory_pressure.h"

namespace Vulkan {

class Device;
class Scheduler;

struct StagingBufferRef {
    VkBuffer buffer;
    VkDeviceAddress device_address;
    VkDeviceSize offset;
    std::span<u8> mapped_span;
    MemoryUsage usage;
    u32 log2_level;
    u64 index;
};

struct StagingBufferPoolStats {
    u64 stream_bytes{};
    u64 stream_upload_requests{};
    u64 stream_upload_request_bytes{};
    u64 stream_size_bypasses{};
    u64 stream_size_bypass_bytes{};
    u64 stream_ring_conflicts{};
    u64 stream_ring_conflict_bytes{};
    u64 stream_ring_wraps{};
    u64 cached_device_local_bytes{};
    u64 cached_upload_bytes{};
    u64 cached_download_bytes{};
    u64 active_cached_bytes{};
    u64 deferred_cached_bytes{};
    u64 total_bytes{};
    u64 peak_total_bytes{};
    u64 allocations{};
    u64 reuses{};
    u64 releases{};
    u64 released_bytes{};
    u64 pressure_releases{};
    u64 pressure_released_bytes{};
    u64 pressure_waits{};
    u64 pressure_wait_reused_bytes{};
    u64 pressure_pending_releases{};
    u64 pressure_pending_release_bytes{};
    u64 cache_limit_hits{};
    u64 over_limit_allocations{};
    u64 cache_limit_bytes{};
    u64 largest_upload_bucket_bytes{};
    u64 largest_free_upload_bucket_bytes{};
    u64 largest_active_upload_bucket_bytes{};
};

struct StagingPressureReclaimResult {
    u64 before_cached_bytes{};
    u64 after_cached_bytes{};
    u64 released_bytes{};
};

class StagingBufferPool {
public:
    static constexpr size_t NUM_SYNCS = 16;

    explicit StagingBufferPool(const Device& device, MemoryAllocator& memory_allocator,
                               Scheduler& scheduler);
    ~StagingBufferPool();

    StagingBufferRef Request(size_t size, MemoryUsage usage, bool deferred = false);
    void FreeDeferred(StagingBufferRef& ref);

    [[nodiscard]] VkBuffer StreamBuf() const noexcept {
        return *stream_buffer;
    }

    /// Apply the latest Android/Xclipse pressure state. Every fresh Elevated-or-higher sample
    /// immediately drops GPU-free cache entries; active/deferred allocations are never destroyed.
    [[nodiscard]] StagingPressureReclaimResult ApplyMemoryPressure(
        MemoryPressureClass pressure);

    /// Run normal incremental cache cleanup.
    void TickFrame();

    [[nodiscard]] StagingBufferPoolStats Stats() const;

private:
    struct StreamBufferCommit {
        size_t upper_bound;
        u64 tick;
    };

    struct StagingBuffer {
        vk::Buffer buffer;
        VkDeviceAddress device_address;
        std::span<u8> mapped_span;
        MemoryUsage usage;
        u32 log2_level;
        u64 index;
        u64 tick = 0;
        bool deferred{};

        StagingBufferRef Ref() const noexcept {
            return {
                .buffer = *buffer,
                .device_address = device_address,
                .offset = 0,
                .mapped_span = mapped_span,
                .usage = usage,
                .log2_level = log2_level,
                .index = index,
            };
        }
    };

    struct StagingBuffers {
        std::vector<StagingBuffer> entries;
        size_t delete_index = 0;
        size_t iterate_index = 0;
    };

    static constexpr size_t NUM_LEVELS = sizeof(size_t) * CHAR_BIT;
    using StagingBuffersCache = std::array<StagingBuffers, NUM_LEVELS>;

    struct PressureReleaseBucket {
        u64 pending_count{};
        std::chrono::steady_clock::time_point first_release{};
    };
    using PressureReleaseBuckets = std::array<PressureReleaseBucket, NUM_LEVELS>;

    StagingBufferRef GetStreamBuffer(size_t size);
    void AccountStreamFallback(size_t size, bool ring_conflict) noexcept;

    bool AreRegionsActive(size_t region_begin, size_t region_end) const;

    StagingBufferRef GetStagingBuffer(size_t size, MemoryUsage usage, bool deferred = false);

    std::optional<StagingBufferRef> TryGetReservedBuffer(size_t size, MemoryUsage usage,
                                                         bool deferred);

    StagingBufferRef CreateStagingBuffer(size_t size, MemoryUsage usage, bool deferred);
    std::optional<StagingBufferRef> TryWaitAndReuseBuffer(size_t size, MemoryUsage usage,
                                                          bool deferred);

    StagingBuffersCache& GetCache(MemoryUsage usage);

    void ReleaseCache(MemoryUsage usage);
    void ReleaseAllFree(MemoryUsage usage);

    void ReleaseLevel(StagingBuffersCache& cache, MemoryUsage usage, size_t log2);
    void AccountAllocation(MemoryUsage usage, u64 bytes, size_t log2);
    void AccountRelease(MemoryUsage usage, u64 bytes, u64 count, bool pressure, size_t log2);
    void TrackPressureRelease(MemoryUsage usage, size_t log2, u64 count);
    void TrackPressureReallocation(MemoryUsage usage, size_t log2, u64 bytes);
    PressureReleaseBuckets& PressureBuckets(MemoryUsage usage);
    const PressureReleaseBuckets& PressureBuckets(MemoryUsage usage) const;
    [[nodiscard]] u64 CachedBytes() const noexcept;
    size_t Region(size_t iter) const noexcept {
        return iter / region_size;
    }

    const Device& device;
    MemoryAllocator& memory_allocator;
    Scheduler& scheduler;

    vk::Buffer stream_buffer;
    VkDeviceAddress stream_buffer_address{};
    std::span<u8> stream_pointer;
    VkDeviceSize stream_buffer_size;
    VkDeviceSize region_size;

    size_t iterator = 0;
    size_t used_iterator = 0;
    size_t free_iterator = 0;
    std::array<u64, NUM_SYNCS> sync_ticks{};

    StagingBuffersCache device_local_cache;
    StagingBuffersCache upload_cache;
    StagingBuffersCache download_cache;
    PressureReleaseBuckets device_local_pressure_releases;
    PressureReleaseBuckets upload_pressure_releases;
    PressureReleaseBuckets download_pressure_releases;

    size_t current_delete_level = 0;
    u64 buffer_index = 0;
    u64 unique_ids{};

    u64 stream_upload_request_count{};
    u64 stream_upload_request_bytes{};
    u64 stream_size_bypass_count{};
    u64 stream_size_bypass_bytes{};
    u64 stream_ring_conflict_count{};
    u64 stream_ring_conflict_bytes{};
    u64 stream_ring_wrap_count{};

    u64 cached_device_local_bytes{};
    u64 cached_upload_bytes{};
    u64 cached_download_bytes{};
    u64 peak_total_bytes{};
    u64 allocation_count{};
    u64 reuse_count{};
    u64 release_count{};
    u64 released_bytes{};
    u64 pressure_release_count{};
    u64 pressure_released_bytes{};
    u64 pressure_wait_count{};
    u64 pressure_wait_reused_bytes{};
    u64 cache_limit_hits{};
    u64 over_limit_allocations{};
    MemoryPressureClass memory_pressure{MemoryPressureClass::Normal};
};

} // namespace Vulkan
