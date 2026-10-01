// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>

#include "common/common_types.h"

namespace Vulkan {

struct XclipseTelemetrySnapshot {
    bool enabled{};

    u64 pipeline_creates{};
    u64 graphics_pipeline_creates{};
    u64 compute_pipeline_creates{};
    u64 pipeline_cache_hits{};
    u64 pipeline_cache_misses{};
    u64 pipeline_failures{};
    u64 pipeline_compile_ns_total{};
    u64 pipeline_compile_ns_max{};

    u64 queue_submits{};
    u64 commands_submitted{};
    u64 sync2_submits{};
    u64 legacy_submits{};
    u64 host_waits{};
    u64 timeline_waits{};
    u64 scheduler_finishes{};
    u64 all_commands_barriers{};

    u64 descriptor_set_allocations{};
    u64 descriptor_buffer_allocations{};
    u64 descriptor_bytes{};
    u64 descriptor_buffer_wraps{};
    u64 descriptor_stalls{};
};

class XclipseTelemetry {
public:
    void SetEnabled(bool enabled_) noexcept {
        enabled.store(enabled_, std::memory_order_relaxed);
    }

    [[nodiscard]] bool Enabled() const noexcept {
        return enabled.load(std::memory_order_relaxed);
    }

    void RecordPipelineCacheLookup(bool hit) noexcept;
    void RecordPipelineCreate(bool graphics, u64 compile_ns, bool success) noexcept;
    void RecordQueueSubmit(u64 commands, bool sync2) noexcept;
    void RecordGpuWait(bool timeline) noexcept;
    void RecordSchedulerFinish() noexcept;
    void RecordAllCommandsBarrier() noexcept;
    void RecordDescriptorSetAllocation(u64 sets = 1) noexcept;
    void RecordDescriptorBufferAllocation(u64 bytes) noexcept;
    void RecordDescriptorBufferWrap(bool stalled) noexcept;

    [[nodiscard]] XclipseTelemetrySnapshot Snapshot() const noexcept;

private:
    static void UpdateMax(std::atomic<u64>& target, u64 value) noexcept;

    std::atomic_bool enabled{false};

    std::atomic<u64> pipeline_creates{};
    std::atomic<u64> graphics_pipeline_creates{};
    std::atomic<u64> compute_pipeline_creates{};
    std::atomic<u64> pipeline_cache_hits{};
    std::atomic<u64> pipeline_cache_misses{};
    std::atomic<u64> pipeline_failures{};
    std::atomic<u64> pipeline_compile_ns_total{};
    std::atomic<u64> pipeline_compile_ns_max{};

    std::atomic<u64> queue_submits{};
    std::atomic<u64> commands_submitted{};
    std::atomic<u64> sync2_submits{};
    std::atomic<u64> legacy_submits{};
    std::atomic<u64> host_waits{};
    std::atomic<u64> timeline_waits{};
    std::atomic<u64> scheduler_finishes{};
    std::atomic<u64> all_commands_barriers{};

    std::atomic<u64> descriptor_set_allocations{};
    std::atomic<u64> descriptor_buffer_allocations{};
    std::atomic<u64> descriptor_bytes{};
    std::atomic<u64> descriptor_buffer_wraps{};
    std::atomic<u64> descriptor_stalls{};
};

} // namespace Vulkan
