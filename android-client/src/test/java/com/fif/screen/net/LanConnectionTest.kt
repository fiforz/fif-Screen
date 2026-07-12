package com.fif.screen.net

import java.io.EOFException
import java.net.ConnectException
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LanConnectionTest {
    @Test
    fun manualHostAcceptsCanonicalIpv4() {
        assertEquals("192.168.1.10", LanHostAddress.normalize(" 192.168.1.10 "))
        assertEquals("10.0.0.8", LanHostAddress.normalize("010.000.000.008"))
    }

    @Test
    fun manualHostRejectsInvalidOrNonLanTargets() {
        assertNull(LanHostAddress.normalize("computer.local"))
        assertNull(LanHostAddress.normalize("192.168.1.999"))
        assertNull(LanHostAddress.normalize("127.0.0.1"))
        assertNull(LanHostAddress.normalize("224.0.0.1"))
        assertNull(LanHostAddress.normalize(""))
    }

    @Test
    fun streamingSocketCloseBecomesSilentReconnect() {
        val failure = ConnectionFailureMessages.describe(
            EOFException("socket closed"),
            ConnectionStage.STREAMING,
            ConnectionMode.LAN
        )

        assertEquals("连接已中断，正在自动重连", failure.message)
        assertFalse(failure.notifyUser)
        assertFalse(failure.message.contains("socket", ignoreCase = true))
    }

    @Test
    fun pairingSocketClosePointsToPinApplication() {
        val failure = ConnectionFailureMessages.describe(
            EOFException("socket closed"),
            ConnectionStage.CONTROL_PAIRING,
            ConnectionMode.LAN
        )

        assertTrue(failure.notifyUser)
        assertTrue(failure.message.contains("PIN"))
        assertFalse(failure.message.contains("socket", ignoreCase = true))
    }

    @Test
    fun lanConnectFailureIncludesWindowsAction() {
        val failure = ConnectionFailureMessages.describe(
            ConnectException("failed to connect"),
            ConnectionStage.CONTROL_CONNECT,
            ConnectionMode.LAN
        )

        assertTrue(failure.notifyUser)
        assertTrue(failure.message.contains("应用 PIN 并等待连接"))
    }

    @Test
    fun usbVideoClosePointsToExtensionDisplay() {
        val failure = ConnectionFailureMessages.describe(
            EOFException("socket closed"),
            ConnectionStage.VIDEO_STARTUP,
            ConnectionMode.USB
        )

        assertTrue(failure.notifyUser)
        assertTrue(failure.message.contains("启动扩展屏"))
        assertFalse(failure.message.contains("socket", ignoreCase = true))
    }
}
