package com.fif.screen.diagnostics

import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.os.Build

object H264CapabilityLogger {
    private const val MIME = "video/avc"

    fun logAvailableDecoders() {
        val codecs = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos
            .filter { !it.isEncoder && it.supportedTypes.any { type -> type.equals(MIME, ignoreCase = true) } }
            .sortedBy { it.name }

        FifLog.decoder(
            "event" to "enumerate_h264",
            "mime" to MIME,
            "decoder_count" to codecs.size
        )

        codecs.forEach { info ->
            val caps = info.getCapabilitiesForType(MIME)
            val videoCaps = caps.videoCapabilities
            FifLog.decoder(
                "event" to "h264_decoder",
                "name" to info.name,
                "mime" to MIME,
                "hardware_accelerated" to isHardwareAccelerated(info),
                "software_only" to isSoftwareOnly(info),
                "vendor" to isVendor(info),
                "profiles_levels" to caps.profileLevels.joinToString(",") { "${it.profile}:${it.level}" },
                "width_range" to videoCaps.supportedWidths,
                "height_range" to videoCaps.supportedHeights,
                "frame_rate_range" to videoCaps.supportedFrameRates,
                "low_latency_feature" to lowLatencyFeature(caps)
            )
        }
    }

    private fun isHardwareAccelerated(info: MediaCodecInfo): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            info.isHardwareAccelerated
        } else {
            !isSoftwareOnly(info)
        }

    private fun isSoftwareOnly(info: MediaCodecInfo): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            info.isSoftwareOnly
        } else {
            val name = info.name.lowercase()
            name.startsWith("omx.google.") || name.startsWith("c2.android.")
        }

    private fun isVendor(info: MediaCodecInfo): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            info.isVendor
        } else {
            !isSoftwareOnly(info)
        }

    private fun lowLatencyFeature(caps: MediaCodecInfo.CodecCapabilities): String =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            caps.isFeatureSupported(MediaCodecInfo.CodecCapabilities.FEATURE_LowLatency).toString()
        } else {
            "api_unsupported"
        }
}
