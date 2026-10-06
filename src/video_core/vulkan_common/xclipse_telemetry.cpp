// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/vulkan_common/xclipse_telemetry.h"

#include <algorithm>
#include <bit>
#include <limits>

namespace Vulkan {
namespace {
std::size_t LatencyBucket(u64 duration_ns) noexcept {
    if (duration_ns == 0) {
        return 0;
    }
    return (std::min)(static_cast<std::size_t>(std::bit_width(duration_ns)),
                      XCLIPSE_LATENCY_BUCKET_COUNT - 1);
}

u64 BucketUpperBoundNs(std::size_t bucket) noexcept {
    if (bucket == 0) {
        return 0;
    }
    if (bucket >= 63) {
        return (std::numeric_limits<u64>::max)();
    }
    return (u64{1} << bucket) - 1;
}
} // namespace

u64 XclipseLatencySnapshot::PercentileUpperBoundNs(u32 percentile) const noexcept {
    if (count == 0 || percentile == 0) {
        return 0;
    }
    percentile = (std::min)(percentile, 100U);
    const u64 target = (count * percentile + 99) / 100;
    u64 cumulative = 0;
    for (std::size_t index = 0; index < buckets.size(); ++index) {
        cumulative += buckets[index];
        if (cumulative >= target) {
            return BucketUpperBoundNs(index);
        }
    }
    return max_ns;
}

void XclipseLatencyAccumulator::Record(u64 duration_ns) noexcept {
    count.fetch_add(1, std::memory_order_relaxed);
    total_ns.fetch_add(duration_ns, std::memory_order_relaxed);
    u64 current = max_ns.load(std::memory_order_relaxed);
    while (current < duration_ns &&
           !max_ns.compare_exchange_weak(current, duration_ns, std::memory_order_relaxed,
                                         std::memory_order_relaxed)) {
    }
    buckets[LatencyBucket(duration_ns)].fetch_add(1, std::memory_order_relaxed);
}

XclipseLatencySnapshot XclipseLatencyAccumulator::Snapshot() const noexcept {
    XclipseLatencySnapshot snapshot{
        .count = count.load(std::memory_order_relaxed),
        .total_ns = total_ns.load(std::memory_order_relaxed),
        .max_ns = max_ns.load(std::memory_order_relaxed),
    };
    for (std::size_t index = 0; index < buckets.size(); ++index) {
        snapshot.buckets[index] = buckets[index].load(std::memory_order_relaxed);
    }
    return snapshot;
}

void XclipseTelemetry::UpdateMax(std::atomic<u64>& target, u64 value) noexcept {
    u64 current = target.load(std::memory_order_relaxed);
    while (current < value &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed,
                                         std::memory_order_relaxed)) {
    }
}

void XclipseTelemetry::RecordRuntimePipelineMapLookup(bool hit) noexcept {
    if (!Enabled()) {
        return;
    }
    (hit ? runtime_pipeline_map_hits : runtime_pipeline_map_misses)
        .fetch_add(1, std::memory_order_relaxed);
}

void XclipseTelemetry::RecordPipelineCreate(bool graphics, u64 create_ns, bool success) noexcept {
    if (!Enabled()) {
        return;
    }
    pipeline_creates.fetch_add(1, std::memory_order_relaxed);
    (graphics ? graphics_pipeline_creates : compute_pipeline_creates)
        .fetch_add(1, std::memory_order_relaxed);
    if (!success) {
        pipeline_failures.fetch_add(1, std::memory_order_relaxed);
    }
    vulkan_pipeline_create_latency.Record(create_ns);
}

void XclipseTelemetry::RecordPipelineBuild(u64 build_ns) noexcept {
    if (Enabled()) {
        pipeline_build_latency.Record(build_ns);
    }
}

void XclipseTelemetry::RecordPipelineQueueResidence(u64 residence_ns) noexcept {
    if (Enabled()) {
        pipeline_queue_residence_latency.Record(residence_ns);
    }
}

void XclipseTelemetry::RecordPipelineBlockingWait(u64 wait_ns) noexcept {
    if (Enabled()) {
        pipeline_blocking_latency.Record(wait_ns);
    }
}

void XclipseTelemetry::RecordStagingPressureReallocation(u64 bytes, u64 elapsed_ns) noexcept {
    if (!Enabled()) {
        return;
    }
    staging_pressure_reallocations.fetch_add(1, std::memory_order_relaxed);
    staging_pressure_reallocated_bytes.fetch_add(bytes, std::memory_order_relaxed);
    staging_pressure_reallocation_latency.Record(elapsed_ns);
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

void XclipseTelemetry::RecordGpuWait(bool timeline, XclipseWaitSource source,
                                     u64 duration_ns) noexcept {
    if (!Enabled()) {
        return;
    }
    host_waits.fetch_add(1, std::memory_order_relaxed);
    if (timeline) {
        timeline_waits.fetch_add(1, std::memory_order_relaxed);
    }
    const auto source_index = static_cast<std::size_t>(source);
    const auto safe_index = source_index < XCLIPSE_WAIT_SOURCE_COUNT
                                ? source_index
                                : static_cast<std::size_t>(XclipseWaitSource::Unknown);
    wait_latency[safe_index].Record(duration_ns);
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
    case XclipseWaitSource::SchedulerFinish:
    case XclipseWaitSource::ResourceHazard:
    case XclipseWaitSource::UploadCompletion:
    case XclipseWaitSource::DownloadReadback:
    case XclipseWaitSource::QueueSynchronization:
    case XclipseWaitSource::FramePresentation:
    case XclipseWaitSource::Teardown:
    case XclipseWaitSource::Other:
    case XclipseWaitSource::Count:
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

void XclipseTelemetry::RecordBcnNativePath(XclipseBcnFormat format) noexcept {
    if (!Enabled()) {
        return;
    }
    const auto index = static_cast<std::size_t>(format);
    if (index >= XCLIPSE_BCN_FORMAT_COUNT) {
        return;
    }
    bcn_native_images[index].fetch_add(1, std::memory_order_relaxed);
}

void XclipseTelemetry::RecordBptcGpuDecode(XclipseBcnFormat format, u64 bytes) noexcept {
    if (!Enabled()) {
        return;
    }
    const auto index = static_cast<std::size_t>(format);
    if (index >= XCLIPSE_BCN_FORMAT_COUNT ||
        (format != XclipseBcnFormat::BC6H && format != XclipseBcnFormat::BC7)) {
        return;
    }
    (format == XclipseBcnFormat::BC7 ? bptc_bc7_dispatches : bptc_bc6_dispatches)
        .fetch_add(1, std::memory_order_relaxed);
    bcn_gpu_decode_dispatches.fetch_add(1, std::memory_order_relaxed);
    bcn_gpu_decode_dispatches_by_format[index].fetch_add(1, std::memory_order_relaxed);
    bcn_gpu_decode_bytes.fetch_add(bytes, std::memory_order_relaxed);
    bptc_gpu_decode_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void XclipseTelemetry::RecordBcnGpuDecode(XclipseBcnFormat format, u64 bytes) noexcept {
    if (!Enabled()) {
        return;
    }
    const auto index = static_cast<std::size_t>(format);
    if (index >= XCLIPSE_BCN_FORMAT_COUNT) {
        return;
    }
    bcn_gpu_decode_dispatches.fetch_add(1, std::memory_order_relaxed);
    bcn_gpu_decode_dispatches_by_format[index].fetch_add(1, std::memory_order_relaxed);
    bcn_gpu_decode_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void XclipseTelemetry::RecordBcnCpuFallback(XclipseBcnFormat format,
                                            XclipseBcnFallbackReason reason) noexcept {
    if (!Enabled()) {
        return;
    }
    const auto format_index = static_cast<std::size_t>(format);
    const auto reason_index = static_cast<std::size_t>(reason);
    if (format_index >= XCLIPSE_BCN_FORMAT_COUNT ||
        reason_index >= XCLIPSE_BCN_FALLBACK_REASON_COUNT) {
        return;
    }
    bcn_gpu_decode_fallbacks.fetch_add(1, std::memory_order_relaxed);
    bcn_cpu_fallbacks[format_index].fetch_add(1, std::memory_order_relaxed);
    bcn_fallback_reasons[format_index][reason_index].fetch_add(1,
                                                               std::memory_order_relaxed);
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
        .runtime_pipeline_map_hits = runtime_pipeline_map_hits.load(std::memory_order_relaxed),
        .runtime_pipeline_map_misses = runtime_pipeline_map_misses.load(std::memory_order_relaxed),
        .pipeline_failures = pipeline_failures.load(std::memory_order_relaxed),
        .pipeline_policy_violations =
            pipeline_policy_violations.load(std::memory_order_relaxed),
        .vulkan_pipeline_create_latency = vulkan_pipeline_create_latency.Snapshot(),
        .pipeline_build_latency = pipeline_build_latency.Snapshot(),
        .pipeline_queue_residence_latency = pipeline_queue_residence_latency.Snapshot(),
        .pipeline_blocking_latency = pipeline_blocking_latency.Snapshot(),
        .staging_pressure_reallocations =
            staging_pressure_reallocations.load(std::memory_order_relaxed),
        .staging_pressure_reallocated_bytes =
            staging_pressure_reallocated_bytes.load(std::memory_order_relaxed),
        .staging_pressure_reallocation_latency =
            staging_pressure_reallocation_latency.Snapshot(),
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
        .wait_latency = [&] {
            std::array<XclipseLatencySnapshot, XCLIPSE_WAIT_SOURCE_COUNT> result{};
            for (std::size_t index = 0; index < result.size(); ++index) {
                result[index] = wait_latency[index].Snapshot();
            }
            return result;
        }(),
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
        .bcn_native_images = [&] {
            std::array<u64, XCLIPSE_BCN_FORMAT_COUNT> result{};
            for (std::size_t index = 0; index < result.size(); ++index) {
                result[index] = bcn_native_images[index].load(std::memory_order_relaxed);
            }
            return result;
        }(),
        .bcn_gpu_decode_dispatches_by_format = [&] {
            std::array<u64, XCLIPSE_BCN_FORMAT_COUNT> result{};
            for (std::size_t index = 0; index < result.size(); ++index) {
                result[index] =
                    bcn_gpu_decode_dispatches_by_format[index].load(std::memory_order_relaxed);
            }
            return result;
        }(),
        .bcn_cpu_fallbacks = [&] {
            std::array<u64, XCLIPSE_BCN_FORMAT_COUNT> result{};
            for (std::size_t index = 0; index < result.size(); ++index) {
                result[index] = bcn_cpu_fallbacks[index].load(std::memory_order_relaxed);
            }
            return result;
        }(),
        .bcn_fallback_reasons = [&] {
            std::array<std::array<u64, XCLIPSE_BCN_FALLBACK_REASON_COUNT>,
                       XCLIPSE_BCN_FORMAT_COUNT>
                result{};
            for (std::size_t format = 0; format < result.size(); ++format) {
                for (std::size_t reason = 0; reason < result[format].size(); ++reason) {
                    result[format][reason] =
                        bcn_fallback_reasons[format][reason].load(std::memory_order_relaxed);
                }
            }
            return result;
        }(),
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
