// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.utils

object NativePgo {
    init {
        System.loadLibrary("yuzu-android")
    }

    external fun buildInfo(): String

    external fun runStage(stage: Int, outputFile: String): String
}
