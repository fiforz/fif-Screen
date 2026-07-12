package com.fif.screen.net

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
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
    context: Context,
    private val discoveryPort: Int = FifProtocol.DISCOVERY_PORT,
    private val timeoutMs: Int = 2600
) {
    data class Result(
        val host: String,
        val controlPort: Int,
        val videoPort: Int,
        val network: Network?
    )

    private val connectivityManager =
        context.applicationContext.getSystemService(ConnectivityManager::class.java)

    fun discover(): Result {
        val requestNonce = SecureRandom().nextInt()
        val request = FifProtocol.encodeDiscoveryRequest(requestNonce)
        val candidates = createCandidates()
        var successfulSends = 0
        var nextSendNs = 0L
        val deadlineNs = System.nanoTime() + timeoutMs * 1_000_000L

        try {
            while (System.nanoTime() < deadlineNs) {
                val nowNs = System.nanoTime()
                if (nowNs >= nextSendNs) {
                    successfulSends += sendDiscoveryRound(candidates, request)
                    nextSendNs = nowNs + DISCOVERY_RETRY_MS * 1_000_000L
                }

                for (candidate in candidates) {
                    val remainingMs = ((deadlineNs - System.nanoTime()) / 1_000_000L)
                        .coerceAtLeast(1L)
                        .coerceAtMost(RECEIVE_SLICE_MS.toLong())
                        .toInt()
                    candidate.socket.soTimeout = remainingMs
                    val response = DatagramPacket(
                        ByteArray(FifProtocol.DISCOVERY_PACKET_SIZE),
                        FifProtocol.DISCOVERY_PACKET_SIZE
                    )
                    try {
                        candidate.socket.receive(response)
                    } catch (_: SocketTimeoutException) {
                        continue
                    }
                    val decoded = runCatching {
                        FifProtocol.decodeDiscoveryResponse(
                            response.data.copyOfRange(
                                response.offset,
                                response.offset + response.length
                            )
                        )
                    }.getOrNull() ?: continue
                    if (decoded.requestNonce != requestNonce) continue
                    val host = response.address.hostAddress ?: continue
                    FifLog.network(
                        "event" to "lan_discovery_found",
                        "host" to host,
                        "control_port" to decoded.controlPort,
                        "video_port" to decoded.videoPort,
                        "network" to candidate.name
                    )
                    return Result(
                        host,
                        decoded.controlPort,
                        decoded.videoPort,
                        candidate.network
                    )
                }
            }
        } finally {
            candidates.forEach { it.socket.close() }
        }

        if (successfulSends == 0) {
            throw LanDiscoveryException("无法访问局域网，请允许附近设备权限并检查 Wi-Fi")
        }
        throw LanDiscoveryException("局域网内未发现 FifScreen 电脑，可改用手动电脑 IP")
    }

    fun direct(host: String): Result {
        val normalized = LanHostAddress.normalize(host)
            ?: throw LanDiscoveryException("请输入有效的电脑 IPv4 地址")
        val address = InetAddress.getByName(normalized)
        val network = localNetworks().firstOrNull { candidate ->
            runCatching {
                connectivityManager.getLinkProperties(candidate)?.routes?.any { it.matches(address) } == true
            }.getOrDefault(false)
        } ?: localNetworks().firstOrNull()
        FifLog.network(
            "event" to "lan_manual_host_selected",
            "host" to normalized,
            "network" to (network?.toString() ?: "default")
        )
        return Result(
            normalized,
            DEFAULT_CONTROL_PORT,
            DEFAULT_VIDEO_PORT,
            network
        )
    }

    private fun createCandidates(): List<Candidate> {
        val candidates = mutableListOf<Candidate>()
        localNetworks().forEach { network ->
            val interfaceName = connectivityManager.getLinkProperties(network)?.interfaceName
            runCatching {
                val socket = newSocket()
                try {
                    network.bindSocket(socket)
                    candidates += Candidate(
                        network = network,
                        name = interfaceName ?: network.toString(),
                        socket = socket,
                        broadcasts = broadcastAddresses(interfaceName)
                    )
                } catch (error: Exception) {
                    socket.close()
                    throw error
                }
            }.onFailure {
                FifLog.network(
                    "event" to "lan_discovery_network_skipped",
                    "network" to (interfaceName ?: network.toString()),
                    "error" to it.javaClass.simpleName
                )
            }
        }

        candidates += Candidate(
            network = null,
            name = "default",
            socket = newSocket(),
            broadcasts = broadcastAddresses(null)
        )
        FifLog.network(
            "event" to "lan_discovery_ready",
            "candidates" to candidates.joinToString(",") { it.name }
        )
        return candidates
    }

    private fun sendDiscoveryRound(candidates: List<Candidate>, request: ByteArray): Int {
        var sent = 0
        candidates.forEach { candidate ->
            candidate.broadcasts.forEach { address ->
                runCatching {
                    candidate.socket.send(
                        DatagramPacket(request, request.size, address, discoveryPort)
                    )
                    sent += 1
                }.onFailure {
                    FifLog.network(
                        "event" to "lan_discovery_send_failed",
                        "network" to candidate.name,
                        "address" to address.hostAddress,
                        "error" to it.javaClass.simpleName
                    )
                }
            }
        }
        FifLog.network(
            "event" to "lan_discovery_sent",
            "port" to discoveryPort,
            "datagrams" to sent
        )
        return sent
    }

    private fun localNetworks(): List<Network> =
        connectivityManager.allNetworks.filter { network ->
            val capabilities = connectivityManager.getNetworkCapabilities(network) ?: return@filter false
            capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) ||
                capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)
        }

    private fun newSocket(): DatagramSocket = DatagramSocket().apply {
        broadcast = true
        reuseAddress = true
    }

    private fun broadcastAddresses(interfaceName: String?): Set<InetAddress> {
        val addresses = linkedSetOf<InetAddress>()
        val interfaces = NetworkInterface.getNetworkInterfaces()?.toList().orEmpty()
        interfaces.asSequence()
            .filter { interfaceName == null || it.name == interfaceName }
            .filter { runCatching { it.isUp && !it.isLoopback }.getOrDefault(false) }
            .flatMap { it.interfaceAddresses.asSequence() }
            .filter { it.address is Inet4Address && it.broadcast != null }
            .mapTo(addresses) { it.broadcast }
        addresses += InetAddress.getByName("255.255.255.255")
        return addresses
    }

    private data class Candidate(
        val network: Network?,
        val name: String,
        val socket: DatagramSocket,
        val broadcasts: Set<InetAddress>
    )

    private companion object {
        const val DEFAULT_CONTROL_PORT = 27183
        const val DEFAULT_VIDEO_PORT = 27184
        const val DISCOVERY_RETRY_MS = 650L
        const val RECEIVE_SLICE_MS = 90
    }
}

object LanHostAddress {
    fun normalize(value: String?): String? {
        val trimmed = value?.trim().orEmpty()
        val octets = trimmed.split('.')
        if (octets.size != 4 || octets.any { part ->
                part.isEmpty() || part.length > 3 || part.any { it !in '0'..'9' }
            }
        ) {
            return null
        }
        val values = octets.map { it.toInt() }
        if (values.any { it !in 0..255 }) return null
        if (values[0] == 0 || values[0] == 127 || values[0] >= 224) return null
        if (values.all { it == 255 }) return null
        return values.joinToString(".")
    }
}

class LanDiscoveryException(message: String) : Exception(message)
