package com.fif.screen.protocol

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.file.Files
import java.nio.file.Path

class FifProtocolTest {
    @Test
    fun sharedHelloVectorParses() {
        val packet = readVector("hello.bin")
        assertEquals(FifProtocol.TYPE_HELLO, packet.header.type)
        assertEquals(1L, packet.header.sequence)
        assertEquals(1_000_000_000L, packet.header.timestampNs)
        assertEquals("""{"role":"android-client","protocol":1}""", packet.payload.decodeToString())
    }

    @Test
    fun sharedHelloAckVectorParses() {
        val packet = readVector("hello_ack.bin")
        assertEquals(FifProtocol.TYPE_HELLO_ACK, packet.header.type)
        assertEquals(2L, packet.header.sequence)
        assertEquals(1_000_001_000L, packet.header.timestampNs)
        assertEquals(
            """{"role":"windows-host","protocol":1,"controlPort":27183,"videoPort":27184}""",
            packet.payload.decodeToString()
        )
    }

    @Test
    fun sharedVideoHeaderVectorParses() {
        val packet = readVector("video_header.bin", FifProtocol.MAX_VIDEO_PAYLOAD)
        assertEquals(FifProtocol.TYPE_VIDEO_FRAME, packet.header.type)
        assertEquals(3L, packet.header.sequence)
        assertEquals(FifProtocol.FLAG_IDR_FRAME, packet.header.flags)
        assertEquals(0, packet.payload.size)
    }

    @Test
    fun partialPacketParses() {
        val bytes = vectorBytes("hello.bin")
        val packet = FifProtocol.readPacket(ChunkedInputStream(bytes, 3), FifProtocol.MAX_CONTROL_PAYLOAD)
        assertEquals(FifProtocol.TYPE_HELLO, packet.header.type)
    }

    @Test
    fun stickyPacketsParseSequentially() {
        val joined = vectorBytes("hello.bin") + vectorBytes("hello_ack.bin")
        val input = ByteArrayInputStream(joined)
        assertEquals(FifProtocol.TYPE_HELLO, FifProtocol.readPacket(input, FifProtocol.MAX_CONTROL_PAYLOAD).header.type)
        assertEquals(FifProtocol.TYPE_HELLO_ACK, FifProtocol.readPacket(input, FifProtocol.MAX_CONTROL_PAYLOAD).header.type)
    }

    @Test
    fun invalidPayloadLengthIsRejected() {
        val bytes = vectorBytes("hello.bin").copyOf()
        ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).putInt(8, FifProtocol.MAX_CONTROL_PAYLOAD + 1)
        assertThrows(ProtocolException::class.java) {
            FifProtocol.readPacket(ByteArrayInputStream(bytes), FifProtocol.MAX_CONTROL_PAYLOAD)
        }
    }

    @Test
    fun unsupportedVersionIsRejected() {
        val bytes = vectorBytes("hello.bin").copyOf()
        ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).putShort(4, 99)
        assertThrows(ProtocolException::class.java) {
            FifProtocol.readPacket(ByteArrayInputStream(bytes), FifProtocol.MAX_CONTROL_PAYLOAD)
        }
    }

    @Test
    fun helloAckEncodesAndDecodes() {
        val payload = """{"role":"windows-host","protocol":1}""".toByteArray()
        val output = ByteArrayOutputStream()
        FifProtocol.writePacket(output, FifProtocol.TYPE_HELLO_ACK, 42, 0, payload)
        val packet = FifProtocol.readPacket(ByteArrayInputStream(output.toByteArray()), FifProtocol.MAX_CONTROL_PAYLOAD)
        assertEquals(FifProtocol.TYPE_HELLO_ACK, packet.header.type)
        assertEquals(42L, packet.header.sequence)
        assertArrayEquals(payload, packet.payload)
    }

    @Test
    fun touchFrameEncodesLittleEndian() {
        val payload = FifProtocol.encodeTouchFrame(
            FifProtocol.TouchFrame(
                listOf(
                    FifProtocol.TouchContact(
                        pointerId = 1,
                        phase = FifProtocol.TouchPhase.DOWN,
                        x = 0,
                        y = 65535,
                        pressure = 768,
                        major = 2048,
                        minor = 1024
                    ),
                    FifProtocol.TouchContact(
                        pointerId = 2,
                        phase = FifProtocol.TouchPhase.MOVE,
                        x = 32768,
                        y = 16384,
                        pressure = 512,
                        major = 0,
                        minor = 0
                    )
                )
            )
        )

        val buffer = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
        assertEquals(FifProtocol.INPUT_KIND_TOUCH_FRAME, buffer.get().toInt())
        assertEquals(FifProtocol.INPUT_PAYLOAD_VERSION, buffer.get().toInt())
        assertEquals(2, buffer.get().toInt())
        buffer.get()
        assertEquals(1, buffer.short.toInt())
        assertEquals(FifProtocol.TouchPhase.DOWN.wireValue, buffer.get().toInt())
        buffer.get()
        assertEquals(0, buffer.short.toInt())
        assertEquals(65535, buffer.short.toInt() and 0xffff)
        assertEquals(768, buffer.short.toInt())
        assertEquals(2048, buffer.short.toInt())
        assertEquals(1024, buffer.short.toInt())
        assertEquals(2, buffer.short.toInt())
        assertEquals(FifProtocol.TouchPhase.MOVE.wireValue, buffer.get().toInt())
    }

    @Test
    fun duplicateTouchPointerIdIsRejected() {
        val contact = FifProtocol.TouchContact(
            pointerId = 1,
            phase = FifProtocol.TouchPhase.DOWN,
            x = 1,
            y = 1,
            pressure = 1,
            major = 1,
            minor = 1
        )
        assertThrows(IllegalArgumentException::class.java) {
            FifProtocol.encodeTouchFrame(FifProtocol.TouchFrame(listOf(contact, contact)))
        }
    }

    private fun readVector(file: String, maxPayload: Int = FifProtocol.MAX_CONTROL_PAYLOAD): FifProtocol.Packet =
        FifProtocol.readPacket(ByteArrayInputStream(vectorBytes(file)), maxPayload)

    private fun vectorBytes(file: String): ByteArray {
        val root = findRepoRoot()
        return Files.readAllBytes(root.resolve("protocol").resolve("test-vectors").resolve(file))
    }

    private fun findRepoRoot(): Path {
        var path = Path.of("").toAbsolutePath()
        while (true) {
            if (Files.exists(path.resolve("protocol").resolve("test-vectors").resolve("test-vectors.json"))) {
                return path
            }
            path = path.parent ?: error("repo root not found")
        }
    }

    private class ChunkedInputStream(
        private val bytes: ByteArray,
        private val chunkSize: Int
    ) : InputStream() {
        private var offset = 0

        override fun read(): Int {
            if (offset >= bytes.size) return -1
            return bytes[offset++].toInt() and 0xff
        }

        override fun read(buffer: ByteArray, off: Int, len: Int): Int {
            if (offset >= bytes.size) return -1
            val count = minOf(chunkSize, len, bytes.size - offset)
            bytes.copyInto(buffer, off, offset, offset + count)
            offset += count
            return count
        }
    }
}
