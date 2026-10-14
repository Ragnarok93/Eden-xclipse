// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.benchmark

import android.os.Bundle
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/** Not an exported Activity or production intent. Launched using adb instrumentation. */
class EdenLabBridgeTest {
    @Test
    fun rendererSnapshotReportsNoRendererWithoutInventingCounters() {
        val decoded = EdenLabRendererSnapshot.decode(longArrayOf(), 123L)
        assertFalse(decoded.getBoolean("available"))
        assertEquals("no_running_vulkan_renderer", decoded.getString("reason"))
    }

    @Test
    fun rendererSnapshotDecodesVersionAndLatencyProvenance() {
        val values = LongArray(25)
        values[0] = 1
        values[1] = 1
        values[2] = 90
        values[7] = 9
        values[8] = 1_024_000
        val decoded = EdenLabRendererSnapshot.decode(values, 456L)
        assertTrue(decoded.getBoolean("available"))
        assertEquals(90L, decoded.getLong("frame_count"))
        assertEquals(9L, decoded.getLong("pipeline_create_count"))
        assertEquals(1_024_000L, decoded.getLong("pipeline_create_p95_upper_ns"))
        assertFalse(decoded.getBoolean("gpu_execution_timing_available"))
    }

    @Test
    fun executeAuthorizedRequest() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val relativePath = InstrumentationRegistry.getArguments().getString("lab_request")
            ?: error("Missing lab request")
        require(Regex("cache/eden-lab-[a-f0-9]{32}[.]json").matches(relativePath))
        val target = instrumentation.targetContext
        val file = File(target.dataDir, relativePath)
        require(file.canonicalFile.parentFile == target.cacheDir.canonicalFile)
        require(file.isFile && file.length() in 1L..65_536L)
        val request = JSONObject(file.readText(Charsets.UTF_8))
        val result = try {
            EdenLabSessionManager.execute(target, request)
        } catch (failure: Exception) {
            JSONObject()
                .put("protocol_version", 1)
                .put("run_id", request.optString("run_id"))
                .put("status", "failed")
                .put("error", failure.message ?: failure.javaClass.simpleName)
        }
        instrumentation.sendStatus(0, Bundle().apply {
            putString("stream", "LAB_RESULT=$result\n")
        })
        assertEquals("complete", result.getString("status"))
    }
}
