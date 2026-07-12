package com.fif.screen.net

import android.net.Network

enum class ConnectionMode(val preferenceValue: String) {
    USB("usb"),
    LAN("lan"),
    RELAY("relay");

    companion object {
        fun fromPreference(value: String?): ConnectionMode =
            entries.firstOrNull { it.preferenceValue == value } ?: USB
    }
}

data class ConnectionEndpoint(
    val host: String,
    val controlPort: Int,
    val videoPort: Int,
    val mode: ConnectionMode,
    val pairingPin: String? = null,
    val network: Network? = null
)

fun interface EndpointProvider {
    fun resolve(): ConnectionEndpoint
}

class UsbEndpointProvider(
    private val controlPort: Int = 27183,
    private val videoPort: Int = 27184
) : EndpointProvider {
    override fun resolve(): ConnectionEndpoint = ConnectionEndpoint(
        host = "127.0.0.1",
        controlPort = controlPort,
        videoPort = videoPort,
        mode = ConnectionMode.USB
    )
}

class LanEndpointProvider(
    private val pin: String,
    private val manualHost: String?,
    private val discovery: LanDiscovery
) : EndpointProvider {
    init {
        require(PairingCrypto.isValidPin(pin)) { "PIN 必须是四位数字" }
    }

    override fun resolve(): ConnectionEndpoint {
        val discovered = if (manualHost.isNullOrBlank()) {
            discovery.discover()
        } else {
            discovery.direct(manualHost)
        }
        return ConnectionEndpoint(
            host = discovered.host,
            controlPort = discovered.controlPort,
            videoPort = discovered.videoPort,
            mode = ConnectionMode.LAN,
            pairingPin = pin,
            network = discovered.network
        )
    }
}

interface RelayEndpointDirectory {
    fun resolve(serverUrl: String, deviceId: String): ConnectionEndpoint
}

class RelayEndpointProvider(
    private val serverUrl: String,
    private val deviceId: String,
    private val directory: RelayEndpointDirectory
) : EndpointProvider {
    override fun resolve(): ConnectionEndpoint = directory.resolve(serverUrl, deviceId)
}
