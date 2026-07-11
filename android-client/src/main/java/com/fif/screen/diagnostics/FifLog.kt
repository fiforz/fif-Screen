package com.fif.screen.diagnostics

import android.util.Log

object FifLog {
    fun device(vararg pairs: Pair<String, Any?>) = write("FIFSCREEN_DEVICE", pairs)
    fun display(vararg pairs: Pair<String, Any?>) = write("FIFSCREEN_DISPLAY", pairs)
    fun decoder(vararg pairs: Pair<String, Any?>) = write("FIFSCREEN_DECODER", pairs)
    fun input(vararg pairs: Pair<String, Any?>) = write("FIFSCREEN_INPUT", pairs)
    fun network(vararg pairs: Pair<String, Any?>) = write("FIFSCREEN_NETWORK", pairs)
    fun surface(vararg pairs: Pair<String, Any?>) = write("FIFSCREEN_SURFACE", pairs)

    private fun write(tag: String, pairs: Array<out Pair<String, Any?>>) {
        Log.i(tag, pairs.joinToString(" ") { "${it.first}=${sanitize(it.second)}" })
    }

    private fun sanitize(value: Any?): String =
        value?.toString()
            ?.replace('\n', '_')
            ?.replace('\r', '_')
            ?.replace('\t', '_')
            ?.replace(' ', '_')
            ?: "null"
}
