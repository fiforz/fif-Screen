package com.fif.screen.protocol

import java.io.EOFException
import java.io.InputStream
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

object FifProtocol {
    private val MAGIC = byteArrayOf('F'.code.toByte(), 'I'.code.toByte(), 'F'.code.toByte(), '1'.code.toByte())
    const val VERSION: Short = 1
    const val HEADER_SIZE = 32
    const val MAX_CONTROL_PAYLOAD = 1024 * 1024
    const val MAX_VIDEO_PAYLOAD = 16 * 1024 * 1024

    const val TYPE_HELLO = 1
    const val TYPE_HELLO_ACK = 2
    const val TYPE_PING = 3
    const val TYPE_PONG = 4
    const val TYPE_STATS = 7
    const val TYPE_REQUEST_IDR = 8
    const val TYPE_PAIR_CHALLENGE = 10
    const val TYPE_PAIR_RESPONSE = 11
    const val TYPE_PAIR_RESULT = 12
    const val TYPE_VIDEO_CHALLENGE = 13
    const val TYPE_VIDEO_AUTH = 14
    const val TYPE_VIDEO_CONFIG = 100
    const val TYPE_VIDEO_FRAME = 101
    const val TYPE_INPUT_EVENT = 200

    const val INPUT_KIND_TOUCH_FRAME = 1
    const val INPUT_PAYLOAD_VERSION = 1
    const val MAX_TOUCH_CONTACTS = 16
    const val TOUCH_FRAME_HEADER_SIZE = 4
    const val TOUCH_CONTACT_SIZE = 14
    const val MAX_TOUCH_POINTER_ID = 256
    const val MAX_TOUCH_PRESSURE = 1024
    const val DISCOVERY_PORT = 27182
    const val DISCOVERY_PACKET_SIZE = 20
    const val PAIRING_PAYLOAD_VERSION = 1
    const val PAIRING_PBKDF2_ITERATIONS = 100_000
    const val PAIRING_SALT_SIZE = 16
    const val PAIRING_NONCE_SIZE = 32
    const val PAIRING_PROOF_SIZE = 32
    const val PAIR_CHALLENGE_PAYLOAD_SIZE = 56
    const val PAIR_RESPONSE_PAYLOAD_SIZE = 68
    const val PAIR_RESULT_PAYLOAD_SIZE = 36
    const val VIDEO_CHALLENGE_PAYLOAD_SIZE = 36
    const val VIDEO_AUTH_PAYLOAD_SIZE = 36
    private val DISCOVERY_REQUEST_MAGIC = "FIFDISC1".toByteArray(Charsets.US_ASCII)
    private val DISCOVERY_RESPONSE_MAGIC = "FIFHERE1".toByteArray(Charsets.US_ASCII)

    const val FLAG_CODEC_CONFIG = 1
    const val FLAG_IDR_FRAME = 1 shl 1
    const val FLAG_DROPPED_BEFORE = 1 shl 2

    data class Header(
        val version: Int,
        val type: Int,
        val payloadLength: Int,
        val sequence: Long,
        val timestampNs: Long,
        val flags: Int
    )

    data class Packet(val header: Header, val payload: ByteArray)

    data class DiscoveryResponse(
        val controlPort: Int,
        val videoPort: Int,
        val requestNonce: Int
    )

    data class PairChallenge(
        val iterations: Int,
        val salt: ByteArray,
        val serverNonce: ByteArray
    )

    data class PairResponse(val clientNonce: ByteArray, val proof: ByteArray)

    data class PairResult(val accepted: Boolean, val hostProof: ByteArray)

    data class VideoChallenge(val nonce: ByteArray)

    enum class TouchPhase(val wireValue: Int) {
        DOWN(1),
        MOVE(2),
        UP(3),
        CANCEL(4)
    }

    data class TouchContact(
        val pointerId: Int,
        val phase: TouchPhase,
        val x: Int,
        val y: Int,
        val pressure: Int,
        val major: Int,
        val minor: Int
    )

    data class TouchFrame(val contacts: List<TouchContact>) {
        val isMoveOnly: Boolean
            get() = contacts.all { it.phase == TouchPhase.MOVE }
    }

    fun encodeDiscoveryRequest(requestNonce: Int): ByteArray =
        ByteBuffer.allocate(DISCOVERY_PACKET_SIZE).order(ByteOrder.LITTLE_ENDIAN).apply {
            put(DISCOVERY_REQUEST_MAGIC)
            putShort(VERSION)
            putShort(0.toShort())
            putShort(0.toShort())
            putShort(0.toShort())
            putInt(requestNonce)
        }.array()

    fun decodeDiscoveryResponse(payload: ByteArray): DiscoveryResponse {
        require(payload.size == DISCOVERY_PACKET_SIZE) { "invalid discovery packet length" }
        require(payload.copyOfRange(0, 8).contentEquals(DISCOVERY_RESPONSE_MAGIC)) {
            "invalid discovery response magic"
        }
        val buffer = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
        buffer.position(8)
        require((buffer.short.toInt() and 0xffff) == VERSION.toInt()) {
            "unsupported discovery version"
        }
        require(buffer.short.toInt() == 0) { "invalid discovery reserved field" }
        val controlPort = buffer.short.toInt() and 0xffff
        val videoPort = buffer.short.toInt() and 0xffff
        val requestNonce = buffer.int
        require(controlPort != 0 && videoPort != 0) { "invalid discovery ports" }
        return DiscoveryResponse(controlPort, videoPort, requestNonce)
    }

    fun decodePairChallenge(payload: ByteArray): PairChallenge {
        requireVersionedPayload(payload, PAIR_CHALLENGE_PAYLOAD_SIZE, "pairing challenge")
        val buffer = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
        buffer.position(4)
        val iterations = buffer.int
        require(iterations in 10_000..1_000_000) { "invalid pairing iteration count" }
        val salt = ByteArray(PAIRING_SALT_SIZE).also(buffer::get)
        val serverNonce = ByteArray(PAIRING_NONCE_SIZE).also(buffer::get)
        return PairChallenge(iterations, salt, serverNonce)
    }

    fun encodePairResponse(value: PairResponse): ByteArray {
        require(value.clientNonce.size == PAIRING_NONCE_SIZE) { "invalid client nonce" }
        require(value.proof.size == PAIRING_PROOF_SIZE) { "invalid pairing proof" }
        return ByteBuffer.allocate(PAIR_RESPONSE_PAYLOAD_SIZE).order(ByteOrder.LITTLE_ENDIAN).apply {
            put(PAIRING_PAYLOAD_VERSION.toByte())
            put(byteArrayOf(0, 0, 0))
            put(value.clientNonce)
            put(value.proof)
        }.array()
    }

    fun decodePairResult(payload: ByteArray): PairResult {
        require(payload.size == PAIR_RESULT_PAYLOAD_SIZE &&
            payload[0].toInt() == PAIRING_PAYLOAD_VERSION &&
            payload[1].toInt() in 0..1 &&
            payload[2].toInt() == 0 && payload[3].toInt() == 0
        ) { "invalid pairing result" }
        return PairResult(
            payload[1].toInt() == 1,
            payload.copyOfRange(4, PAIR_RESULT_PAYLOAD_SIZE)
        )
    }

    fun decodeVideoChallenge(payload: ByteArray): VideoChallenge {
        requireVersionedPayload(payload, VIDEO_CHALLENGE_PAYLOAD_SIZE, "video challenge")
        return VideoChallenge(payload.copyOfRange(4, VIDEO_CHALLENGE_PAYLOAD_SIZE))
    }

    fun encodeVideoAuth(proof: ByteArray): ByteArray {
        require(proof.size == PAIRING_PROOF_SIZE) { "invalid video proof" }
        return ByteBuffer.allocate(VIDEO_AUTH_PAYLOAD_SIZE).order(ByteOrder.LITTLE_ENDIAN).apply {
            put(PAIRING_PAYLOAD_VERSION.toByte())
            put(byteArrayOf(0, 0, 0))
            put(proof)
        }.array()
    }

    fun encodeTouchFrame(frame: TouchFrame): ByteArray {
        require(frame.contacts.isNotEmpty() && frame.contacts.size <= MAX_TOUCH_CONTACTS) {
            "invalid touch contact count"
        }
        require(frame.contacts.map { it.pointerId }.distinct().size == frame.contacts.size) {
            "duplicate touch pointer id"
        }

        val buffer = ByteBuffer.allocate(
            TOUCH_FRAME_HEADER_SIZE + frame.contacts.size * TOUCH_CONTACT_SIZE
        ).order(ByteOrder.LITTLE_ENDIAN)
        buffer.put(INPUT_KIND_TOUCH_FRAME.toByte())
        buffer.put(INPUT_PAYLOAD_VERSION.toByte())
        buffer.put(frame.contacts.size.toByte())
        buffer.put(0)
        frame.contacts.forEach { contact ->
            require(contact.pointerId in 1..MAX_TOUCH_POINTER_ID) { "invalid touch pointer id" }
            require(contact.x in 0..0xffff && contact.y in 0..0xffff) {
                "invalid touch coordinates"
            }
            require(contact.pressure in 0..MAX_TOUCH_PRESSURE) { "invalid touch pressure" }
            require(contact.major in 0..0xffff && contact.minor in 0..0xffff) {
                "invalid touch contact size"
            }
            buffer.putShort(contact.pointerId.toShort())
            buffer.put(contact.phase.wireValue.toByte())
            buffer.put(0)
            buffer.putShort(contact.x.toShort())
            buffer.putShort(contact.y.toShort())
            buffer.putShort(contact.pressure.toShort())
            buffer.putShort(contact.major.toShort())
            buffer.putShort(contact.minor.toShort())
        }
        return buffer.array()
    }

    fun readPacket(input: InputStream, maxPayload: Int): Packet {
        val headerBytes = readExact(input, HEADER_SIZE)
        if (!headerBytes.copyOfRange(0, 4).contentEquals(MAGIC)) {
            throw ProtocolException("invalid magic")
        }

        val buffer = ByteBuffer.wrap(headerBytes).order(ByteOrder.LITTLE_ENDIAN)
        buffer.position(4)
        val version = buffer.short.toInt() and 0xffff
        if (version != VERSION.toInt()) {
            throw ProtocolException("unsupported version $version")
        }
        val type = buffer.short.toInt() and 0xffff
        val payloadLength = buffer.int
        if (payloadLength < 0 || payloadLength > maxPayload) {
            throw ProtocolException("invalid payload length $payloadLength")
        }
        val sequence = buffer.long
        val timestamp = buffer.long
        val flags = buffer.int

        return Packet(
            Header(version, type, payloadLength, sequence, timestamp, flags),
            readExact(input, payloadLength)
        )
    }

    fun writePacket(output: OutputStream, type: Int, sequence: Long, flags: Int, payload: ByteArray) {
        if (payload.size > MAX_CONTROL_PAYLOAD) {
            throw ProtocolException("control payload too large")
        }
        val header = ByteBuffer.allocate(HEADER_SIZE).order(ByteOrder.LITTLE_ENDIAN)
        header.put(MAGIC)
        header.putShort(VERSION)
        header.putShort(type.toShort())
        header.putInt(payload.size)
        header.putLong(sequence)
        header.putLong(System.nanoTime())
        header.putInt(flags)
        output.write(header.array())
        output.write(payload)
        output.flush()
    }

    private fun readExact(input: InputStream, count: Int): ByteArray {
        val out = ByteArray(count)
        var offset = 0
        while (offset < count) {
            val read = input.read(out, offset, count - offset)
            if (read < 0) throw EOFException("socket closed")
            offset += read
        }
        return out
    }

    private fun requireVersionedPayload(payload: ByteArray, size: Int, name: String) {
        require(payload.size == size &&
            payload[0].toInt() == PAIRING_PAYLOAD_VERSION &&
            payload[1].toInt() == 0 && payload[2].toInt() == 0 && payload[3].toInt() == 0
        ) { "invalid $name" }
    }
}

class ProtocolException(message: String) : Exception(message)
