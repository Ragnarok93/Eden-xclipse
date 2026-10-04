// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu.disk_shader_cache

import androidx.annotation.Keep
import androidx.lifecycle.ViewModelProvider
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.activities.EmulationActivity
import org.yuzu.yuzu_emu.model.EmulationViewModel
import org.yuzu.yuzu_emu.utils.Log

@Keep
object DiskShaderCacheProgress {
    private var emulationViewModel: EmulationViewModel? = null

    private fun viewModelFor(activity: EmulationActivity): EmulationViewModel {
        return emulationViewModel
            ?: ViewModelProvider(activity)[EmulationViewModel::class.java].also {
                emulationViewModel = it
            }
    }

    @JvmStatic
    fun loadProgress(stage: Int, progress: Int, max: Int) {
        val callbackStage = LoadCallbackStage.entries.getOrNull(stage)
        if (callbackStage == null) {
            Log.warning("[DiskShaderCacheProgress] Ignoring unknown stage=$stage")
            return
        }

        val activity = NativeLibrary.sEmulationActivity.get()
        if (activity == null) {
            // Cache compilation may legitimately begin before the Activity is registered.
            // Dropping UI-only progress is safer than flooding the persistent log or retaining
            // a stale Activity while native shader workers continue.
            return
        }

        activity.runOnUiThread {
            if (NativeLibrary.sEmulationActivity.get() !== activity ||
                activity.isFinishing || activity.isDestroyed) {
                return@runOnUiThread
            }

            when (callbackStage) {
                LoadCallbackStage.Prepare -> {
                    viewModelFor(activity)
                }

                LoadCallbackStage.Build -> {
                    viewModelFor(activity).updateProgress(
                        activity.getString(R.string.building_shaders),
                        progress,
                        max
                    )
                }

                LoadCallbackStage.Complete -> {
                    emulationViewModel = null
                }
            }
        }
    }

    // Equivalent to VideoCore::LoadCallbackStage
    enum class LoadCallbackStage {
        Prepare, Build, Complete
    }
}
