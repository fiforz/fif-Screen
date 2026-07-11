package com.fif.screen.net

import com.fif.screen.protocol.FifProtocol
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PairingCryptoTest {
    @Test
    fun matchesSharedPbkdf2AndHmacVector() {
        val challenge = FifProtocol.PairChallenge(
            iterations = 100_000,
            salt = ByteArray(16) { it.toByte() },
            serverNonce = ByteArray(32) { (0x10 + it).toByte() }
        )
        val clientNonce = ByteArray(32) { (0x30 + it).toByte() }
        val videoNonce = ByteArray(32) { (0x50 + it).toByte() }
        val material = PairingCrypto.deriveMaterial("1234", challenge, clientNonce)

        assertArrayEquals(
            hex("39f519ba5dddb0ff94c3e5513495e904efe1c0d1c80e76935bf83382a568fdcd"),
            material.controlProof
        )
        assertArrayEquals(
            hex("03c0519ce3121b0f74123b391f8ea86b53660e02e5ff9fdc0fbad3dccc84be1e"),
            material.sessionKey
        )
        assertArrayEquals(
            hex("6cc14c8f91464da895e2f1cead6a32de250db7a638a8c1b56e023085a4a6e9f6"),
            PairingCrypto.hostProof(material.sessionKey)
        )
        assertArrayEquals(
            hex("58f43d30ce583edeabd3115564e5483c49ba0c9ef9d11efbeac76c8a31339506"),
            PairingCrypto.videoProof(material.sessionKey, videoNonce)
        )
    }

    @Test
    fun pinUsesExactlyFourAsciiDigits() {
        assertTrue(PairingCrypto.isValidPin("0000"))
        assertTrue(PairingCrypto.isValidPin("9876"))
        assertFalse(PairingCrypto.isValidPin("123"))
        assertFalse(PairingCrypto.isValidPin("12a4"))
        assertFalse(PairingCrypto.isValidPin("１２３４"))
    }

    private fun hex(value: String): ByteArray =
        value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
}
