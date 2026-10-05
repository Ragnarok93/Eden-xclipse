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

void XclipseTelemetry::RecordDispatchDeferral() noexcept {
    if (Enabled()) {
        dispatch_deferrals.fetch_add(1, std::memory_order_relaxed);
    }
}

void XclipseTelemetry::RecordQueueSubmit(u64 commands, bool sync2, bool has_upload) noexcept {
    if (!Enabled()) {
        return;
    }
    queue_submits.fetch_add(1, std::memory_order_relaxed);
    commands_submitted.fetch_add(commands, std::memory_order_relaxed);
    (sync2 ? sync2_submits : legacy_submits).fetch_add(1, std::memory_order_relaxed);
    (has_upload ? upload_submits : non_upload_submits).fetch_add(1, std::memory_order_relaxed);
}

void XclipseTelemetry::RecordGpuWait(bool timeline, XclipseWaitSource source) noexcept {
    if (!Enabled()) {
        return;
    }
    host_waits.fetch_add(1, std::memory_order_relaxed);
    if (timeline) {
        timeline_waits.fetch_add(1, std::memory_order_relaxed);
    }
    switch (source) {
    case XclipseWaitSource::BufferCache:
        wait_buffer_cache.fetch_add(1, std::memory_order_relaxed);
        break;
    case XclipseWaitSource::Fence:
        wait_fence.fetch_add(1, std::memory_order_relaxed);
        break;
    case XclipseWaitSource::DescriptorBuffer:
        wait_descriptor_buffer.fetch_add(1, std::memory_order_relaxed);
        break;
    case XclipseWaitSource::StagingPressure:
        wait_staging_pressure.fetch_add(1, std::memory_order_relaxed);
        break;
    case XclipseWaitSource::Unknown:
    default:
        wait_unknown.fetch_add(1, std::memory_order_relaxed);
        break;
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

void XclipseTelemetry::RecordTransferConsumerBarrier() noexcept {
    if (Enabled()) {
        transfer_consumer_barriers.fetch_add(1, std::memory_order_relaxed);
    }
}

void XclipseTelemetry::RecordComputeConsumerBarrier() noexcept {
    if (Enabled()) {
        compute_consumer_barriers.fetch_add(1, std::memory_order_relaxed);
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

void XclipseTelemetry::RecordDescriptorSetUpdate() noexcept {
    if (Enabled()) {
        descriptor_set_updates.fetch_add(1, std::memory_order_relaxed);
    }
}

void XclipseTelemetry::RecordDescriptorPushUpdate() noexcept {
    if (Enabled()) {
        descriptor_push_updates.fetch_add(1, std::memory_order_relaxed);
    }
}

void XclipseTelemetry::RecordDescriptorBufferAllocation(u64 bytes) noexcept {
    if (!Enabled()) {
        return;
    }
    descriptor_buffer_allocations.fetch_add(1, std::memory_order_relaxed);
    descriptor_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void XclipseTelemetry::RecordDescriptorBufferUse(bool reused) noexcept {
    if (!Enabled()) {
        return;
    }
    descriptor_buffer_uses.fetch_add(1, std::memory_order_relaxed);
    if (reused) {
        descriptor_buffer_reuses.fetch_add(1, std::memory_order_relaxed);
    }
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

void XclipseTelemetry::RecordBptcGpuDecode(bool bc7, u64 bytes) noexcept {
    if (!Enabled()) {
        return;
    }
    (bc7 ? bptc_bc7_dispatches : bptc_bc6_dispatches).fetch_add(1, std::memory_order_relaxed);
    bptc_gpu_decode_bytes.fetch_add(bytes, std::memory_order_relaxed);
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

void XclipseTelemetry::RecordColorShaderBlit() noexcept {
    if (Enabled()) {
        color_shader_blits.fetch_add(1, std::memory_order_relaxed);
    }
}

void XclipseTelemetry::RecordDepthStencilBlit(bool native) noexcept {
    if (!Enabled()) {
        return;
    }
    (native ? depth_stencil_native_blits : depth_stencil_shader_blits)
        .fetch_add(1, std::memory_order_relaxed);
}

void XclipseTelemetry::RecordNativeResolve() noexcept {
    if (Enabled()) {
        native_resolves.fetch_add(1, std::memory_order_relaxed);
    }
}

void XclipseTelemetry::RecordRenderPassAttachment(bool undefined_initial_layout, bool dontcare_store) noexcept {
    if (!Enabled()) {
        return;
    }
    if (undefined_initial_layout) {
        renderpass_undefined_initial_layouts.fetch_add(1, std::memory_order_relaxed);
    }
    if (dontcare_store) {
        renderpass_dontcare_stores.fetch_add(1, std::memory_order_relaxed);
    }
}

void XclipseTelemetry::RecordImageCopy(bool native) noexcept {
    if (!Enabled()) {
        return;
    }
    (native ? native_image_copies : reinterpret_copies)
        .fetch_add(1, std::memory_order_relaxed);
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
        .upload_submits = upload_submits.load(std::memory_order_relaxed),
        .non_upload_submits = non_upload_submits.load(std::memory_order_relaxed),
        .dispatch_deferrals = dispatch_deferrals.load(std::memory_order_relaxed),
        .commands_submitted = commands_submitted.load(std::memory_order_relaxed),
        .sync2_submits = sync2_submits.load(std::memory_order_relaxed),
        .legacy_submits = legacy_submits.load(std::memory_order_relaxed),
        .host_waits = host_waits.load(std::memory_order_relaxed),
        .timeline_waits = timeline_waits.load(std::memory_order_relaxed),
        .scheduler_finishes = scheduler_finishes.load(std::memory_order_relaxed),
        .wait_unknown = wait_unknown.load(std::memory_order_relaxed),
        .wait_buffer_cache = wait_buffer_cache.load(std::memory_order_relaxed),
        .wait_fence = wait_fence.load(std::memory_order_relaxed),
        .wait_descriptor_buffer = wait_descriptor_buffer.load(std::memory_order_relaxed),
        .wait_staging_pressure = wait_staging_pressure.load(std::memory_order_relaxed),
        .all_commands_barriers = all_commands_barriers.load(std::memory_order_relaxed),
        .transfer_consumer_barriers =
            transfer_consumer_barriers.load(std::memory_order_relaxed),
        .compute_consumer_barriers =
            compute_consumer_barriers.load(std::memory_order_relaxed),
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
        .bcn_gpu_decode_dispatches =
            bcn_gpu_decode_dispatches.load(std::memory_order_relaxed),
        .bcn_gpu_decode_bytes = bcn_gpu_decode_bytes.load(std::memory_order_relaxed),
        .bcn_gpu_decode_fallbacks =
            bcn_gpu_decode_fallbacks.load(std::memory_order_relaxed),
        .bptc_bc6_dispatches = bptc_bc6_dispatches.load(std::memory_order_relaxed),
        .bptc_bc7_dispatches = bptc_bc7_dispatches.load(std::memory_order_relaxed),
        .bptc_gpu_decode_bytes = bptc_gpu_decode_bytes.load(std::memory_order_relaxed),
        .color_shader_blits = color_shader_blits.load(std::memory_order_relaxed),
        .depth_stencil_native_blits =
            depth_stencil_native_blits.load(std::memory_order_relaxed),
        .depth_stencil_shader_blits =
            depth_stencil_shader_blits.load(std::memory_order_relaxed),
        .native_resolves = native_resolves.load(std::memory_order_relaxed),
        .native_image_copies = native_image_copies.load(std::memory_order_relaxed),
        .reinterpret_copies = reinterpret_copies.load(std::memory_order_relaxed),
        .renderpass_undefined_initial_layouts =
            renderpass_undefined_initial_layouts.load(std::memory_order_relaxed),
        .renderpass_dontcare_stores = renderpass_dontcare_stores.load(std::memory_order_relaxed),
    };
}

} // namespace Vulkan
