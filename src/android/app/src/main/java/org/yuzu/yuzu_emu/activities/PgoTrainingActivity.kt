// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import android.app.Activity
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.Process
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import org.json.JSONArray
import org.json.JSONObject
import org.yuzu.yuzu_emu.utils.PgoProfileExporter
import org.yuzu.yuzu_emu.utils.NativePgo
import java.io.File
import java.security.MessageDigest
import java.util.UUID

/** Isolated worker process: cancelling or timing out cannot kill emulation. */
class PgoTrainingActivity : Activity() {
    private val handler = Handler(Looper.getMainLooper())
    private var running = false
    private lateinit var status: TextView
    private lateinit var export: Button
    private lateinit var session: File

    private val timeout = Runnable {
        if (running) {
            status.text = "Profiling timed out. Completed stages are retained; this session is incomplete."
            stopWorker()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            val pad = (24 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad, pad, pad)
        }
        status = TextView(this).apply {
            textSize = 18f
            text = "Preparing automated native PGO training…"
        }
        export = Button(this).apply {
            text = "Export profiling results"
            isEnabled = false
            setOnClickListener { PgoProfileExporter.export(this@PgoTrainingActivity, session) }
        }
        layout.addView(status)
        layout.addView(export)
        layout.addView(Button(this).apply {
            text = "Close / cancel"
            setOnClickListener { stopWorker() }
        })
        setContentView(layout)
        session = File(filesDir, "pgo/${UUID.randomUUID()}")
        check(session.mkdirs())
        running = true
        Thread({ runSuite() }, "eden-pgo-training").start()
    }

    private fun runSuite() {
        val report = JSONObject().apply {
            put("complete", false)
            put("abi", "arm64-v8a")
            put("device", Build.MODEL)
            put("soc", if (Build.VERSION.SDK_INT >= 31) Build.SOC_MODEL else "unknown")
            put("android", Build.VERSION.RELEASE)
            put("coverage", "texture swizzle, CPU BCn fallback, compute IR/SPIR-V, headless Vulkan pipeline creation")
            put("not_covered", "guest CPU/NCE, guest shader frontend, texture cache, render submission, presentation, lifecycle")
        }
        val results = JSONArray()
        report.put("stages", results)
        try {
            val info = JSONObject(NativePgo.buildInfo())
            report.put("build", info)
            check(info.optBoolean("instrumented")) { "Install the PGO training APK first." }
            saveReport(report)
            val names = arrayOf("Texture round trips", "BCn decode", "Shader compilation", "Vulkan pipelines")
            for (stage in names.indices) {
                handler.post {
                    status.text = "${stage + 1}/${names.size}: ${names[stage]}\nNo games or input are required."
                    handler.removeCallbacks(timeout)
                    handler.postDelayed(timeout, 180_000)
                }
                val raw = File(session, "stage-$stage.profraw")
                val result = JSONObject(NativePgo.runStage(stage, raw.absolutePath))
                results.put(result)
                if (result.optBoolean("success")) {
                    result.put("file", raw.name)
                    result.put("sha256", sha256(raw))
                    result.put("bytes", raw.length())
                }
                saveReport(report)
                check(result.optBoolean("success")) { result.optString("error", "Stage failed") }
            }
            report.put("complete", true)
            saveReport(report)
            handler.post { status.text = "Profiling complete. Export the results to build the PGO APK." }
        } catch (error: Exception) {
            report.put("error", error.message)
            runCatching { saveReport(report) }
            handler.post { status.text = "Profiling incomplete: ${error.message}" }
        } finally {
            handler.post {
                running = false
                handler.removeCallbacks(timeout)
                window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                export.isEnabled = File(session, "manifest.json").exists()
            }
        }
    }

    private fun saveReport(report: JSONObject) {
        val temporary = File(session, "manifest.tmp")
        temporary.writeText(report.toString(2))
        check(temporary.renameTo(File(session, "manifest.json")))
    }

    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input ->
            val buffer = ByteArray(64 * 1024)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        return digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
    }

    private fun stopWorker() {
        handler.removeCallbacks(timeout)
        finishAndRemoveTask()
        Process.killProcess(Process.myPid())
    }

    @Deprecated("Activity back callback")
    override fun onBackPressed() = stopWorker()

    override fun onDestroy() {
        handler.removeCallbacks(timeout)
        super.onDestroy()
        if (running && isFinishing) Process.killProcess(Process.myPid())
    }
}
