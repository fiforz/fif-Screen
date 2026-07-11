package com.fif.screen.net

import com.fif.screen.protocol.FifProtocol
import java.security.MessageDigest
import java.security.SecureRandom
import javax.crypto.Mac
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.PBEKeySpec
import javax.crypto.spec.SecretKeySpec

object PairingCrypto {
    private val secureRandom = SecureRandom()
    private val controlLabel = "FifScreen/control/v1".toByteArray(Charsets.US_ASCII)
    private val sessionLabel = "FifScreen/session/v1".toByteArray(Charsets.US_ASCII)
    private val acceptedLabel = "FifScreen/accepted/v1".toByteArray(Charsets.US_ASCII)
    private val videoLabel = "FifScreen/video/v1".toByteArray(Charsets.US_ASCII)

    data class Material(val controlProof: ByteArray, val sessionKey: ByteArray)

    fun isValidPin(pin: String): Boolean = pin.length == 4 && pin.all { it in '0'..'9' }

    fun randomNonce(): ByteArray = ByteArray(FifProtocol.PAIRING_NONCE_SIZE).also {
        secureRandom.nextBytes(it)
    }

    fun deriveMaterial(
        pin: String,
        challenge: FifProtocol.PairChallenge,
        clientNonce: ByteArray
    ): Material {
        require(isValidPin(pin)) { "PIN must contain exactly four ASCII digits" }
        require(clientNonce.size == FifProtocol.PAIRING_NONCE_SIZE) { "invalid client nonce" }
        val spec = PBEKeySpec(pin.toCharArray(), challenge.salt, challenge.iterations, 256)
        val pinKey = try {
            SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256").generateSecret(spec).encoded
        } finally {
            spec.clearPassword()
        }
        return try {
            Material(
                controlProof = hmac(pinKey, controlLabel, challenge.serverNonce, clientNonce),
                sessionKey = hmac(pinKey, sessionLabel, challenge.serverNonce, clientNonce)
            )
        } finally {
            pinKey.fill(0)
        }
    }

    fun hostProof(sessionKey: ByteArray): ByteArray = hmac(sessionKey, acceptedLabel)

    fun videoProof(sessionKey: ByteArray, videoNonce: ByteArray): ByteArray =
        hmac(sessionKey, videoLabel, videoNonce)

    fun secureEquals(left: ByteArray, right: ByteArray): Boolean =
        MessageDigest.isEqual(left, right)

    private fun hmac(key: ByteArray, vararg parts: ByteArray): ByteArray {
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(key, "HmacSHA256"))
        parts.forEach(mac::update)
        return mac.doFinal()
    }
}
