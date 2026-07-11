package com.fif.screen.net

import com.fif.screen.diagnostics.FifLog
import com.fif.screen.protocol.FifProtocol
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.Inet4Address
import java.net.InetAddress
import java.net.NetworkInterface
import java.net.SocketTimeoutException
import java.security.SecureRandom

class LanDiscovery(
    private val discoveryPort: Int = FifProtocol.DISCOVERY_PORT,
    private val timeoutMs: Int = 1800
) {
    data class Result(val host: String, val controlPort: Int, val videoPort: Int)

    fun discover(): Result {
        val requestNonce = SecureRandom().nextInt()
        val request = FifProtocol.encodeDiscoveryRequest(requestNonce)
        DatagramSocket().use { socket ->
            socket.broadcast = true
            socket.reuseAddress = true
            broadcastAddresses().forEach { address ->
                runCatching {
                    socket.send(DatagramPacket(request, request.size, address, discoveryPort))
                }
            }
            FifLog.network("event" to "lan_discovery_sent", "port" to discoveryPort)

            val deadline = System.nanoTime() + timeoutMs * 1_000_000L
            val buffer = ByteArray(FifProtocol.DISCOVERY_PACKET_SIZE)
            while (System.nanoTime() < deadline) {
                val remainingMs = ((deadline - System.nanoTime()) / 1_000_000L)
                    .coerceAtLeast(1L)
                    .coerceAtMost(Int.MAX_VALUE.toLong())
                    .toInt()
                socket.soTimeout = remainingMs
                val response = DatagramPacket(buffer, buffer.size)
                try {
                    socket.receive(response)
                } catch (_: SocketTimeoutException) {
                    break
                }
                val decoded = runCatching {
                    FifProtocol.decodeDiscoveryResponse(
                        response.data.copyOfRange(response.offset, response.offset + response.length)
                    )
                }.getOrNull() ?: continue
                if (decoded.requestNonce != requestNonce) continue
                val host = response.address.hostAddress ?: continue
                FifLog.network(
                    "event" to "lan_discovery_found",
                    "host" to host,
                    "control_port" to decoded.controlPort,
                    "video_port" to decoded.videoPort
                )
                return Result(host, decoded.controlPort, decoded.videoPort)
            }
        }
        throw LanDiscoveryException("局域网内未发现 FifScreen 电脑")
    }

    private fun broadcastAddresses(): Set<InetAddress> {
        val addresses = linkedSetOf<InetAddress>()
        addresses += InetAddress.getByName("255.255.255.255")
        val interfaces = NetworkInterface.getNetworkInterfaces()?.toList().orEmpty()
        interfaces.filter { runCatching { it.isUp && !it.isLoopback }.getOrDefault(false) }
            .flatMap { it.interfaceAddresses }
            .filter { it.address is Inet4Address && it.broadcast != null }
            .mapTo(addresses) { it.broadcast }
        return addresses
    }
}

class LanDiscoveryException(message: String) : Exception(message)
