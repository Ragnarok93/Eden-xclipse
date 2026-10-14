// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.benchmark

import android.os.SystemClock
import org.json.JSONObject
import org.yuzu.yuzu_emu.NativeLibrary

/**
 * Versioned, passive source of existing Vulkan aggregate diagnostics.
 * No log formatting and no GPU queue waits on an emulation hot path.
 */
object EdenLabRendererSnapshot {
    const val SCHEMA_VERSION = 1

    private val fields = listOf(
        "aggregate_telemetry_enabled",
        "frame_count",
        "pipeline_creates",
        "pipeline_failures",
        "runtime_pipeline_map_hits",
        "runtime_pipeline_map_misses",
        "pipeline_create_count",
        "pipeline_create_p95_upper_ns",
        "pipeline_block_count",
        "pipeline_block_p95_upper_ns",
        "pipeline_queue_residence_count",
        "pipeline_queue_residence_p95_upper_ns",
        "shader_decode_count",
        "shader_decode_p95_upper_ns",
        "spirv_generation_count",
        "spirv_generation_p95_upper_ns",
        "queue_submits",
        "host_waits",
        "timeline_waits",
        "bcn_gpu_decode_dispatches",
        "bcn_cpu_fallbacks",
        "descriptor_stalls",
        "image_native_copies",
        "dref_unresolved_bindings"
    )

    internal fun decode(values: LongArray, elapsedRealtimeNs: Long): JSONObject {
        if (values.isEmpty()) {
            return JSONObject()
                .put("available", false)
                .put("reason", "no_running_vulkan_renderer")
                .put("host_elapsed_realtime_ns", elapsedRealtimeNs)
        }
        require(values.size == fields.size + 1 && values[0] == SCHEMA_VERSION.toLong()) {
            "Unsupported renderer telemetry schema"
        }
        val result = JSONObject()
            .put("available", true)
            .put("schema_version", SCHEMA_VERSION)
            .put("host_elapsed_realtime_ns", elapsedRealtimeNs)
            .put("latencies_are_bucket_upper_bounds", true)
            .put("gpu_execution_timing_available", false)
        fields.forEachIndexed { index, name ->
            val value = values[index + 1]
            require(value >= 0) { "Invalid negative counter: $name" }
            result.put(name, if (index == 0) value == 1L else value)
        }
        return result
    }

    fun sample(): JSONObject {
        // Timestamp is a clock-domain label, not a Vulkan GPU-finish timestamp.
        val counters = NativeLibrary.getLabRendererCounters()
        return decode(counters, SystemClock.elapsedRealtimeNanos())
    }
}
