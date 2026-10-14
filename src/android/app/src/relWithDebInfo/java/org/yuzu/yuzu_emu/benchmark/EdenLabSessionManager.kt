// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.benchmark

import android.content.Context
import android.content.pm.ApplicationInfo
import android.os.Build
import android.util.AtomicFile
import java.io.File
import java.io.RandomAccessFile
import java.security.MessageDigest
import org.json.JSONArray
import org.json.JSONObject
import org.yuzu.yuzu_emu.NativeLibrary

/**
 * Phase 1 instrumentation owner. No game launch or production configuration
 * mutation is supported. The only writable target is files/eden-lab/sandbox.json.
 */
object EdenLabSessionManager {
    private const val PROTOCOL = 1
    private val runIdPattern = Regex("[a-f0-9]{32}")

    private fun sha256(bytes: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(bytes)
            .joinToString("") { "%02x".format(it) }

    private fun writeExact(file: File, content: ByteArray) {
        val atomic = AtomicFile(file)
        val stream = atomic.startWrite()
        try {
            stream.write(content)
            atomic.finishWrite(stream)
        } catch (failure: Throwable) {
            atomic.failWrite(stream)
            throw failure
        }
    }

    private fun validate(request: JSONObject): String {
        require(request.getInt("protocol_version") == PROTOCOL)
        val id = request.getString("run_id")
        require(runIdPattern.matches(id))
        val operation = request.getString("operation")
        require(operation in setOf("probe", "sandbox-roundtrip", "recover"))
        val expected = mutableSetOf("protocol_version", "run_id", "operation")
        if (operation == "sandbox-roundtrip") {
            expected.add("config")
            val config = request.getJSONObject("config")
            require(config.length() == 2)
            require(config.has("measurement_tag") && config.has("aggregate_telemetry"))
            val tag = config.get("measurement_tag")
            val aggregate = config.get("aggregate_telemetry")
            require(tag is String && tag.length <= 64 && tag.all { it.code in 32..126 })
            require(aggregate is Boolean)
        }
        require(request.keys().asSequence().toSet() == expected)
        return operation
    }

    fun execute(context: Context, request: JSONObject): JSONObject {
        check(context.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0) {
            "Debug build required"
        }
        // Refuse a second guest lifecycle even if the host's idle check raced.
        check(!NativeLibrary.isRunning()) { "Emulation session is active" }
        val operation = validate(request)
        val runId = request.getString("run_id")
        val root = File(context.filesDir, "eden-lab").apply {
            check(isDirectory || mkdirs()) { "Cannot create lab directory" }
        }
        RandomAccessFile(File(root, "session.lock"), "rw").use { lockFile ->
            val lock = lockFile.channel.tryLock() ?: error("Lab session already active")
            lock.use {
                val pending = File(root, "pending.json")
                check(!pending.exists() || operation == "recover") {
                    "Pending recovery; no further operations allowed"
                }
                val result = when (operation) {
                    "probe" -> JSONObject()
                        .put("package", context.packageName)
                        .put("model", Build.MODEL)
                        .put("android", Build.VERSION.RELEASE)
                        .put("recovery_pending", pending.exists())
                        .put("read_only", true)
                        .put("session_frame_counts", JSONArray(NativeLibrary.getLabFrameCounts().toList()))
                        .put("renderer_telemetry", EdenLabRendererSnapshot.sample())
                    "sandbox-roundtrip" -> sandboxRoundtrip(root, request.getJSONObject("config"))
                    "recover" -> recover(root)
                    else -> error("Unsupported operation")
                }
                return result.put("protocol_version", PROTOCOL)
                    .put("run_id", runId)
                    .put("status", "complete")
            }
        }
    }

    private fun sandboxRoundtrip(root: File, config: JSONObject): JSONObject {
        val target = File(root, "sandbox.json")
        val snapshot = File(root, "original.bin")
        val pending = File(root, "pending.json")
        check(!pending.exists())
        val existed = target.isFile
        val original = if (existed) target.readBytes() else ByteArray(0)
        require(original.size <= 65_536) { "Original sandbox exceeds size limit" }
        val previousHash = sha256(original)
        // Durable snapshot must precede the journal, which precedes mutation.
        writeExact(snapshot, original)
        check(sha256(snapshot.readBytes()) == previousHash)
        writeExact(pending, JSONObject()
            .put("original_exists", existed)
            .put("original_sha256", previousHash)
            .toString().toByteArray(Charsets.UTF_8))
        try {
            writeExact(target, config.toString().toByteArray(Charsets.UTF_8))
            val readback = JSONObject(target.readText(Charsets.UTF_8))
            check(readback.getString("measurement_tag") ==
                  config.getString("measurement_tag"))
            check(readback.getBoolean("aggregate_telemetry") ==
                  config.getBoolean("aggregate_telemetry"))
            check(readback.length() == 2) { "Persisted sandbox mismatch" }
            val restored = restore(root)
            check(restored) { "Restoration did not complete" }
            return JSONObject()
                .put("sandbox_only", true)
                .put("persisted_readback_verified", true)
                .put("restored_exact_bytes", true)
                .put("original_sha256", previousHash)
        } catch (failure: Throwable) {
            // Leave the pending journal if restore itself fails.
            try {
                restore(root)
            } catch (_: Throwable) {
                // Recovery remains mandatory and fail-closed on a later request.
            }
            throw failure
        }
    }

    private fun restore(root: File): Boolean {
        val pending = File(root, "pending.json")
        check(pending.isFile) { "No recovery journal" }
        val info = JSONObject(pending.readText(Charsets.UTF_8))
        val expected = info.getString("original_sha256")
        require(Regex("[a-f0-9]{64}").matches(expected))
        val existed = info.getBoolean("original_exists")
        val snapshot = File(root, "original.bin")
        check(snapshot.isFile) { "Missing recovery snapshot" }
        val bytes = snapshot.readBytes()
        check(bytes.size <= 65_536 && sha256(bytes) == expected) {
            "Recovery snapshot failed hash verification"
        }
        val target = File(root, "sandbox.json")
        if (existed) {
            writeExact(target, bytes)
        } else {
            check(!target.exists() || target.delete()) { "Cannot remove lab sandbox" }
        }
        check((target.isFile == existed) &&
              (!existed || sha256(target.readBytes()) == expected)) {
            "Sandbox restoration verification failed"
        }
        check(pending.delete()) { "Cannot clear recovery journal" }
        // Snapshot remains harmless if cleanup is interrupted after committing
        // recovery; next transaction overwrites it while no journal exists.
        snapshot.delete()
        return true
    }

    private fun recover(root: File): JSONObject {
        val pending = File(root, "pending.json")
        if (!pending.exists()) return JSONObject().put("recovered", false)
        return JSONObject().put("recovered", restore(root))
    }
}
