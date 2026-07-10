package com.fif.screen.video

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat
import android.os.Build
import android.os.Bundle
import android.view.Surface
import com.fif.screen.diagnostics.FifLog
import com.fif.screen.protocol.FifProtocol
import java.nio.ByteBuffer
import java.util.ArrayDeque

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
    private var lastDecodeLatencyMs = 0.0
    private val submitTimesNs = ArrayDeque<Long>()

    fun start() {
        val info = selectDecoder()
        decoderName = info.name
        FifLog.decoder("event" to "selected", "name" to decoderName, "mime" to "video/avc")
        val format = MediaFormat.createVideoFormat("video/avc", width, height)
        format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, MAX_INPUT_BYTES)
        format.setInteger(MediaFormat.KEY_FRAME_RATE, 50)
        format.setInteger(MediaFormat.KEY_PRIORITY, 0)
        format.setFloat(MediaFormat.KEY_OPERATING_RATE, 50f)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            format.setInteger(MediaFormat.KEY_LOW_LATENCY, 1)
            FifLog.decoder("event" to "low_latency_requested", "api" to Build.VERSION.SDK_INT)
        }

        codec = MediaCodec.createByCodecName(info.name).apply {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val vendorParameters = supportedVendorParameters
                val latencyParameters = vendorParameters.filter {
                    it.contains("latency", ignoreCase = true) ||
                        it.contains("lowlat", ignoreCase = true)
                }
                FifLog.decoder(
                    "event" to "vendor_parameters",
                    "count" to vendorParameters.size,
                    "latency_candidates" to latencyParameters.joinToString(",")
                )
                latencyParameters.forEach { name ->
                    FifLog.decoder(
                        "event" to "vendor_parameter",
                        "name" to name,
                        "type" to (getParameterDescriptor(name)?.type ?: -1)
                    )
                }
            }
            configure(format, surface, null, 0)
            start()
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                runCatching {
                    setParameters(Bundle().apply {
                        putInt(MediaCodec.PARAMETER_KEY_LOW_LATENCY, 1)
                    })
                }.onSuccess {
                    FifLog.decoder("event" to "low_latency_parameter_applied")
                }.onFailure { error ->
                    FifLog.decoder(
                        "event" to "low_latency_parameter_failed",
                        "error" to (error.message ?: error.javaClass.simpleName)
                    )
                }
            }
        }
        FifLog.decoder("event" to "started", "name" to decoderName)
    }

    fun submit(payload: ByteArray, flags: Int, timestampNs: Long) {
        val active = codec ?: return
        drain(active)
        if (payload.size > MAX_INPUT_BYTES) {
            droppedFrames += 1
            FifLog.decoder(
                "event" to "input_too_large",
                "payload_bytes" to payload.size,
                "capacity_bytes" to MAX_INPUT_BYTES
            )
            return
        }
        val inputIndex = active.dequeueInputBuffer(0)
        if (inputIndex < 0) {
            droppedFrames += 1
            return
        }

        val buffer: ByteBuffer = active.getInputBuffer(inputIndex) ?: run {
            droppedFrames += 1
            return
        }
        buffer.clear()
        if (payload.size > buffer.remaining()) {
            droppedFrames += 1
            FifLog.decoder(
                "event" to "input_too_large",
                "payload_bytes" to payload.size,
                "capacity_bytes" to buffer.remaining()
            )
            active.queueInputBuffer(
                inputIndex,
                0,
                0,
                timestampNs / 1000L,
                MediaCodec.BUFFER_FLAG_DECODE_ONLY
            )
            return
        }
        buffer.put(payload)
        val codecFlags =
            if ((flags and FifProtocol.FLAG_CODEC_CONFIG) != 0) MediaCodec.BUFFER_FLAG_CODEC_CONFIG else 0
        val submitNs = System.nanoTime()
        active.queueInputBuffer(inputIndex, 0, payload.size, timestampNs / 1000L, codecFlags)
        if (codecFlags == 0) {
            submittedFrames += 1
            submitTimesNs.addLast(submitNs)
        }
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
        submitTimesNs.clear()
    }

    fun stats(): Stats =
        Stats(decoderName, submittedFrames, renderedFrames, droppedFrames, lastDecodeLatencyMs)

    private fun drain(active: MediaCodec) {
        val info = MediaCodec.BufferInfo()
        while (true) {
            val outputIndex = active.dequeueOutputBuffer(info, 0)
            when (outputIndex) {
                MediaCodec.INFO_TRY_AGAIN_LATER -> return
                MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> continue
                MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED -> continue
                else -> {
                    if (outputIndex >= 0) {
                        active.releaseOutputBuffer(outputIndex, true)
                        renderedFrames += 1
                        submitTimesNs.pollFirst()?.let { submitNs ->
                            lastDecodeLatencyMs = (System.nanoTime() - submitNs) / 1_000_000.0
                        }
                    }
                }
            }
        }
    }

    private fun selectDecoder(): MediaCodecInfo {
        val candidates = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos
            .filter { !it.isEncoder && it.supportedTypes.any { type -> type.equals("video/avc", ignoreCase = true) } }
            .sortedWith(
                compareBy<MediaCodecInfo> { isSoftwareDecoder(it) }
                    .thenBy { !isVendorDecoder(it) }
                    .thenBy { it.name }
            )

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

    private fun isVendorDecoder(info: MediaCodecInfo): Boolean {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            return info.isVendor
        }
        val name = info.name.lowercase()
        return !name.startsWith("omx.google.") && !name.startsWith("c2.android.")
    }

    private companion object {
        const val MAX_INPUT_BYTES = 4 * 1024 * 1024
    }

}
