// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstddef>

#include "common/common_types.h"

namespace Vulkan {

enum class XclipseWaitSource : u8 {
    Unknown,
    BufferCache,
    Fence,
    DescriptorBuffer,
    StagingPressure,
    SchedulerFinish,
    ResourceHazard,
    UploadCompletion,
    DownloadReadback,
    QueueSynchronization,
    FramePresentation,
    Teardown,
    Other,
    Count,
};

[[nodiscard]] constexpr const char* XclipseWaitSourceName(XclipseWaitSource source) noexcept {
    switch (source) {
    case XclipseWaitSource::Unknown:
        return "unknown";
    case XclipseWaitSource::BufferCache:
        return "buffer-cache";
    case XclipseWaitSource::Fence:
        return "fence";
    case XclipseWaitSource::DescriptorBuffer:
        return "descriptor-buffer";
    case XclipseWaitSource::StagingPressure:
        return "staging-pressure";
    case XclipseWaitSource::SchedulerFinish:
        return "scheduler-finish";
    case XclipseWaitSource::ResourceHazard:
        return "resource-hazard";
    case XclipseWaitSource::UploadCompletion:
        return "upload-completion";
    case XclipseWaitSource::DownloadReadback:
        return "download-readback";
    case XclipseWaitSource::QueueSynchronization:
        return "queue-synchronization";
    case XclipseWaitSource::FramePresentation:
        return "frame-presentation";
    case XclipseWaitSource::Teardown:
        return "teardown";
    case XclipseWaitSource::Other:
        return "other";
    case XclipseWaitSource::Count:
        break;
    }
    return "invalid";
}

enum class XclipseBcnFormat : u8 {
    BC1,
    BC2,
    BC3,
    BC4,
    BC5,
    BC6H,
    BC7,
    Count,
};

[[nodiscard]] constexpr const char* XclipseBcnFormatName(XclipseBcnFormat format) noexcept {
    switch (format) {
    case XclipseBcnFormat::BC1:
        return "BC1";
    case XclipseBcnFormat::BC2:
        return "BC2";
    case XclipseBcnFormat::BC3:
        return "BC3";
    case XclipseBcnFormat::BC4:
        return "BC4";
    case XclipseBcnFormat::BC5:
        return "BC5";
    case XclipseBcnFormat::BC6H:
        return "BC6H";
    case XclipseBcnFormat::BC7:
        return "BC7";
    case XclipseBcnFormat::Count:
        break;
    }
    return "invalid";
}

enum class XclipseBcnFallbackReason : u8 {
    RuntimeDisabled,
    ValidationUnavailable,
    DecoderUnavailable,
    UnsupportedImageShape,
    StorageFormatUnsupported,
    FormatSpecificRestriction,
    BrokenCompute,
    UnsupportedGpuPath,
    AllocationFailure,
    SynchronizationConstraint,
    Other,
    Count,
};

[[nodiscard]] constexpr const char* XclipseBcnFallbackReasonName(
    XclipseBcnFallbackReason reason) noexcept {
    switch (reason) {
    case XclipseBcnFallbackReason::RuntimeDisabled:
        return "runtime-disabled";
    case XclipseBcnFallbackReason::ValidationUnavailable:
        return "validation-unavailable";
    case XclipseBcnFallbackReason::DecoderUnavailable:
        return "decoder-unavailable";
    case XclipseBcnFallbackReason::UnsupportedImageShape:
        return "image-shape";
    case XclipseBcnFallbackReason::StorageFormatUnsupported:
        return "storage-format";
    case XclipseBcnFallbackReason::FormatSpecificRestriction:
        return "format-specific";
    case XclipseBcnFallbackReason::BrokenCompute:
        return "broken-compute";
    case XclipseBcnFallbackReason::UnsupportedGpuPath:
        return "unsupported-gpu-path";
    case XclipseBcnFallbackReason::AllocationFailure:
        return "allocation";
    case XclipseBcnFallbackReason::SynchronizationConstraint:
        return "synchronization";
    case XclipseBcnFallbackReason::Other:
        return "other";
    case XclipseBcnFallbackReason::Count:
        break;
    }
    return "invalid";
}

inline constexpr std::size_t XCLIPSE_LATENCY_BUCKET_COUNT = 48;
inline constexpr std::size_t XCLIPSE_WAIT_SOURCE_COUNT =
    static_cast<std::size_t>(XclipseWaitSource::Count);
inline constexpr std::size_t XCLIPSE_BCN_FORMAT_COUNT =
    static_cast<std::size_t>(XclipseBcnFormat::Count);
inline constexpr std::size_t XCLIPSE_BCN_FALLBACK_REASON_COUNT =
    static_cast<std::size_t>(XclipseBcnFallbackReason::Count);

struct XclipseLatencySnapshot {
    u64 count{};
    u64 total_ns{};
    u64 max_ns{};
    std::array<u64, XCLIPSE_LATENCY_BUCKET_COUNT> buckets{};

    [[nodiscard]] u64 PercentileUpperBoundNs(u32 percentile) const noexcept;
};

class XclipseLatencyAccumulator {
public:
    void Record(u64 duration_ns) noexcept;
    [[nodiscard]] XclipseLatencySnapshot Snapshot() const noexcept;

private:
    std::atomic<u64> count{};
    std::atomic<u64> total_ns{};
    std::atomic<u64> max_ns{};
    std::array<std::atomic<u64>, XCLIPSE_LATENCY_BUCKET_COUNT> buckets{};
};

struct XclipseTelemetrySnapshot {
    bool enabled{};

    u64 pipeline_creates{};
    u64 graphics_pipeline_creates{};
    u64 compute_pipeline_creates{};
    u64 runtime_pipeline_map_hits{};
    u64 runtime_pipeline_map_misses{};
    u64 pipeline_failures{};
    u64 pipeline_policy_violations{};
    XclipseLatencySnapshot vulkan_pipeline_create_latency{};
    XclipseLatencySnapshot pipeline_build_latency{};
    XclipseLatencySnapshot pipeline_queue_residence_latency{};
    XclipseLatencySnapshot pipeline_blocking_latency{};
    u64 staging_pressure_reallocations{};
    u64 staging_pressure_reallocated_bytes{};
    XclipseLatencySnapshot staging_pressure_reallocation_latency{};

    u64 queue_submits{};
    u64 upload_submits{};
    u64 non_upload_submits{};
    u64 dispatch_deferrals{};
    u64 commands_submitted{};
    u64 sync2_submits{};
    u64 legacy_submits{};
    u64 host_waits{};
    u64 timeline_waits{};
    u64 scheduler_finishes{};
    u64 wait_unknown{};
    u64 wait_buffer_cache{};
    u64 wait_fence{};
    u64 wait_descriptor_buffer{};
    u64 wait_staging_pressure{};
    std::array<XclipseLatencySnapshot, XCLIPSE_WAIT_SOURCE_COUNT> wait_latency{};
    u64 all_commands_barriers{};
    u64 transfer_consumer_barriers{};
    u64 compute_consumer_barriers{};

    u64 frame_count{};
    u64 descriptor_set_allocations{};
    u64 descriptor_set_updates{};
    u64 descriptor_push_updates{};
    u64 descriptor_buffer_allocations{};
    u64 descriptor_buffer_uses{};
    u64 descriptor_buffer_reuses{};
    u64 descriptor_bytes{};
    u64 descriptor_buffer_wraps{};
    u64 descriptor_stalls{};
    u64 descriptor_frame_wait_requests{};

    u64 bcn_gpu_decode_dispatches{};
    u64 bcn_gpu_decode_bytes{};
    u64 bcn_gpu_decode_fallbacks{};
    std::array<u64, XCLIPSE_BCN_FORMAT_COUNT> bcn_native_images{};
    std::array<u64, XCLIPSE_BCN_FORMAT_COUNT> bcn_gpu_decode_dispatches_by_format{};
    std::array<u64, XCLIPSE_BCN_FORMAT_COUNT> bcn_cpu_fallbacks{};
    std::array<std::array<u64, XCLIPSE_BCN_FALLBACK_REASON_COUNT>,
               XCLIPSE_BCN_FORMAT_COUNT>
        bcn_fallback_reasons{};
    u64 bptc_bc6_dispatches{};
    u64 bptc_bc7_dispatches{};
    u64 bptc_gpu_decode_bytes{};

    u64 color_shader_blits{};
    u64 depth_stencil_native_blits{};
    u64 depth_stencil_shader_blits{};
    u64 native_resolves{};
    u64 native_image_copies{};
    u64 reinterpret_copies{};
    u64 renderpass_undefined_initial_layouts{};
    u64 renderpass_dontcare_stores{};
};

class XclipseTelemetry {
public:
    void SetEnabled(bool enabled_) noexcept {
        enabled.store(enabled_, std::memory_order_relaxed);
    }

    [[nodiscard]] bool Enabled() const noexcept {
        return enabled.load(std::memory_order_relaxed);
    }

    void RecordRuntimePipelineMapLookup(bool hit) noexcept;
    void RecordPipelineCreate(bool graphics, u64 create_ns, bool success) noexcept;
    void RecordPipelineBuild(u64 build_ns) noexcept;
    void RecordPipelineQueueResidence(u64 residence_ns) noexcept;
    void RecordPipelineBlockingWait(u64 wait_ns) noexcept;
    void RecordStagingPressureReallocation(u64 bytes, u64 elapsed_ns) noexcept;
    void RecordPipelinePolicyViolations(u64 count) noexcept;
    void RecordQueueSubmit(u64 commands, bool sync2, bool has_upload = false) noexcept;
    void RecordDispatchDeferral() noexcept;
    void RecordGpuWait(bool timeline, XclipseWaitSource source = XclipseWaitSource::Unknown,
                       u64 duration_ns = 0) noexcept;
    void RecordSchedulerFinish() noexcept;
    void RecordAllCommandsBarrier() noexcept;
    void RecordTransferConsumerBarrier() noexcept;
    void RecordComputeConsumerBarrier() noexcept;
    void RecordFrame() noexcept;
    void RecordDescriptorSetAllocation(u64 sets = 1) noexcept;
    void RecordDescriptorSetUpdate() noexcept;
    void RecordDescriptorPushUpdate() noexcept;
    void RecordDescriptorBufferAllocation(u64 bytes) noexcept;
    void RecordDescriptorBufferUse(bool reused) noexcept;
    void RecordDescriptorBufferWrap(bool stalled) noexcept;
    void RecordDescriptorFrameWaitRequest() noexcept;
    void RecordBcnNativePath(XclipseBcnFormat format) noexcept;
    void RecordBcnGpuDecode(XclipseBcnFormat format, u64 bytes) noexcept;
    void RecordBptcGpuDecode(XclipseBcnFormat format, u64 bytes) noexcept;
    void RecordBcnCpuFallback(XclipseBcnFormat format,
                              XclipseBcnFallbackReason reason) noexcept;
    void RecordColorShaderBlit() noexcept;
    void RecordDepthStencilBlit(bool native) noexcept;
    void RecordNativeResolve() noexcept;
    void RecordImageCopy(bool native) noexcept;
    void RecordRenderPassAttachment(bool undefined_initial_layout, bool dontcare_store) noexcept;

    [[nodiscard]] XclipseTelemetrySnapshot Snapshot() const noexcept;

private:
    static void UpdateMax(std::atomic<u64>& target, u64 value) noexcept;

    std::atomic_bool enabled{false};

    std::atomic<u64> pipeline_creates{};
    std::atomic<u64> graphics_pipeline_creates{};
    std::atomic<u64> compute_pipeline_creates{};
    std::atomic<u64> runtime_pipeline_map_hits{};
    std::atomic<u64> runtime_pipeline_map_misses{};
    std::atomic<u64> pipeline_failures{};
    std::atomic<u64> pipeline_policy_violations{};
    XclipseLatencyAccumulator vulkan_pipeline_create_latency{};
    XclipseLatencyAccumulator pipeline_build_latency{};
    XclipseLatencyAccumulator pipeline_queue_residence_latency{};
    XclipseLatencyAccumulator pipeline_blocking_latency{};
    std::atomic<u64> staging_pressure_reallocations{};
    std::atomic<u64> staging_pressure_reallocated_bytes{};
    XclipseLatencyAccumulator staging_pressure_reallocation_latency{};

    std::atomic<u64> queue_submits{};
    std::atomic<u64> upload_submits{};
    std::atomic<u64> non_upload_submits{};
    std::atomic<u64> dispatch_deferrals{};
    std::atomic<u64> commands_submitted{};
    std::atomic<u64> sync2_submits{};
    std::atomic<u64> legacy_submits{};
    std::atomic<u64> host_waits{};
    std::atomic<u64> timeline_waits{};
    std::atomic<u64> scheduler_finishes{};
    std::atomic<u64> wait_unknown{};
    std::atomic<u64> wait_buffer_cache{};
    std::atomic<u64> wait_fence{};
    std::atomic<u64> wait_descriptor_buffer{};
    std::atomic<u64> wait_staging_pressure{};
    std::array<XclipseLatencyAccumulator, XCLIPSE_WAIT_SOURCE_COUNT> wait_latency{};
    std::atomic<u64> all_commands_barriers{};
    std::atomic<u64> transfer_consumer_barriers{};
    std::atomic<u64> compute_consumer_barriers{};

    std::atomic<u64> frame_count{};
    std::atomic<u64> descriptor_set_allocations{};
    std::atomic<u64> descriptor_set_updates{};
    std::atomic<u64> descriptor_push_updates{};
    std::atomic<u64> descriptor_buffer_allocations{};
    std::atomic<u64> descriptor_buffer_uses{};
    std::atomic<u64> descriptor_buffer_reuses{};
    std::atomic<u64> descriptor_bytes{};
    std::atomic<u64> descriptor_buffer_wraps{};
    std::atomic<u64> descriptor_stalls{};
    std::atomic<u64> descriptor_frame_wait_requests{};

    std::atomic<u64> bcn_gpu_decode_dispatches{};
    std::atomic<u64> bcn_gpu_decode_bytes{};
    std::atomic<u64> bcn_gpu_decode_fallbacks{};
    std::array<std::atomic<u64>, XCLIPSE_BCN_FORMAT_COUNT> bcn_native_images{};
    std::array<std::atomic<u64>, XCLIPSE_BCN_FORMAT_COUNT>
        bcn_gpu_decode_dispatches_by_format{};
    std::array<std::atomic<u64>, XCLIPSE_BCN_FORMAT_COUNT> bcn_cpu_fallbacks{};
    std::array<std::array<std::atomic<u64>, XCLIPSE_BCN_FALLBACK_REASON_COUNT>,
               XCLIPSE_BCN_FORMAT_COUNT>
        bcn_fallback_reasons{};
    std::atomic<u64> bptc_bc6_dispatches{};
    std::atomic<u64> bptc_bc7_dispatches{};
    std::atomic<u64> bptc_gpu_decode_bytes{};

    std::atomic<u64> color_shader_blits{};
    std::atomic<u64> depth_stencil_native_blits{};
    std::atomic<u64> depth_stencil_shader_blits{};
    std::atomic<u64> native_resolves{};
    std::atomic<u64> native_image_copies{};
    std::atomic<u64> reinterpret_copies{};
    std::atomic<u64> renderpass_undefined_initial_layouts{};
    std::atomic<u64> renderpass_dontcare_stores{};
};

} // namespace Vulkan
