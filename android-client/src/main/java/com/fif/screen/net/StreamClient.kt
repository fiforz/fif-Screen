package com.fif.screen.net

import android.view.Surface
import com.fif.screen.diagnostics.FifLog
import com.fif.screen.protocol.FifProtocol
import com.fif.screen.video.H264SurfaceDecoder
import com.fif.screen.video.RawSurfaceRenderer
import java.net.InetSocketAddress
import java.net.Socket
import java.nio.charset.StandardCharsets
import kotlin.math.roundToInt
import org.json.JSONObject

class StreamClient(
    private val surface: Surface,
    private val listener: Listener,
    private val host: String = "127.0.0.1",
    private val controlPort: Int = 27183,
    private val videoPort: Int = 27184
) {
    interface Listener {
        fun onStatus(text: String)
        fun onStats(text: String)
        fun onStopped()
    }

    @Volatile private var running = true
    private var controlSocket: Socket? = null
    private var videoSocket: Socket? = null
    private var decoder: H264SurfaceDecoder? = null
    private var rawRenderer: RawSurfaceRenderer? = null

    fun run() {
        try {
            listener.onStatus("Connecting control")
            FifLog.network("event" to "connect_start", "port" to controlPort)
            controlSocket = connect(controlPort)
            val control = controlSocket ?: error("control socket missing")
            val helloSentNs = sendHello(control)
            FifLog.network("event" to "hello_sent", "android_timestamp_ns" to helloSentNs)
            val ack = FifProtocol.readPacket(control.getInputStream(), FifProtocol.MAX_CONTROL_PAYLOAD)
            val ackReceivedNs = System.nanoTime()
            if (ack.header.type != FifProtocol.TYPE_HELLO_ACK) {
                error("expected HelloAck, got ${ack.header.type}")
            }
            FifLog.network(
                "event" to "hello_ack_received",
                "android_timestamp_ns" to ackReceivedNs,
                "rtt_ms" to ((ackReceivedNs - helloSentNs) / 1_000_000.0)
            )

            listener.onStatus("Connecting video")
            FifLog.network("event" to "connect_start", "port" to videoPort)
            videoSocket = connect(videoPort)
            readVideoLoop(videoSocket ?: error("video socket missing"))
        } catch (e: Exception) {
            if (running) {
                FifLog.network("event" to "client_error", "message" to e.message)
                listener.onStatus("Error: ${e.message}")
            }
        } finally {
            cleanup()
            listener.onStopped()
        }
    }

    fun stop() {
        running = false
        cleanup()
    }

    private fun connect(port: Int): Socket =
        Socket().apply {
            tcpNoDelay = true
            connect(InetSocketAddress(host, port), 3000)
            FifLog.network("event" to "connect_success", "host" to host, "port" to port)
        }

    private fun sendHello(socket: Socket): Long {
        val payload = """
            {"role":"android-client","protocol":1,"appVersion":"0.1.0","screen":{"width":1280,"height":720,"refreshHz":60},"decoders":[{"codec":"video/avc"}]}
        """.trimIndent().toByteArray(StandardCharsets.UTF_8)
        val timestampNs = System.nanoTime()
        FifProtocol.writePacket(socket.getOutputStream(), FifProtocol.TYPE_HELLO, 1, 0, payload)
        return timestampNs
    }

    private fun readVideoLoop(socket: Socket) {
        listener.onStatus("Streaming")
        var lastStatsNs = System.nanoTime()
        var receivedFrames = 0L
        var videoBytesReceived = 0L
        var configuredCodec = "waiting"
        var configuredWidth = 0
        var configuredHeight = 0
        while (running) {
            val packet = FifProtocol.readPacket(socket.getInputStream(), FifProtocol.MAX_VIDEO_PAYLOAD)
            videoBytesReceived += packet.payload.size.toLong()
            when (packet.header.type) {
                FifProtocol.TYPE_VIDEO_CONFIG -> {
                    val config = JSONObject(String(packet.payload, StandardCharsets.UTF_8))
                    configuredCodec = config.optString("codec", "video/avc")
                    configuredWidth = config.optInt("width", 1280)
                    configuredHeight = config.optInt("height", 720)
                    FifLog.decoder(
                        "event" to "video_config",
                        "codec" to configuredCodec,
                        "width" to configuredWidth,
                        "height" to configuredHeight
                    )
                    if (configuredCodec == "raw-rgba" || configuredCodec == "raw-rgb565") {
                        decoder?.stop()
                        decoder = null
                        rawRenderer = RawSurfaceRenderer(surface, configuredWidth, configuredHeight, configuredCodec)
                        listener.onStatus("Streaming $configuredCodec")
                    } else {
                        rawRenderer = null
                        if (decoder == null) {
                            FifLog.decoder("event" to "decoder_start_requested", "mime" to "video/avc")
                            decoder = H264SurfaceDecoder(surface, configuredWidth, configuredHeight).also { it.start() }
                        }
                        listener.onStatus("Streaming H.264")
                    }
                }
                FifProtocol.TYPE_VIDEO_FRAME -> {
                    val raw = rawRenderer
                    if (raw != null) {
                        raw.render(packet.payload)
                    } else {
                        if (decoder == null) {
                            configuredCodec = "video/avc"
                            configuredWidth = if (configuredWidth == 0) 1280 else configuredWidth
                            configuredHeight = if (configuredHeight == 0) 720 else configuredHeight
                            decoder = H264SurfaceDecoder(surface, configuredWidth, configuredHeight).also { it.start() }
                        }
                        decoder?.submit(packet.payload, packet.header.flags, packet.header.timestampNs)
                    }
                    receivedFrames += 1
                }
            }

            val now = System.nanoTime()
            if (now - lastStatsNs >= 1_000_000_000L) {
                val fps = receivedFrames
                receivedFrames = 0
                lastStatsNs = now
                val rawStats = rawRenderer?.stats()
                val h264Stats = decoder?.stats()
                when {
                    rawStats != null -> {
                        listener.onStats(
                            "${configuredWidth}x$configuredHeight  FPS $fps  $configuredCodec  " +
                                "bytes $videoBytesReceived  rendered ${rawStats.renderedFrames}  " +
                                "dropped ${rawStats.droppedFrames}"
                        )
                        FifLog.decoder(
                            "event" to "raw_stats",
                            "VIDEO_BYTES_RECEIVED" to videoBytesReceived,
                            "VIDEO_FRAMES_RECEIVED" to rawStats.submittedFrames,
                            "RENDERED_FRAMES" to rawStats.renderedFrames
                        )
                    }
                    h264Stats != null -> {
                        listener.onStats(
                            "${configuredWidth}x$configuredHeight  FPS $fps  decoder ${h264Stats.decoderName}  " +
                                "decode ${h264Stats.lastDecodeLatencyMs.roundToInt()} ms  " +
                                "dropped ${h264Stats.droppedFrames}"
                        )
                        FifLog.decoder(
                            "event" to "h264_stats",
                            "VIDEO_BYTES_RECEIVED" to videoBytesReceived,
                            "VIDEO_FRAMES_RECEIVED" to h264Stats.submittedFrames,
                            "DECODER_INPUT_FRAMES" to h264Stats.submittedFrames,
                            "DECODER_OUTPUT_FRAMES" to h264Stats.renderedFrames,
                            "RENDERED_FRAMES" to h264Stats.renderedFrames
                        )
                    }
                    else -> listener.onStats(
                        "waiting  FPS $fps  codec $configuredCodec  bytes $videoBytesReceived"
                    )
                }
            }
        }
    }

    private fun cleanup() {
        FifLog.network("event" to "cleanup")
        try {
            controlSocket?.close()
        } catch (_: Exception) {
        }
        try {
            videoSocket?.close()
        } catch (_: Exception) {
        }
        decoder?.stop()
        rawRenderer = null
        controlSocket = null
        videoSocket = null
        decoder = null
    }
}
