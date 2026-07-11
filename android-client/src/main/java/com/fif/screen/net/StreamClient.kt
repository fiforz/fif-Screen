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
    private val endpointProvider: EndpointProvider = UsbEndpointProvider()
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
    private val controlSequence = AtomicLong(1)
    private val inputLock = ReentrantLock()
    private val inputAvailable = inputLock.newCondition()
    private val inputQueue = ArrayDeque<QueuedInput>()
    private val controlWriteLock = Any()

    fun run() {
        var sessionKey: ByteArray? = null
        try {
            listener.onStatus("Resolving connection")
            val endpoint = endpointProvider.resolve()
            FifLog.network(
                "event" to "endpoint_resolved",
                "transport" to endpoint.mode.preferenceValue,
                "host" to endpoint.host,
                "control_port" to endpoint.controlPort,
                "video_port" to endpoint.videoPort
            )
            listener.onStatus("Connecting control")
            FifLog.network("event" to "connect_start", "port" to endpoint.controlPort)
            controlSocket = connect(endpoint.host, endpoint.controlPort)
            val control = controlSocket ?: error("control socket missing")
            if (endpoint.mode == ConnectionMode.LAN) {
                sessionKey = pairControl(control, endpoint.pairingPin ?: error("pairing PIN missing"))
            }
            val helloSentNs = sendHello(control, endpoint.mode)
            FifLog.network("event" to "hello_sent", "android_timestamp_ns" to helloSentNs)
            val ack = FifProtocol.readPacket(control.getInputStream(), FifProtocol.MAX_CONTROL_PAYLOAD)
            val ackReceivedNs = System.nanoTime()
            if (ack.header.type != FifProtocol.TYPE_HELLO_ACK) {
                error("expected HelloAck, got ${ack.header.type}")
            }
            val ackJson = JSONObject(String(ack.payload, StandardCharsets.UTF_8))
            val touchEnabled = ackJson.optJSONObject("input")?.optBoolean("touch", false) == true
            FifLog.network(
                "event" to "hello_ack_received",
                "android_timestamp_ns" to ackReceivedNs,
                "rtt_ms" to ((ackReceivedNs - helloSentNs) / 1_000_000.0),
                "touch" to touchEnabled
            )
            if (touchEnabled) {
                startInputSender(control)
            } else {
                FifLog.network("event" to "touch_unavailable")
            }

            listener.onStatus("Connecting video")
            FifLog.network("event" to "connect_start", "port" to endpoint.videoPort)
            videoSocket = connect(endpoint.host, endpoint.videoPort)
            val video = videoSocket ?: error("video socket missing")
            if (endpoint.mode == ConnectionMode.LAN) {
                pairVideo(video, sessionKey ?: error("LAN session key missing"))
            }
            readVideoLoop(video)
        } catch (e: Exception) {
            if (running) {
                FifLog.network("event" to "client_error", "message" to e.message)
                listener.onStatus("Error: ${e.message}")
            }
        } finally {
            sessionKey?.fill(0)
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
            if (!frame.isMoveOnly) {
                FifLog.input(
                    "event" to "touch_not_ready",
                    "running" to running,
                    "input_ready" to inputReady
                )
            }
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

    private fun connect(host: String, port: Int): Socket =
        Socket().apply {
            tcpNoDelay = true
            connect(InetSocketAddress(host, port), 3000)
            FifLog.network("event" to "connect_success", "host" to host, "port" to port)
        }

    private fun sendHello(socket: Socket, mode: ConnectionMode): Long {
        val payload = """
            {"role":"android-client","protocol":1,"appVersion":"${BuildConfig.VERSION_NAME}","transport":"${mode.preferenceValue}","screen":{"width":1280,"height":720,"refreshHz":60},"decoders":[{"codec":"video/avc","lowLatency":true}],"input":{"touch":true,"maxContacts":${FifProtocol.MAX_TOUCH_CONTACTS}}}
        """.trimIndent().toByteArray(StandardCharsets.UTF_8)
        val timestampNs = System.nanoTime()
        FifProtocol.writePacket(
            socket.getOutputStream(),
            FifProtocol.TYPE_HELLO,
            controlSequence.getAndIncrement(),
            0,
            payload
        )
        return timestampNs
    }

    private fun pairControl(socket: Socket, pin: String): ByteArray {
        listener.onStatus("Pairing LAN")
        socket.soTimeout = PAIRING_TIMEOUT_MS
        val challengePacket = FifProtocol.readPacket(
            socket.getInputStream(), FifProtocol.MAX_CONTROL_PAYLOAD
        )
        if (challengePacket.header.type != FifProtocol.TYPE_PAIR_CHALLENGE) {
            throw PairingException("电脑未进入无线配对模式")
        }
        val challenge = FifProtocol.decodePairChallenge(challengePacket.payload)
        val clientNonce = PairingCrypto.randomNonce()
        val material = PairingCrypto.deriveMaterial(pin, challenge, clientNonce)
        var paired = false
        try {
            try {
                FifProtocol.writePacket(
                    socket.getOutputStream(),
                    FifProtocol.TYPE_PAIR_RESPONSE,
                    controlSequence.getAndIncrement(),
                    0,
                    FifProtocol.encodePairResponse(
                        FifProtocol.PairResponse(clientNonce, material.controlProof)
                    )
                )
            } finally {
                material.controlProof.fill(0)
            }
            val resultPacket = FifProtocol.readPacket(
                socket.getInputStream(), FifProtocol.MAX_CONTROL_PAYLOAD
            )
            if (resultPacket.header.type != FifProtocol.TYPE_PAIR_RESULT) {
                throw PairingException("无线配对响应无效")
            }
            val result = FifProtocol.decodePairResult(resultPacket.payload)
            val expectedHostProof = PairingCrypto.hostProof(material.sessionKey)
            val proofValid = PairingCrypto.secureEquals(result.hostProof, expectedHostProof)
            expectedHostProof.fill(0)
            if (!result.accepted || !proofValid) {
                throw PairingException("四位 PIN 不一致")
            }
            socket.soTimeout = 0
            FifLog.network("event" to "lan_pairing_accepted")
            paired = true
            return material.sessionKey
        } finally {
            material.controlProof.fill(0)
            if (!paired) {
                material.sessionKey.fill(0)
            }
        }
    }

    private fun pairVideo(socket: Socket, sessionKey: ByteArray) {
        socket.soTimeout = PAIRING_TIMEOUT_MS
        val challengePacket = FifProtocol.readPacket(
            socket.getInputStream(), FifProtocol.MAX_CONTROL_PAYLOAD
        )
        if (challengePacket.header.type != FifProtocol.TYPE_VIDEO_CHALLENGE) {
            throw PairingException("无线视频认证响应无效")
        }
        val challenge = FifProtocol.decodeVideoChallenge(challengePacket.payload)
        val proof = PairingCrypto.videoProof(sessionKey, challenge.nonce)
        try {
            FifProtocol.writePacket(
                socket.getOutputStream(),
                FifProtocol.TYPE_VIDEO_AUTH,
                controlSequence.getAndIncrement(),
                0,
                FifProtocol.encodeVideoAuth(proof)
            )
        } finally {
            proof.fill(0)
        }
        socket.soTimeout = 0
        FifLog.network("event" to "lan_video_authenticated")
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
                        controlSequence.getAndIncrement(),
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
        const val PAIRING_TIMEOUT_MS = 7000
    }
}

class PairingException(message: String) : Exception(message)
