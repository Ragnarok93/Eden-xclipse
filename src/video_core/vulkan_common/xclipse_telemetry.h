// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>

#include "common/common_types.h"

namespace Vulkan {

enum class XclipseWaitSource : u8 {
    Unknown,
    BufferCache,
    Fence,
    DescriptorBuffer,
    StagingPressure,
};

struct XclipseTelemetrySnapshot {
    bool enabled{};

    u64 pipeline_creates{};
    u64 graphics_pipeline_creates{};
    u64 compute_pipeline_creates{};
    u64 pipeline_cache_hits{};
    u64 pipeline_cache_misses{};
    u64 pipeline_failures{};
    u64 pipeline_policy_violations{};
    u64 pipeline_compile_ns_total{};
    u64 pipeline_compile_ns_max{};

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

    void RecordPipelineCacheLookup(bool hit) noexcept;
    void RecordPipelineCreate(bool graphics, u64 compile_ns, bool success) noexcept;
    void RecordPipelinePolicyViolations(u64 count) noexcept;
    void RecordQueueSubmit(u64 commands, bool sync2, bool has_upload = false) noexcept;
    void RecordDispatchDeferral() noexcept;
    void RecordGpuWait(bool timeline,
                       XclipseWaitSource source = XclipseWaitSource::Unknown) noexcept;
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
    void RecordBcnGpuDecode(u64 bytes) noexcept;
    void RecordBptcGpuDecode(bool bc7, u64 bytes) noexcept;
    void RecordBcnGpuDecodeFallback() noexcept;
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
    std::atomic<u64> pipeline_cache_hits{};
    std::atomic<u64> pipeline_cache_misses{};
    std::atomic<u64> pipeline_failures{};
    std::atomic<u64> pipeline_policy_violations{};
    std::atomic<u64> pipeline_compile_ns_total{};
    std::atomic<u64> pipeline_compile_ns_max{};

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
