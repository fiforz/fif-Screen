package com.fif.screen.net

import android.view.Surface
import com.fif.screen.BuildConfig
import com.fif.screen.diagnostics.FifLog
import com.fif.screen.protocol.FifProtocol
import com.fif.screen.video.DirtyRawSurfaceRenderer
import com.fif.screen.video.H264SurfaceDecoder
import com.fif.screen.video.RawSurfaceRenderer
import java.net.InetSocketAddress
import java.net.Socket
import java.nio.charset.StandardCharsets
import java.util.ArrayDeque
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock
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
    @Volatile private var inputReady = false
    private var controlSocket: Socket? = null
    private var videoSocket: Socket? = null
    private var inputThread: Thread? = null
    private var decoder: H264SurfaceDecoder? = null
    private var dirtyRawRenderer: DirtyRawSurfaceRenderer? = null
    private var rawRenderer: RawSurfaceRenderer? = null
    private val inputSequence = AtomicLong(2)
    private val inputLock = ReentrantLock()
    private val inputAvailable = inputLock.newCondition()
    private val inputQueue = ArrayDeque<QueuedInput>()
    private val controlWriteLock = Any()

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
            val ackJson = JSONObject(String(ack.payload, StandardCharsets.UTF_8))
            FifLog.network(
                "event" to "hello_ack_received",
                "android_timestamp_ns" to ackReceivedNs,
                "rtt_ms" to ((ackReceivedNs - helloSentNs) / 1_000_000.0)
            )
            if (ackJson.optJSONObject("input")?.optBoolean("touch", false) == true) {
                startInputSender(control)
            } else {
                FifLog.network("event" to "touch_unavailable")
            }

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

    fun sendTouchFrame(frame: FifProtocol.TouchFrame): Boolean {
        if (!running || !inputReady) {
            return false
        }
        val queued = QueuedInput(
            payload = FifProtocol.encodeTouchFrame(frame),
            moveOnly = frame.isMoveOnly
        )
        var overflow = false
        inputLock.withLock {
            if (!inputReady) {
                return false
            }
            if (queued.moveOnly && inputQueue.peekLast()?.moveOnly == true) {
                inputQueue.removeLast()
            }
            if (inputQueue.size >= MAX_INPUT_QUEUE_SIZE) {
                val iterator = inputQueue.iterator()
                while (iterator.hasNext()) {
                    if (iterator.next().moveOnly) {
                        iterator.remove()
                        break
                    }
                }
            }
            if (inputQueue.size >= MAX_INPUT_QUEUE_SIZE) {
                overflow = true
            } else {
                inputQueue.addLast(queued)
                inputAvailable.signal()
            }
        }
        if (overflow) {
            FifLog.network("event" to "touch_queue_overflow")
            failInputConnection()
        }
        return true
    }

    private fun connect(port: Int): Socket =
        Socket().apply {
            tcpNoDelay = true
            connect(InetSocketAddress(host, port), 3000)
            FifLog.network("event" to "connect_success", "host" to host, "port" to port)
        }

    private fun sendHello(socket: Socket): Long {
        val payload = """
            {"role":"android-client","protocol":1,"appVersion":"${BuildConfig.VERSION_NAME}","screen":{"width":1280,"height":720,"refreshHz":60},"decoders":[{"codec":"video/avc","lowLatency":true}],"input":{"touch":true,"maxContacts":${FifProtocol.MAX_TOUCH_CONTACTS}}}
        """.trimIndent().toByteArray(StandardCharsets.UTF_8)
        val timestampNs = System.nanoTime()
        FifProtocol.writePacket(socket.getOutputStream(), FifProtocol.TYPE_HELLO, 1, 0, payload)
        return timestampNs
    }

    private fun startInputSender(socket: Socket) {
        inputReady = true
        inputThread = Thread({ runInputSender(socket) }, "fifscreen-touch-sender").apply {
            isDaemon = true
            start()
        }
        FifLog.network("event" to "touch_sender_ready")
    }

    private fun runInputSender(socket: Socket) {
        try {
            val output = socket.getOutputStream()
            while (running) {
                val queued = inputLock.withLock {
                    while (running && inputQueue.isEmpty()) {
                        inputAvailable.await()
                    }
                    if (!running) null else inputQueue.removeFirst()
                } ?: break
                synchronized(controlWriteLock) {
                    FifProtocol.writePacket(
                        output,
                        FifProtocol.TYPE_INPUT_EVENT,
                        inputSequence.getAndIncrement(),
                        0,
                        queued.payload
                    )
                }
            }
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
        } catch (e: Exception) {
            if (running) {
                FifLog.network("event" to "touch_sender_error", "message" to e.message)
                failInputConnection()
            }
        }
    }

    private fun failInputConnection() {
        running = false
        inputReady = false
        try {
            controlSocket?.close()
        } catch (_: Exception) {
        }
        try {
            videoSocket?.close()
        } catch (_: Exception) {
        }
        inputLock.withLock { inputAvailable.signalAll() }
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
                    if (configuredCodec == "raw-rgb565-dirty") {
                        decoder?.stop()
                        decoder = null
                        rawRenderer = null
                        dirtyRawRenderer = DirtyRawSurfaceRenderer(surface, configuredWidth, configuredHeight)
                        listener.onStatus("Streaming dirty raw")
                    } else if (configuredCodec == "raw-rgba" || configuredCodec == "raw-rgb565") {
                        decoder?.stop()
                        decoder = null
                        dirtyRawRenderer = null
                        rawRenderer = RawSurfaceRenderer(surface, configuredWidth, configuredHeight, configuredCodec)
                        listener.onStatus("Streaming $configuredCodec")
                    } else {
                        dirtyRawRenderer = null
                        rawRenderer = null
                        if (decoder == null) {
                            FifLog.decoder("event" to "decoder_start_requested", "mime" to "video/avc")
                            decoder = H264SurfaceDecoder(surface, configuredWidth, configuredHeight).also { it.start() }
                        }
                        listener.onStatus("Streaming H.264")
                    }
                }
                FifProtocol.TYPE_VIDEO_FRAME -> {
                    val dirtyRaw = dirtyRawRenderer
                    val raw = rawRenderer
                    if (dirtyRaw != null) {
                        dirtyRaw.render(packet.payload)
                    } else if (raw != null) {
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
                val dirtyRawStats = dirtyRawRenderer?.stats()
                val h264Stats = decoder?.stats()
                when {
                    dirtyRawStats != null -> {
                        listener.onStats(
                            "${configuredWidth}x$configuredHeight  FPS $fps  $configuredCodec  " +
                                "bytes $videoBytesReceived  rendered ${dirtyRawStats.renderedFrames}  " +
                                "rects ${dirtyRawStats.dirtyRects}  full ${dirtyRawStats.fullFrames}  " +
                                "dropped ${dirtyRawStats.droppedFrames}"
                        )
                        FifLog.decoder(
                            "event" to "dirty_raw_stats",
                            "VIDEO_BYTES_RECEIVED" to videoBytesReceived,
                            "VIDEO_FRAMES_RECEIVED" to dirtyRawStats.submittedFrames,
                            "RENDERED_FRAMES" to dirtyRawStats.renderedFrames,
                            "DIRTY_RECTS" to dirtyRawStats.dirtyRects
                        )
                    }
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
                            "RENDERED_FRAMES" to h264Stats.renderedFrames,
                            "DROPPED_FRAMES" to h264Stats.droppedFrames,
                            "DECODE_LATENCY_MS" to h264Stats.lastDecodeLatencyMs.roundToInt()
                        )
                    }
                    else -> listener.onStats(
                        "waiting  FPS $fps  codec $configuredCodec  bytes $videoBytesReceived"
                    )
                }
            }
        }
    }

    @Synchronized
    private fun cleanup() {
        FifLog.network("event" to "cleanup")
        inputReady = false
        inputLock.withLock {
            inputQueue.clear()
            inputAvailable.signalAll()
        }
        try {
            controlSocket?.close()
        } catch (_: Exception) {
        }
        try {
            videoSocket?.close()
        } catch (_: Exception) {
        }
        val sender = inputThread
        inputThread = null
        sender?.interrupt()
        if (sender != null && sender !== Thread.currentThread()) {
            try {
                sender.join(250)
            } catch (_: InterruptedException) {
                Thread.currentThread().interrupt()
            }
        }
        decoder?.stop()
        dirtyRawRenderer = null
        rawRenderer = null
        controlSocket = null
        videoSocket = null
        decoder = null
    }

    private data class QueuedInput(val payload: ByteArray, val moveOnly: Boolean)

    private companion object {
        const val MAX_INPUT_QUEUE_SIZE = 64
    }
}
