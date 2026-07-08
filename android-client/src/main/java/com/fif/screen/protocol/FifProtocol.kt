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
    const val TYPE_VIDEO_CONFIG = 100
    const val TYPE_VIDEO_FRAME = 101

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
}

class ProtocolException(message: String) : Exception(message)

