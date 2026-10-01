// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/vulkan_common/xclipse_telemetry.h"

namespace Vulkan {

void XclipseTelemetry::UpdateMax(std::atomic<u64>& target, u64 value) noexcept {
    u64 current = target.load(std::memory_order_relaxed);
    while (current < value &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed,
                                         std::memory_order_relaxed)) {
    }
}

void XclipseTelemetry::RecordPipelineCacheLookup(bool hit) noexcept {
    if (!Enabled()) {
        return;
    }
    (hit ? pipeline_cache_hits : pipeline_cache_misses).fetch_add(1, std::memory_order_relaxed);
}

void XclipseTelemetry::RecordPipelineCreate(bool graphics, u64 compile_ns, bool success) noexcept {
    if (!Enabled()) {
        return;
    }
    pipeline_creates.fetch_add(1, std::memory_order_relaxed);
    (graphics ? graphics_pipeline_creates : compute_pipeline_creates)
        .fetch_add(1, std::memory_order_relaxed);
    if (!success) {
        pipeline_failures.fetch_add(1, std::memory_order_relaxed);
    }
    pipeline_compile_ns_total.fetch_add(compile_ns, std::memory_order_relaxed);
    UpdateMax(pipeline_compile_ns_max, compile_ns);
}

void XclipseTelemetry::RecordQueueSubmit(u64 commands, bool sync2) noexcept {
    if (!Enabled()) {
        return;
    }
    queue_submits.fetch_add(1, std::memory_order_relaxed);
    commands_submitted.fetch_add(commands, std::memory_order_relaxed);
    (sync2 ? sync2_submits : legacy_submits).fetch_add(1, std::memory_order_relaxed);
}

void XclipseTelemetry::RecordGpuWait(bool timeline) noexcept {
    if (!Enabled()) {
        return;
    }
    host_waits.fetch_add(1, std::memory_order_relaxed);
    if (timeline) {
        timeline_waits.fetch_add(1, std::memory_order_relaxed);
    }
}

void XclipseTelemetry::RecordSchedulerFinish() noexcept {
    if (Enabled()) {
        scheduler_finishes.fetch_add(1, std::memory_order_relaxed);
    }
}

void XclipseTelemetry::RecordAllCommandsBarrier() noexcept {
    if (Enabled()) {
        all_commands_barriers.fetch_add(1, std::memory_order_relaxed);
    }
}

void XclipseTelemetry::RecordFrame() noexcept {
    if (Enabled()) {
        frame_count.fetch_add(1, std::memory_order_relaxed);
    }
}

void XclipseTelemetry::RecordDescriptorSetAllocation(u64 sets) noexcept {
    if (Enabled()) {
        descriptor_set_allocations.fetch_add(sets, std::memory_order_relaxed);
    }
}

void XclipseTelemetry::RecordDescriptorSetUpdate(u64 cpu_ns) noexcept {
    if (!Enabled()) {
        return;
    }
    descriptor_set_updates.fetch_add(1, std::memory_order_relaxed);
    descriptor_cpu_ns_total.fetch_add(cpu_ns, std::memory_order_relaxed);
    UpdateMax(descriptor_cpu_ns_max, cpu_ns);
}

void XclipseTelemetry::RecordDescriptorPushUpdate(u64 cpu_ns) noexcept {
    if (!Enabled()) {
        return;
    }
    descriptor_push_updates.fetch_add(1, std::memory_order_relaxed);
    descriptor_cpu_ns_total.fetch_add(cpu_ns, std::memory_order_relaxed);
    UpdateMax(descriptor_cpu_ns_max, cpu_ns);
}

void XclipseTelemetry::RecordDescriptorBufferAllocation(u64 bytes) noexcept {
    if (!Enabled()) {
        return;
    }
    descriptor_buffer_allocations.fetch_add(1, std::memory_order_relaxed);
    descriptor_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void XclipseTelemetry::RecordDescriptorBufferUse(bool reused, u64 cpu_ns) noexcept {
    if (!Enabled()) {
        return;
    }
    descriptor_buffer_uses.fetch_add(1, std::memory_order_relaxed);
    if (reused) {
        descriptor_buffer_reuses.fetch_add(1, std::memory_order_relaxed);
    }
    descriptor_cpu_ns_total.fetch_add(cpu_ns, std::memory_order_relaxed);
    UpdateMax(descriptor_cpu_ns_max, cpu_ns);
}

void XclipseTelemetry::RecordDescriptorBufferWrap(bool stalled) noexcept {
    if (!Enabled()) {
        return;
    }
    descriptor_buffer_wraps.fetch_add(1, std::memory_order_relaxed);
    if (stalled) {
        descriptor_stalls.fetch_add(1, std::memory_order_relaxed);
    }
}

void XclipseTelemetry::RecordDescriptorFrameWaitRequest() noexcept {
    if (Enabled()) {
        descriptor_frame_wait_requests.fetch_add(1, std::memory_order_relaxed);
    }
}

XclipseTelemetrySnapshot XclipseTelemetry::Snapshot() const noexcept {
    return {
        .enabled = Enabled(),
        .pipeline_creates = pipeline_creates.load(std::memory_order_relaxed),
        .graphics_pipeline_creates =
            graphics_pipeline_creates.load(std::memory_order_relaxed),
        .compute_pipeline_creates =
            compute_pipeline_creates.load(std::memory_order_relaxed),
        .pipeline_cache_hits = pipeline_cache_hits.load(std::memory_order_relaxed),
        .pipeline_cache_misses = pipeline_cache_misses.load(std::memory_order_relaxed),
        .pipeline_failures = pipeline_failures.load(std::memory_order_relaxed),
        .pipeline_compile_ns_total =
            pipeline_compile_ns_total.load(std::memory_order_relaxed),
        .pipeline_compile_ns_max = pipeline_compile_ns_max.load(std::memory_order_relaxed),
        .queue_submits = queue_submits.load(std::memory_order_relaxed),
        .commands_submitted = commands_submitted.load(std::memory_order_relaxed),
        .sync2_submits = sync2_submits.load(std::memory_order_relaxed),
        .legacy_submits = legacy_submits.load(std::memory_order_relaxed),
        .host_waits = host_waits.load(std::memory_order_relaxed),
        .timeline_waits = timeline_waits.load(std::memory_order_relaxed),
        .scheduler_finishes = scheduler_finishes.load(std::memory_order_relaxed),
        .all_commands_barriers = all_commands_barriers.load(std::memory_order_relaxed),
        .frame_count = frame_count.load(std::memory_order_relaxed),
        .descriptor_set_allocations =
            descriptor_set_allocations.load(std::memory_order_relaxed),
        .descriptor_set_updates = descriptor_set_updates.load(std::memory_order_relaxed),
        .descriptor_push_updates = descriptor_push_updates.load(std::memory_order_relaxed),
        .descriptor_buffer_allocations =
            descriptor_buffer_allocations.load(std::memory_order_relaxed),
        .descriptor_buffer_uses = descriptor_buffer_uses.load(std::memory_order_relaxed),
        .descriptor_buffer_reuses = descriptor_buffer_reuses.load(std::memory_order_relaxed),
        .descriptor_bytes = descriptor_bytes.load(std::memory_order_relaxed),
        .descriptor_buffer_wraps = descriptor_buffer_wraps.load(std::memory_order_relaxed),
        .descriptor_stalls = descriptor_stalls.load(std::memory_order_relaxed),
        .descriptor_frame_wait_requests =
            descriptor_frame_wait_requests.load(std::memory_order_relaxed),
        .descriptor_cpu_ns_total =
            descriptor_cpu_ns_total.load(std::memory_order_relaxed),
        .descriptor_cpu_ns_max = descriptor_cpu_ns_max.load(std::memory_order_relaxed),
    };
}

} // namespace Vulkan
