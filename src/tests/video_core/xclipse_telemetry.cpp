// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/vulkan_common/xclipse_telemetry.h"

TEST_CASE("XclipseTelemetry: disabled collector is inert", "[video_core]") {
    Vulkan::XclipseTelemetry telemetry;
    telemetry.RecordPipelineCacheLookup(true);
    telemetry.RecordPipelineCreate(true, 100, false);
    telemetry.RecordQueueSubmit(4, true);
    telemetry.RecordGpuWait(true);
    telemetry.RecordDescriptorBufferAllocation(64);

    const auto snapshot = telemetry.Snapshot();
    REQUIRE_FALSE(snapshot.enabled);
    REQUIRE(snapshot.pipeline_creates == 0);
    REQUIRE(snapshot.queue_submits == 0);
    REQUIRE(snapshot.descriptor_bytes == 0);
}

TEST_CASE("XclipseTelemetry: records pipeline sync and descriptor counters", "[video_core]") {
    Vulkan::XclipseTelemetry telemetry;
    telemetry.SetEnabled(true);

    telemetry.RecordPipelineCacheLookup(true);
    telemetry.RecordPipelineCacheLookup(false);
    telemetry.RecordPipelineCreate(true, 100, true);
    telemetry.RecordPipelineCreate(false, 250, false);
    telemetry.RecordPipelinePolicyViolations(3);
    telemetry.RecordQueueSubmit(7, true);
    telemetry.RecordQueueSubmit(3, false);
    telemetry.RecordGpuWait(true);
    telemetry.RecordGpuWait(false);
    telemetry.RecordSchedulerFinish();
    telemetry.RecordAllCommandsBarrier();
    telemetry.RecordTransferConsumerBarrier();
    telemetry.RecordComputeConsumerBarrier();
    telemetry.RecordDescriptorSetAllocation(2);
    telemetry.RecordDescriptorBufferAllocation(96);
    telemetry.RecordDescriptorBufferWrap(false);
    telemetry.RecordDescriptorBufferWrap(true);
    telemetry.RecordBcnGpuDecode(4096);
    telemetry.RecordBcnGpuDecodeFallback();

    const auto snapshot = telemetry.Snapshot();
    REQUIRE(snapshot.enabled);
    REQUIRE(snapshot.pipeline_creates == 2);
    REQUIRE(snapshot.graphics_pipeline_creates == 1);
    REQUIRE(snapshot.compute_pipeline_creates == 1);
    REQUIRE(snapshot.pipeline_cache_hits == 1);
    REQUIRE(snapshot.pipeline_cache_misses == 1);
    REQUIRE(snapshot.pipeline_failures == 1);
    REQUIRE(snapshot.pipeline_policy_violations == 3);
    REQUIRE(snapshot.pipeline_compile_ns_total == 350);
    REQUIRE(snapshot.pipeline_compile_ns_max == 250);
    REQUIRE(snapshot.queue_submits == 2);
    REQUIRE(snapshot.commands_submitted == 10);
    REQUIRE(snapshot.sync2_submits == 1);
    REQUIRE(snapshot.legacy_submits == 1);
    REQUIRE(snapshot.host_waits == 2);
    REQUIRE(snapshot.timeline_waits == 1);
    REQUIRE(snapshot.scheduler_finishes == 1);
    REQUIRE(snapshot.all_commands_barriers == 1);
    REQUIRE(snapshot.transfer_consumer_barriers == 1);
    REQUIRE(snapshot.compute_consumer_barriers == 1);
    REQUIRE(snapshot.descriptor_set_allocations == 2);
    REQUIRE(snapshot.descriptor_buffer_allocations == 1);
    REQUIRE(snapshot.descriptor_bytes == 96);
    REQUIRE(snapshot.descriptor_buffer_wraps == 2);
    REQUIRE(snapshot.descriptor_stalls == 1);
    REQUIRE(snapshot.bcn_gpu_decode_dispatches == 1);
    REQUIRE(snapshot.bcn_gpu_decode_bytes == 4096);
    REQUIRE(snapshot.bcn_gpu_decode_fallbacks == 1);
}
