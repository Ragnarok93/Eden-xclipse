// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.utils

import android.app.Activity
import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.widget.Toast
import androidx.core.content.FileProvider
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

object PgoProfileExporter {
    private val exporting = AtomicBoolean(false)

    private fun latest(context: Context): File? = File(context.filesDir, "pgo").listFiles()
        ?.filter { it.isDirectory && File(it, "manifest.json").isFile }
        ?.maxByOrNull { File(it, "manifest.json").lastModified() }

    fun hasResults(context: Context): Boolean = latest(context) != null

    fun export(activity: Activity, session: File? = null) {
        if (!exporting.compareAndSet(false, true)) return
        Thread({
            try {
                val source = session ?: latest(activity) ?: error("No profiling results yet")
                val archive = File(activity.cacheDir, "eden-pgo-${source.name}.zip")
                ZipOutputStream(archive.outputStream()).use { zip ->
                    source.listFiles()?.filter { it.extension == "profraw" || it.name == "manifest.json" }
                        ?.sortedBy { it.name }?.forEach { file ->
                            zip.putNextEntry(ZipEntry(file.name))
                            file.inputStream().use { it.copyTo(zip) }
                            zip.closeEntry()
                        }
                }
                val uri = FileProvider.getUriForFile(activity, "${activity.packageName}.provider", archive)
                activity.runOnUiThread {
                    try {
                        if (!activity.isFinishing) {
                            activity.startActivity(Intent.createChooser(Intent(Intent.ACTION_SEND).apply {
                                type = "application/zip"
                                putExtra(Intent.EXTRA_STREAM, uri)
                                clipData = ClipData.newUri(activity.contentResolver, "Eden PGO profile", uri)
                                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                            }, "Export Eden PGO profile"))
                        }
                    } finally {
                        exporting.set(false)
                    }
                }
            } catch (error: Exception) {
                exporting.set(false)
                activity.runOnUiThread {
                    Toast.makeText(activity, "Export failed: ${error.message}", Toast.LENGTH_LONG).show()
                }
            }
        }, "eden-pgo-export").start()
    }
}
