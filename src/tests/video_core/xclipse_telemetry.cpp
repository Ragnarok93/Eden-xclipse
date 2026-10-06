// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/vulkan_common/xclipse_telemetry.h"

TEST_CASE("XclipseTelemetry: disabled collector is inert", "[video_core]") {
    Vulkan::XclipseTelemetry telemetry;
    telemetry.RecordRuntimePipelineMapLookup(true);
    telemetry.RecordPipelineCreate(true, 100, false);
    telemetry.RecordQueueSubmit(4, true);
    telemetry.RecordGpuWait(true, Vulkan::XclipseWaitSource::BufferCache);
    telemetry.RecordGpuWait(true, Vulkan::XclipseWaitSource::Fence);
    telemetry.RecordGpuWait(true, Vulkan::XclipseWaitSource::DescriptorBuffer);
    telemetry.RecordGpuWait(true);
    telemetry.RecordTransferConsumerBarrier();
    telemetry.RecordComputeConsumerBarrier();
    telemetry.RecordFrame();
    telemetry.RecordDescriptorSetUpdate();
    telemetry.RecordDescriptorPushUpdate();
    telemetry.RecordDescriptorBufferAllocation(64);
    telemetry.RecordDescriptorBufferUse(true);
    telemetry.RecordDescriptorFrameWaitRequest();
    telemetry.RecordColorShaderBlit();
    telemetry.RecordDepthStencilBlit(true);
    telemetry.RecordDepthStencilBlit(false);
    telemetry.RecordNativeResolve();
    telemetry.RecordImageCopy(true);
    telemetry.RecordImageCopy(false);

    const auto snapshot = telemetry.Snapshot();
    REQUIRE_FALSE(snapshot.enabled);
    REQUIRE(snapshot.pipeline_creates == 0);
    REQUIRE(snapshot.queue_submits == 0);
    REQUIRE(snapshot.host_waits == 0);
    REQUIRE(snapshot.wait_unknown == 0);
    REQUIRE(snapshot.wait_buffer_cache == 0);
    REQUIRE(snapshot.wait_fence == 0);
    REQUIRE(snapshot.wait_descriptor_buffer == 0);
    REQUIRE(snapshot.transfer_consumer_barriers == 0);
    REQUIRE(snapshot.compute_consumer_barriers == 0);
    REQUIRE(snapshot.frame_count == 0);
    REQUIRE(snapshot.descriptor_set_updates == 0);
    REQUIRE(snapshot.descriptor_push_updates == 0);
    REQUIRE(snapshot.descriptor_buffer_uses == 0);
    REQUIRE(snapshot.descriptor_frame_wait_requests == 0);
    REQUIRE(snapshot.descriptor_bytes == 0);
    REQUIRE(snapshot.color_shader_blits == 0);
    REQUIRE(snapshot.depth_stencil_native_blits == 0);
    REQUIRE(snapshot.depth_stencil_shader_blits == 0);
    REQUIRE(snapshot.native_resolves == 0);
    REQUIRE(snapshot.native_image_copies == 0);
    REQUIRE(snapshot.reinterpret_copies == 0);
}

TEST_CASE("XclipseTelemetry: records pipeline sync and descriptor counters", "[video_core]") {
    Vulkan::XclipseTelemetry telemetry;
    telemetry.SetEnabled(true);

    telemetry.RecordRuntimePipelineMapLookup(true);
    telemetry.RecordRuntimePipelineMapLookup(false);
    telemetry.RecordPipelineCreate(true, 100, true);
    telemetry.RecordPipelineCreate(false, 250, false);
    telemetry.RecordPipelineBuild(500);
    telemetry.RecordPipelineQueueResidence(750);
    telemetry.RecordPipelineBlockingWait(2'000'000);
    telemetry.RecordPipelinePolicyViolations(3);
    telemetry.RecordQueueSubmit(7, true);
    telemetry.RecordQueueSubmit(3, false);
    telemetry.RecordGpuWait(true, Vulkan::XclipseWaitSource::BufferCache, 1'000'000);
    telemetry.RecordGpuWait(false, Vulkan::XclipseWaitSource::Fence, 2'000'000);
    telemetry.RecordGpuWait(true, Vulkan::XclipseWaitSource::DescriptorBuffer, 4'000'000);
    telemetry.RecordGpuWait(true, Vulkan::XclipseWaitSource::Unknown, 8'000'000);
    telemetry.RecordSchedulerFinish();
    telemetry.RecordAllCommandsBarrier();
    telemetry.RecordTransferConsumerBarrier();
    telemetry.RecordComputeConsumerBarrier();
    telemetry.RecordFrame();
    telemetry.RecordFrame();
    telemetry.RecordDescriptorSetAllocation(2);
    telemetry.RecordDescriptorSetUpdate();
    telemetry.RecordDescriptorPushUpdate();
    telemetry.RecordDescriptorBufferAllocation(96);
    telemetry.RecordDescriptorBufferUse(false);
    telemetry.RecordDescriptorBufferUse(true);
    telemetry.RecordDescriptorBufferWrap(false);
    telemetry.RecordDescriptorBufferWrap(true);
    telemetry.RecordDescriptorFrameWaitRequest();
    telemetry.RecordBcnNativePath(Vulkan::XclipseBcnFormat::BC3);
    telemetry.RecordBcnGpuDecode(Vulkan::XclipseBcnFormat::BC5, 4096);
    telemetry.RecordBptcGpuDecode(Vulkan::XclipseBcnFormat::BC7, 8192);
    telemetry.RecordBcnCpuFallback(Vulkan::XclipseBcnFormat::BC7,
                                   Vulkan::XclipseBcnFallbackReason::RuntimeDisabled);
    telemetry.RecordColorShaderBlit();
    telemetry.RecordDepthStencilBlit(true);
    telemetry.RecordDepthStencilBlit(false);
    telemetry.RecordNativeResolve();
    telemetry.RecordImageCopy(true);
    telemetry.RecordImageCopy(false);

    const auto snapshot = telemetry.Snapshot();
    REQUIRE(snapshot.enabled);
    REQUIRE(snapshot.pipeline_creates == 2);
    REQUIRE(snapshot.graphics_pipeline_creates == 1);
    REQUIRE(snapshot.compute_pipeline_creates == 1);
    REQUIRE(snapshot.runtime_pipeline_map_hits == 1);
    REQUIRE(snapshot.runtime_pipeline_map_misses == 1);
    REQUIRE(snapshot.pipeline_failures == 1);
    REQUIRE(snapshot.pipeline_policy_violations == 3);
    REQUIRE(snapshot.pipeline_build_latency.count == 1);
    REQUIRE(snapshot.pipeline_queue_residence_latency.count == 1);
    REQUIRE(snapshot.pipeline_blocking_latency.count == 1);
    REQUIRE(snapshot.pipeline_blocking_latency.total_ns == 2'000'000);
    REQUIRE(snapshot.vulkan_pipeline_create_latency.count == 2);
    REQUIRE(snapshot.vulkan_pipeline_create_latency.total_ns == 350);
    REQUIRE(snapshot.vulkan_pipeline_create_latency.max_ns == 250);
    REQUIRE(snapshot.queue_submits == 2);
    REQUIRE(snapshot.commands_submitted == 10);
    REQUIRE(snapshot.sync2_submits == 1);
    REQUIRE(snapshot.legacy_submits == 1);
    REQUIRE(snapshot.host_waits == 4);
    REQUIRE(snapshot.timeline_waits == 3);
    REQUIRE(snapshot.wait_unknown == 1);
    REQUIRE(snapshot.wait_buffer_cache == 1);
    REQUIRE(snapshot.wait_fence == 1);
    REQUIRE(snapshot.wait_descriptor_buffer == 1);
    REQUIRE(snapshot.wait_latency[static_cast<std::size_t>(
                Vulkan::XclipseWaitSource::BufferCache)].total_ns == 1'000'000);
    REQUIRE(snapshot.wait_latency[static_cast<std::size_t>(
                Vulkan::XclipseWaitSource::DescriptorBuffer)].total_ns == 4'000'000);
    REQUIRE(snapshot.scheduler_finishes == 1);
    REQUIRE(snapshot.all_commands_barriers == 1);
    REQUIRE(snapshot.transfer_consumer_barriers == 1);
    REQUIRE(snapshot.compute_consumer_barriers == 1);
    REQUIRE(snapshot.frame_count == 2);
    REQUIRE(snapshot.descriptor_set_allocations == 2);
    REQUIRE(snapshot.descriptor_set_updates == 1);
    REQUIRE(snapshot.descriptor_push_updates == 1);
    REQUIRE(snapshot.descriptor_buffer_allocations == 1);
    REQUIRE(snapshot.descriptor_buffer_uses == 2);
    REQUIRE(snapshot.descriptor_buffer_reuses == 1);
    REQUIRE(snapshot.descriptor_bytes == 96);
    REQUIRE(snapshot.descriptor_buffer_wraps == 2);
    REQUIRE(snapshot.descriptor_stalls == 1);
    REQUIRE(snapshot.descriptor_frame_wait_requests == 1);
    REQUIRE(snapshot.bcn_gpu_decode_dispatches == 2);
    REQUIRE(snapshot.bcn_gpu_decode_bytes == 12288);
    REQUIRE(snapshot.bcn_gpu_decode_fallbacks == 1);
    REQUIRE(snapshot.bcn_native_images[static_cast<std::size_t>(
                Vulkan::XclipseBcnFormat::BC3)] == 1);
    REQUIRE(snapshot.bcn_gpu_decode_dispatches_by_format[static_cast<std::size_t>(
                Vulkan::XclipseBcnFormat::BC5)] == 1);
    REQUIRE(snapshot.bcn_gpu_decode_dispatches_by_format[static_cast<std::size_t>(
                Vulkan::XclipseBcnFormat::BC7)] == 1);
    REQUIRE(snapshot.bcn_cpu_fallbacks[static_cast<std::size_t>(
                Vulkan::XclipseBcnFormat::BC7)] == 1);
    REQUIRE(snapshot.bcn_fallback_reasons[static_cast<std::size_t>(
                Vulkan::XclipseBcnFormat::BC7)][static_cast<std::size_t>(
                Vulkan::XclipseBcnFallbackReason::RuntimeDisabled)] == 1);
    REQUIRE(snapshot.bptc_bc7_dispatches == 1);
    REQUIRE(snapshot.bptc_gpu_decode_bytes == 8192);
    REQUIRE(snapshot.color_shader_blits == 1);
    REQUIRE(snapshot.depth_stencil_native_blits == 1);
    REQUIRE(snapshot.depth_stencil_shader_blits == 1);
    REQUIRE(snapshot.native_resolves == 1);
    REQUIRE(snapshot.native_image_copies == 1);
    REQUIRE(snapshot.reinterpret_copies == 1);
}


TEST_CASE("XclipseTelemetry: latency histograms expose bounded percentiles", "[video_core]") {
    Vulkan::XclipseTelemetry telemetry;
    telemetry.SetEnabled(true);
    for (const u64 duration : {1'000'000ULL, 2'000'000ULL, 4'000'000ULL, 8'000'000ULL,
                               16'000'000ULL, 32'000'000ULL, 64'000'000ULL, 128'000'000ULL}) {
        telemetry.RecordPipelineCreate(true, duration, true);
    }
    const auto latency = telemetry.Snapshot().vulkan_pipeline_create_latency;
    REQUIRE(latency.count == 8);
    REQUIRE(latency.PercentileUpperBoundNs(50) >= 8'000'000ULL);
    REQUIRE(latency.PercentileUpperBoundNs(90) >= 64'000'000ULL);
    REQUIRE(latency.PercentileUpperBoundNs(99) >= 128'000'000ULL);
    REQUIRE(latency.max_ns == 128'000'000ULL);
}
