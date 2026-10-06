// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <utility>
#include <vector>

#include <fmt/ranges.h>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/bit_util.h"
#include "common/common_types.h"
#include "common/literals.h"
#include "common/settings.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_staging_buffer_pool.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
namespace {

using namespace Common::Literals;

// Maximum potential alignment of a Vulkan buffer
constexpr VkDeviceSize MAX_ALIGNMENT = 256;

// Stream buffer size in bytes
// *NIX drivers are more sensitive to increased buffers for streaming.
// Windows ones however, can intake bigger buffers and generally do not OOM.
// - GTX 960 on Windows will not OOM with 256mib
// - GT 1030 on ^NIX will OOM with 256mib
#if defined(__FreeBSD__)
constexpr VkDeviceSize MAX_STREAM_BUFFER_SIZE = 128_MiB;
#else
constexpr VkDeviceSize MAX_STREAM_BUFFER_SIZE = 256_MiB;
#endif

size_t GetStreamBufferSize(const Device& device) {
    const bool pressure_limited =
        device.IsXclipse() && Settings::values.xclipse_memory_pressure_monitor.GetValue();
    if (!device.HasDebuggingToolAttached()) {
        return static_cast<size_t>(
            XclipsePressureStreamBufferSize(MAX_STREAM_BUFFER_SIZE, pressure_limited));
    }

    VkDeviceSize size{0};
    bool has_device_local_host_visible_heap{};
    ForEachDeviceLocalHostVisibleHeap(device, [&size, &has_device_local_host_visible_heap](
                                                  size_t index, VkMemoryHeap& heap) {
        has_device_local_host_visible_heap = true;
        size = (std::max)(size, heap.size);
    });
    if (has_device_local_host_visible_heap) {
        // If rebar is not supported, cut the max heap size to 40%. This will allow 2 captures to be
        // loaded at the same time in RenderDoc. If rebar is supported, this shouldn't be an issue
        // as the heap will be much larger.
        if (size <= MAX_STREAM_BUFFER_SIZE) {
            size = size * 40 / 100;
        }
    } else {
        size = MAX_STREAM_BUFFER_SIZE;
    }
    const VkDeviceSize stream_size =
        (std::min)(Common::AlignUp(size, MAX_ALIGNMENT), MAX_STREAM_BUFFER_SIZE);
    return static_cast<size_t>(XclipsePressureStreamBufferSize(stream_size, pressure_limited));
}
} // Anonymous namespace

StagingBufferPool::StagingBufferPool(const Device& device_, MemoryAllocator& memory_allocator_,
                                     Scheduler& scheduler_)
    : device{device_}, memory_allocator{memory_allocator_}, scheduler{scheduler_},
      stream_buffer_size{GetStreamBufferSize(device)}, region_size{stream_buffer_size /
                                                                   StagingBufferPool::NUM_SYNCS} {
    VkBufferCreateInfo stream_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = stream_buffer_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    if (device.IsExtTransformFeedbackSupported()) {
        stream_ci.usage |= VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT;
    }
    if (device.IsBufferDeviceAddressSupported()) {
        stream_ci.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    stream_buffer = memory_allocator.CreateBuffer(stream_ci, MemoryUsage::Stream);
    if (device.HasDebuggingToolAttached()) {
        stream_buffer.SetObjectNameEXT("Stream Buffer");
    }
    if (device.IsBufferDeviceAddressSupported()) {
        stream_buffer_address = device.GetLogical().GetBufferDeviceAddress(*stream_buffer);
    }
    stream_pointer = stream_buffer.Mapped();
    ASSERT_MSG(!stream_pointer.empty(), "Stream buffer must be host visible!");
    peak_total_bytes = static_cast<u64>(stream_buffer_size);
}

StagingBufferPool::~StagingBufferPool() = default;

StagingBufferRef StagingBufferPool::Request(size_t size, MemoryUsage usage, bool deferred) {
    if (!deferred && usage == MemoryUsage::Upload) {
        if (device.IsXclipse()) {
            ++stream_upload_request_count;
            stream_upload_request_bytes += size;
        }
        if (size <= region_size) {
            return GetStreamBuffer(size);
        }
        AccountStreamFallback(size, false);
    }
    return GetStagingBuffer(size, usage, deferred);
}

void StagingBufferPool::AccountStreamFallback(size_t size, bool ring_conflict) noexcept {
    if (!device.IsXclipse()) {
        return;
    }
    if (ring_conflict) {
        ++stream_ring_conflict_count;
        stream_ring_conflict_bytes += size;
    } else {
        ++stream_size_bypass_count;
        stream_size_bypass_bytes += size;
    }
}

void StagingBufferPool::FreeDeferred(StagingBufferRef& ref) {
    auto& entries = GetCache(ref.usage)[ref.log2_level].entries;
    const auto is_this_one = [&ref](const StagingBuffer& entry) {
        return entry.index == ref.index;
    };
    auto it = std::find_if(entries.begin(), entries.end(), is_this_one);
    ASSERT(it != entries.end());
    ASSERT(it->deferred);
    it->tick = scheduler.CurrentTick();
    it->deferred = false;
}

StagingPressureReclaimResult StagingBufferPool::ApplyMemoryPressure(
    MemoryPressureClass pressure) {
    memory_pressure = device.IsXclipse() ? pressure : MemoryPressureClass::Normal;

    StagingPressureReclaimResult result{
        .before_cached_bytes = CachedBytes(),
    };
    if (ShouldReclaimFreeStaging(memory_pressure)) {
        ReleaseAllFree(MemoryUsage::DeviceLocal);
        ReleaseAllFree(MemoryUsage::Upload);
        ReleaseAllFree(MemoryUsage::Download);
    }
    result.after_cached_bytes = CachedBytes();
    result.released_bytes = result.before_cached_bytes - result.after_cached_bytes;
    return result;
}

void StagingBufferPool::TickFrame() {
    current_delete_level = (current_delete_level + 1) % NUM_LEVELS;

    ReleaseCache(MemoryUsage::DeviceLocal);
    ReleaseCache(MemoryUsage::Upload);
    ReleaseCache(MemoryUsage::Download);
}

StagingBufferPoolStats StagingBufferPool::Stats() const {
    StagingBufferPoolStats stats{
        .stream_bytes = static_cast<u64>(stream_buffer_size),
        .stream_upload_requests = stream_upload_request_count,
        .stream_upload_request_bytes = stream_upload_request_bytes,
        .stream_size_bypasses = stream_size_bypass_count,
        .stream_size_bypass_bytes = stream_size_bypass_bytes,
        .stream_ring_conflicts = stream_ring_conflict_count,
        .stream_ring_conflict_bytes = stream_ring_conflict_bytes,
        .stream_ring_wraps = stream_ring_wrap_count,
        .cached_device_local_bytes = cached_device_local_bytes,
        .cached_upload_bytes = cached_upload_bytes,
        .cached_download_bytes = cached_download_bytes,
        .total_bytes = static_cast<u64>(stream_buffer_size) + CachedBytes(),
        .peak_total_bytes = peak_total_bytes,
        .allocations = allocation_count,
        .reuses = reuse_count,
        .releases = release_count,
        .released_bytes = released_bytes,
        .pressure_releases = pressure_release_count,
        .pressure_released_bytes = pressure_released_bytes,
        .pressure_waits = pressure_wait_count,
        .pressure_wait_reused_bytes = pressure_wait_reused_bytes,
        .cache_limit_hits = cache_limit_hits,
        .over_limit_allocations = over_limit_allocations,
        .cache_limit_bytes = XclipseStagingCacheLimitBytes(memory_pressure),
    };

    const auto accumulate_activity = [this, &stats](const StagingBuffersCache& cache) {
        for (size_t log2 = 0; log2 < cache.size(); ++log2) {
            const u64 bytes = u64{1} << log2;
            for (const auto& entry : cache[log2].entries) {
                if (entry.deferred) {
                    stats.deferred_cached_bytes += bytes;
                    stats.active_cached_bytes += bytes;
                } else if (!scheduler.IsFree(entry.tick)) {
                    stats.active_cached_bytes += bytes;
                }
            }
        }
    };
    accumulate_activity(device_local_cache);
    accumulate_activity(upload_cache);
    accumulate_activity(download_cache);

    const auto accumulate_pending_pressure = [&stats](const PressureReleaseBuckets& buckets) {
        for (size_t log2 = 0; log2 < buckets.size(); ++log2) {
            const u64 count = buckets[log2].pending_count;
            stats.pressure_pending_releases += count;
            stats.pressure_pending_release_bytes += count * (u64{1} << log2);
        }
    };
    accumulate_pending_pressure(device_local_pressure_releases);
    accumulate_pending_pressure(upload_pressure_releases);
    accumulate_pending_pressure(download_pressure_releases);

    for (size_t log2 = 0; log2 < upload_cache.size(); ++log2) {
        const auto& entries = upload_cache[log2].entries;
        if (entries.empty()) {
            continue;
        }
        const u64 bytes = u64{1} << log2;
        stats.largest_upload_bucket_bytes = (std::max)(stats.largest_upload_bucket_bytes, bytes);
        for (const auto& entry : entries) {
            if (!entry.deferred && scheduler.IsFree(entry.tick)) {
                stats.largest_free_upload_bucket_bytes =
                    (std::max)(stats.largest_free_upload_bucket_bytes, bytes);
            } else {
                stats.largest_active_upload_bucket_bytes =
                    (std::max)(stats.largest_active_upload_bucket_bytes, bytes);
            }
        }
    }
    return stats;
}

StagingBufferRef StagingBufferPool::GetStreamBuffer(size_t size) {
    if (AreRegionsActive(Region(free_iterator) + 1,
                         (std::min)(Region(iterator + size) + 1, NUM_SYNCS))) {
        // Avoid waiting for the previous usages to be free
        AccountStreamFallback(size, true);
        return GetStagingBuffer(size, MemoryUsage::Upload);
    }
    const u64 current_tick = scheduler.CurrentTick();
    std::fill(sync_ticks.begin() + Region(used_iterator), sync_ticks.begin() + Region(iterator),
              current_tick);
    used_iterator = iterator;
    free_iterator = (std::max)(free_iterator, iterator + size);

    if (iterator + size >= stream_buffer_size) {
        if (device.IsXclipse()) {
            ++stream_ring_wrap_count;
        }
        std::fill(sync_ticks.begin() + Region(used_iterator), sync_ticks.begin() + NUM_SYNCS,
                  current_tick);
        used_iterator = 0;
        iterator = 0;
        free_iterator = size;

        if (AreRegionsActive(0, Region(size) + 1)) {
            // Avoid waiting for the previous usages to be free
            AccountStreamFallback(size, true);
            return GetStagingBuffer(size, MemoryUsage::Upload);
        }
    }
    const size_t offset = iterator;
    iterator = Common::AlignUp(iterator + size, MAX_ALIGNMENT);
    return StagingBufferRef{
        .buffer = *stream_buffer,
        .device_address = stream_buffer_address,
        .offset = static_cast<VkDeviceSize>(offset),
        .mapped_span = stream_pointer.subspan(offset, size),
        .usage{},
        .log2_level{},
        .index{},
    };
}

bool StagingBufferPool::AreRegionsActive(size_t region_begin, size_t region_end) const {
    const u64 gpu_tick = scheduler.GetMasterSemaphore().KnownGpuTick();
    return std::any_of(sync_ticks.begin() + region_begin, sync_ticks.begin() + region_end,
                       [gpu_tick](u64 sync_tick) { return gpu_tick < sync_tick; });
};

StagingBufferRef StagingBufferPool::GetStagingBuffer(size_t size, MemoryUsage usage,
                                                     bool deferred) {
    if (const std::optional<StagingBufferRef> ref = TryGetReservedBuffer(size, usage, deferred)) {
        return *ref;
    }

    const u32 log2_size = Common::Log2Ceil<u32>(u32(size));
    const u64 allocation_bytes = u64{1} << log2_size;
    const u64 cache_limit = XclipseStagingCacheLimitBytes(memory_pressure);
    if (device.IsXclipse() && cache_limit != 0 &&
        CachedBytes() + allocation_bytes > cache_limit) {
        ++cache_limit_hits;

        // Reclaim every already-free bucket before growing the unified-memory footprint.
        ReleaseAllFree(MemoryUsage::DeviceLocal);
        ReleaseAllFree(MemoryUsage::Upload);
        ReleaseAllFree(MemoryUsage::Download);
        if (const std::optional<StagingBufferRef> ref =
                TryGetReservedBuffer(size, usage, deferred)) {
            return *ref;
        }

        // Under High/Critical pressure, trading one bounded GPU wait for an existing compatible
        // allocation is preferable to another potentially-hundreds-of-MiB power-of-two buffer.
        if (ShouldPreferStagingWaitReuse(memory_pressure)) {
            if (const std::optional<StagingBufferRef> ref =
                    TryWaitAndReuseBuffer(size, usage, deferred)) {
                return *ref;
            }
        }

        // Some single requests are themselves larger than the pressure ceiling. They must still
        // succeed, but keep them visible so the next on-device log can distinguish unavoidable
        // working-set size from avoidable cache growth.
        ++over_limit_allocations;
    }
    return CreateStagingBuffer(size, usage, deferred);
}

std::optional<StagingBufferRef> StagingBufferPool::TryGetReservedBuffer(size_t size,
                                                                        MemoryUsage usage,
                                                                        bool deferred) {
    StagingBuffers& cache_level = GetCache(usage)[Common::Log2Ceil(size)];

    const auto is_free = [this](const StagingBuffer& entry) {
        return !entry.deferred && scheduler.IsFree(entry.tick);
    };
    auto& entries = cache_level.entries;
    const auto hint_it = entries.begin() + cache_level.iterate_index;
    auto it = std::find_if(entries.begin() + cache_level.iterate_index, entries.end(), is_free);
    if (it == entries.end()) {
        it = std::find_if(entries.begin(), hint_it, is_free);
        if (it == hint_it) {
            return std::nullopt;
        }
    }
    cache_level.iterate_index = std::distance(entries.begin(), it) + 1;
    it->tick = deferred ? (std::numeric_limits<u64>::max)() : scheduler.CurrentTick();
    ASSERT(!it->deferred);
    it->deferred = deferred;
    ++reuse_count;
    return it->Ref();
}

std::optional<StagingBufferRef> StagingBufferPool::TryWaitAndReuseBuffer(
    size_t size, MemoryUsage usage, bool deferred) {
    if (deferred || !ShouldPreferStagingWaitReuse(memory_pressure)) {
        return std::nullopt;
    }

    const u32 requested_log2 = Common::Log2Ceil<u32>(u32(size));
    StagingBuffer* candidate = nullptr;
    StagingBuffers* candidate_cache = nullptr;
    u32 candidate_log2 = 0;

    // Do not restrict pressure reuse to the exact size bucket. A larger idle staging allocation
    // is already committed memory and can satisfy a smaller request without another allocation.
    // This is especially important on unified-memory Xclipse, where repeated power-of-two misses
    // can otherwise grow the active working set faster than the pressure sampler can react.
    auto& cache = GetCache(usage);
    for (u32 log2 = requested_log2; log2 < NUM_LEVELS; ++log2) {
        auto& entries = cache[log2].entries;
        for (auto& entry : entries) {
            if (entry.deferred) {
                continue;
            }
            if (!candidate || entry.tick < candidate->tick ||
                (entry.tick == candidate->tick && log2 < candidate_log2)) {
                candidate = &entry;
                candidate_cache = &cache[log2];
                candidate_log2 = log2;
            }
        }
        if (candidate && candidate_log2 == requested_log2) {
            break;
        }
    }

    if (!candidate || !candidate_cache) {
        return std::nullopt;
    }

    scheduler.Wait(candidate->tick, 0.0, XclipseWaitSource::StagingPressure);
    candidate->tick = scheduler.CurrentTick();
    candidate->deferred = false;
    ++reuse_count;
    ++pressure_wait_count;
    pressure_wait_reused_bytes += u64{1} << candidate->log2_level;
    return candidate->Ref();
}

StagingBufferRef StagingBufferPool::CreateStagingBuffer(size_t size, MemoryUsage usage, bool deferred) {
    auto const log2_size = Common::Log2Ceil<u32>(u32(size));
    VkBufferCreateInfo buffer_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = 1ULL << log2_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    if (device.IsExtTransformFeedbackSupported()) {
        buffer_ci.usage |= VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT;
    }
    if (device.IsBufferDeviceAddressSupported()) {
        buffer_ci.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    vk::Buffer buffer = memory_allocator.CreateBuffer(buffer_ci, usage);
    if (device.HasDebuggingToolAttached()) {
        ++buffer_index;
        buffer.SetObjectNameEXT(fmt::format("Staging Buffer {}", buffer_index).c_str());
    }
    const std::span<u8> mapped_span = buffer.Mapped();
    const VkDeviceAddress buffer_address =
        device.IsBufferDeviceAddressSupported()
            ? device.GetLogical().GetBufferDeviceAddress(*buffer)
            : VkDeviceAddress{};
    StagingBuffer& entry = GetCache(usage)[log2_size].entries.emplace_back(StagingBuffer{
        .buffer = std::move(buffer),
        .device_address = buffer_address,
        .mapped_span = mapped_span,
        .usage = usage,
        .log2_level = log2_size,
        .index = unique_ids++,
        .tick = deferred ? (std::numeric_limits<u64>::max)() : scheduler.CurrentTick(),
        .deferred = deferred,
    });
    AccountAllocation(usage, u64{1} << log2_size, log2_size);
    return entry.Ref();
}

StagingBufferPool::StagingBuffersCache& StagingBufferPool::GetCache(MemoryUsage usage) {
    switch (usage) {
    case MemoryUsage::DeviceLocal:
        return device_local_cache;
    case MemoryUsage::Upload:
        return upload_cache;
    case MemoryUsage::Download:
        return download_cache;
    default:
        ASSERT_MSG(false, "Invalid memory usage={}", usage);
        return upload_cache;
    }
}

void StagingBufferPool::ReleaseCache(MemoryUsage usage) {
    ReleaseLevel(GetCache(usage), usage, current_delete_level);
}

void StagingBufferPool::ReleaseAllFree(MemoryUsage usage) {
    auto& cache = GetCache(usage);
    for (size_t log2 = cache.size(); log2-- > 0;) {
        auto& staging = cache[log2];
        auto& entries = staging.entries;
        const size_t old_size = entries.size();
        if (old_size == 0) {
            staging.delete_index = 0;
            staging.iterate_index = 0;
            continue;
        }

        const auto is_deletable = [this](const StagingBuffer& entry) {
            return !entry.deferred && scheduler.IsFree(entry.tick);
        };
        std::erase_if(entries, is_deletable);
        const u64 removed = static_cast<u64>(old_size - entries.size());
        if (removed != 0) {
            AccountRelease(usage, removed * (u64{1} << log2), removed, true, log2);
        }
        staging.delete_index = 0;
        if (staging.iterate_index > entries.size()) {
            staging.iterate_index = 0;
        }
    }
}

void StagingBufferPool::ReleaseLevel(StagingBuffersCache& cache, MemoryUsage usage, size_t log2) {
    constexpr size_t deletions_per_tick = 16;
    auto& staging = cache[log2];
    auto& entries = staging.entries;
    const size_t old_size = entries.size();
    if (old_size == 0) {
        staging.delete_index = 0;
        staging.iterate_index = 0;
        return;
    }

    const auto is_deletable = [this](const StagingBuffer& entry) {
        return !entry.deferred && scheduler.IsFree(entry.tick);
    };
    const size_t begin_offset = (std::min)(staging.delete_index, old_size);
    const size_t end_offset = (std::min)(begin_offset + deletions_per_tick, old_size);
    const auto begin = entries.begin() + begin_offset;
    const auto end = entries.begin() + end_offset;
    const auto new_end = std::remove_if(begin, end, is_deletable);
    const u64 removed = static_cast<u64>(std::distance(new_end, end));
    entries.erase(new_end, end);
    if (removed != 0) {
        AccountRelease(usage, removed * (u64{1} << log2), removed, false, log2);
    }

    const size_t new_size = entries.size();
    staging.delete_index += deletions_per_tick;
    if (staging.delete_index >= new_size) {
        staging.delete_index = 0;
    }
    if (staging.iterate_index > new_size) {
        staging.iterate_index = 0;
    }
}

void StagingBufferPool::AccountAllocation(MemoryUsage usage, u64 bytes, size_t log2) {
    switch (usage) {
    case MemoryUsage::DeviceLocal:
        cached_device_local_bytes += bytes;
        break;
    case MemoryUsage::Upload:
        cached_upload_bytes += bytes;
        break;
    case MemoryUsage::Download:
        cached_download_bytes += bytes;
        break;
    default:
        ASSERT_MSG(false, "Invalid staging memory usage={}", usage);
        return;
    }
    ++allocation_count;
    TrackPressureReallocation(usage, log2, bytes);
    peak_total_bytes =
        (std::max)(peak_total_bytes, static_cast<u64>(stream_buffer_size) + CachedBytes());
}

void StagingBufferPool::AccountRelease(MemoryUsage usage, u64 bytes, u64 count, bool pressure,
                                       size_t log2) {
    u64* cached_bytes{};
    switch (usage) {
    case MemoryUsage::DeviceLocal:
        cached_bytes = &cached_device_local_bytes;
        break;
    case MemoryUsage::Upload:
        cached_bytes = &cached_upload_bytes;
        break;
    case MemoryUsage::Download:
        cached_bytes = &cached_download_bytes;
        break;
    default:
        ASSERT_MSG(false, "Invalid staging memory usage={}", usage);
        return;
    }
    ASSERT(*cached_bytes >= bytes);
    *cached_bytes -= bytes;
    release_count += count;
    released_bytes += bytes;
    if (pressure) {
        pressure_release_count += count;
        pressure_released_bytes += bytes;
        TrackPressureRelease(usage, log2, count);
    }
}

StagingBufferPool::PressureReleaseBuckets& StagingBufferPool::PressureBuckets(MemoryUsage usage) {
    switch (usage) {
    case MemoryUsage::DeviceLocal:
        return device_local_pressure_releases;
    case MemoryUsage::Upload:
        return upload_pressure_releases;
    case MemoryUsage::Download:
        return download_pressure_releases;
    default:
        ASSERT_MSG(false, "Invalid staging memory usage={}", usage);
        return upload_pressure_releases;
    }
}

const StagingBufferPool::PressureReleaseBuckets& StagingBufferPool::PressureBuckets(
    MemoryUsage usage) const {
    switch (usage) {
    case MemoryUsage::DeviceLocal:
        return device_local_pressure_releases;
    case MemoryUsage::Upload:
        return upload_pressure_releases;
    case MemoryUsage::Download:
        return download_pressure_releases;
    default:
        ASSERT_MSG(false, "Invalid staging memory usage={}", usage);
        return upload_pressure_releases;
    }
}

void StagingBufferPool::TrackPressureRelease(MemoryUsage usage, size_t log2, u64 count) {
    if (!device.IsXclipse() || count == 0 || log2 >= NUM_LEVELS) {
        return;
    }
    auto& bucket = PressureBuckets(usage)[log2];
    if (bucket.pending_count == 0) {
        bucket.first_release = std::chrono::steady_clock::now();
    }
    bucket.pending_count += count;
}

void StagingBufferPool::TrackPressureReallocation(MemoryUsage usage, size_t log2, u64 bytes) {
    if (!device.IsXclipse() || log2 >= NUM_LEVELS) {
        return;
    }
    auto& bucket = PressureBuckets(usage)[log2];
    if (bucket.pending_count == 0) {
        return;
    }
    const auto elapsed = std::chrono::steady_clock::now() - bucket.first_release;
    const u64 elapsed_ns = static_cast<u64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    --bucket.pending_count;
    if (bucket.pending_count == 0) {
        bucket.first_release = {};
    }
    device.GetXclipseTelemetry().RecordStagingPressureReallocation(bytes, elapsed_ns);
}

u64 StagingBufferPool::CachedBytes() const noexcept {
    return cached_device_local_bytes + cached_upload_bytes + cached_download_bytes;
}

} // namespace Vulkan
