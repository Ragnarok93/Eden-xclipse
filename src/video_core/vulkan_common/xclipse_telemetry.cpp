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

void XclipseTelemetry::RecordPipelinePolicyViolations(u64 count) noexcept {
    if (Enabled() && count != 0) {
        pipeline_policy_violations.fetch_add(count, std::memory_order_relaxed);
    }
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

void XclipseTelemetry::RecordPreciseUploadBarrier() noexcept {
    if (Enabled()) {
        precise_upload_barriers.fetch_add(1, std::memory_order_relaxed);
    }
}

void XclipseTelemetry::RecordDescriptorSetAllocation(u64 sets) noexcept {
    if (Enabled()) {
        descriptor_set_allocations.fetch_add(sets, std::memory_order_relaxed);
    }
}

void XclipseTelemetry::RecordDescriptorBufferAllocation(u64 bytes) noexcept {
    if (!Enabled()) {
        return;
    }
    descriptor_buffer_allocations.fetch_add(1, std::memory_order_relaxed);
    descriptor_bytes.fetch_add(bytes, std::memory_order_relaxed);
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

void XclipseTelemetry::RecordBcnGpuDecode(u64 bytes) noexcept {
    if (!Enabled()) {
        return;
    }
    bcn_gpu_decode_dispatches.fetch_add(1, std::memory_order_relaxed);
    bcn_gpu_decode_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void XclipseTelemetry::RecordBcnGpuDecodeFallback() noexcept {
    if (Enabled()) {
        bcn_gpu_decode_fallbacks.fetch_add(1, std::memory_order_relaxed);
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
        .pipeline_policy_violations =
            pipeline_policy_violations.load(std::memory_order_relaxed),
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
        .precise_upload_barriers = precise_upload_barriers.load(std::memory_order_relaxed),
        .descriptor_set_allocations =
            descriptor_set_allocations.load(std::memory_order_relaxed),
        .descriptor_buffer_allocations =
            descriptor_buffer_allocations.load(std::memory_order_relaxed),
        .descriptor_bytes = descriptor_bytes.load(std::memory_order_relaxed),
        .descriptor_buffer_wraps = descriptor_buffer_wraps.load(std::memory_order_relaxed),
        .descriptor_stalls = descriptor_stalls.load(std::memory_order_relaxed),
        .bcn_gpu_decode_dispatches =
            bcn_gpu_decode_dispatches.load(std::memory_order_relaxed),
        .bcn_gpu_decode_bytes = bcn_gpu_decode_bytes.load(std::memory_order_relaxed),
        .bcn_gpu_decode_fallbacks =
            bcn_gpu_decode_fallbacks.load(std::memory_order_relaxed),
    };
}

} // namespace Vulkan
