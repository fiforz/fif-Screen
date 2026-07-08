package com.fif.screen.video

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat
import android.os.Build
import android.view.Surface
import com.fif.screen.diagnostics.FifLog
import com.fif.screen.protocol.FifProtocol
import java.nio.ByteBuffer

class H264SurfaceDecoder(
    private val surface: Surface,
    private val width: Int,
    private val height: Int
) {
    data class Stats(
        val decoderName: String,
        val submittedFrames: Long,
        val renderedFrames: Long,
        val droppedFrames: Long,
        val lastDecodeLatencyMs: Double
    )

    private var codec: MediaCodec? = null
    private var decoderName: String = "unknown"
    private var submittedFrames = 0L
    private var renderedFrames = 0L
    private var droppedFrames = 0L
    private var lastSubmitNs = 0L
    private var lastDecodeLatencyMs = 0.0

    fun start() {
        val info = selectDecoder()
        decoderName = info.name
        FifLog.decoder("event" to "selected", "name" to decoderName, "mime" to "video/avc")
        val format = MediaFormat.createVideoFormat("video/avc", width, height)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            format.setInteger(MediaFormat.KEY_LOW_LATENCY, 1)
            FifLog.decoder("event" to "low_latency_requested", "api" to Build.VERSION.SDK_INT)
        }

        codec = MediaCodec.createByCodecName(info.name).apply {
            configure(format, surface, null, 0)
            start()
        }
        FifLog.decoder("event" to "started", "name" to decoderName)
    }

    fun submit(payload: ByteArray, flags: Int, timestampNs: Long) {
        val active = codec ?: return
        val inputIndex = active.dequeueInputBuffer(0)
        if (inputIndex < 0) {
            droppedFrames += 1
            drain(active)
            return
        }

        val buffer: ByteBuffer = active.getInputBuffer(inputIndex) ?: return
        buffer.clear()
        buffer.put(payload)
        val codecFlags =
            if ((flags and FifProtocol.FLAG_CODEC_CONFIG) != 0) MediaCodec.BUFFER_FLAG_CODEC_CONFIG else 0
        lastSubmitNs = System.nanoTime()
        active.queueInputBuffer(inputIndex, 0, payload.size, timestampNs / 1000L, codecFlags)
        submittedFrames += 1
        drain(active)
    }

    fun stop() {
        codec?.run {
            try {
                stop()
            } catch (_: IllegalStateException) {
            }
            release()
        }
        FifLog.decoder("event" to "stopped", "name" to decoderName)
        codec = null
    }

    fun stats(): Stats =
        Stats(decoderName, submittedFrames, renderedFrames, droppedFrames, lastDecodeLatencyMs)

    private fun drain(active: MediaCodec) {
        val info = MediaCodec.BufferInfo()
        while (true) {
            when (val outputIndex = active.dequeueOutputBuffer(info, 0)) {
                MediaCodec.INFO_TRY_AGAIN_LATER -> return
                MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> continue
                MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED -> continue
                else -> {
                    if (outputIndex >= 0) {
                        active.releaseOutputBuffer(outputIndex, true)
                        renderedFrames += 1
                        lastDecodeLatencyMs = (System.nanoTime() - lastSubmitNs) / 1_000_000.0
                    }
                }
            }
        }
    }

    private fun selectDecoder(): MediaCodecInfo {
        val candidates = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos
            .filter { !it.isEncoder && it.supportedTypes.any { type -> type.equals("video/avc", ignoreCase = true) } }
            .sortedWith(compareBy<MediaCodecInfo> { isSoftwareDecoder(it) }.thenBy { it.name })

        return candidates.firstOrNull()
            ?: throw IllegalStateException("no H.264 decoder found")
    }

    private fun isSoftwareDecoder(info: MediaCodecInfo): Boolean {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            return info.isSoftwareOnly
        }
        val name = info.name.lowercase()
        return name.startsWith("omx.google.") || name.startsWith("c2.android.")
    }
}
