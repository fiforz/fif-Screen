package com.fif.screen.net

import android.view.Surface
import com.fif.screen.diagnostics.FifLog
import com.fif.screen.protocol.FifProtocol
import com.fif.screen.video.H264SurfaceDecoder
import java.net.InetSocketAddress
import java.net.Socket
import java.nio.charset.StandardCharsets
import kotlin.math.roundToInt

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
            FifLog.decoder("event" to "decoder_start_requested", "mime" to "video/avc")
            decoder = H264SurfaceDecoder(surface, 1280, 720).also { it.start() }
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
        while (running) {
            val packet = FifProtocol.readPacket(socket.getInputStream(), FifProtocol.MAX_VIDEO_PAYLOAD)
            if (packet.header.type == FifProtocol.TYPE_VIDEO_CONFIG ||
                packet.header.type == FifProtocol.TYPE_VIDEO_FRAME
            ) {
                decoder?.submit(packet.payload, packet.header.flags, packet.header.timestampNs)
                receivedFrames += 1
            }

            val now = System.nanoTime()
            if (now - lastStatsNs >= 1_000_000_000L) {
                val stats = decoder?.stats()
                val fps = receivedFrames
                receivedFrames = 0
                lastStatsNs = now
                if (stats != null) {
                    listener.onStats(
                        "1280x720  FPS $fps  decoder ${stats.decoderName}  " +
                            "decode ${stats.lastDecodeLatencyMs.roundToInt()} ms  " +
                            "dropped ${stats.droppedFrames}"
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
        controlSocket = null
        videoSocket = null
        decoder = null
    }
}
