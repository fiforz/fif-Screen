package com.fif.screen

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputFilter
import android.text.InputType
import android.view.Gravity
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.Window
import android.view.WindowManager
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.TextView
import android.widget.Toast
import com.fif.screen.diagnostics.FifLog
import com.fif.screen.diagnostics.H264CapabilityLogger
import com.fif.screen.input.TouchInputMapper
import com.fif.screen.net.ConnectionMode
import com.fif.screen.net.EndpointProvider
import com.fif.screen.net.LanDiscovery
import com.fif.screen.net.LanEndpointProvider
import com.fif.screen.net.LanHostAddress
import com.fif.screen.net.StreamClient
import com.fif.screen.net.UsbEndpointProvider
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

class MainActivity : Activity(), SurfaceHolder.Callback {
    private lateinit var surfaceView: SurfaceView
    private lateinit var statusView: TextView
    private lateinit var statsView: TextView
    private lateinit var toggleButton: Button
    private lateinit var settingsButton: ImageButton

    private var executor: ExecutorService? = null
    private var client: StreamClient? = null
    private var surfaceReady = false
    private var desiredRunning = true
    private var connectionMode = ConnectionMode.USB
    private var pairingPin: String? = null
    private var useManualLanHost = false
    private var manualLanHost: String? = null
    private var clientGeneration = 0L
    private var connectionDialog: AlertDialog? = null
    private var lastErrorToastNs = 0L
    private val mainHandler = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        FifLog.device(
            "event" to "activity_on_create",
            "manufacturer" to Build.MANUFACTURER,
            "model" to Build.MODEL,
            "product" to Build.PRODUCT,
            "android" to Build.VERSION.RELEASE,
            "api" to Build.VERSION.SDK_INT,
            "abi" to Build.SUPPORTED_ABIS.joinToString(",")
        )
        FifLog.display(
            "event" to "activity_on_create",
            "refresh_rate" to displayRefreshRate()
        )
        FifLog.network(
            "event" to "defaults",
            "transport" to "usb",
            "control_port" to 27183,
            "video_port" to 27184
        )
        val preferences = getPreferences(Context.MODE_PRIVATE)
        connectionMode = ConnectionMode.fromPreference(
            preferences.getString(PREF_CONNECTION_MODE, null)
        )
        requestedConnectionMode(intent)?.let { requestedMode ->
            connectionMode = requestedMode
            preferences.edit()
                .putString(PREF_CONNECTION_MODE, requestedMode.preferenceValue)
                .apply()
            FifLog.network(
                "event" to "transport_requested",
                "transport" to requestedMode.preferenceValue,
                "source" to "launch_intent"
            )
        }
        useManualLanHost = preferences.getBoolean(PREF_LAN_MANUAL_HOST, false)
        manualLanHost = preferences.getString(PREF_LAN_HOST, null)
        if (connectionMode == ConnectionMode.LAN) {
            desiredRunning = false
        }
        H264CapabilityLogger.logAvailableDecoders()

        requestWindowFeature(Window.FEATURE_NO_TITLE)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        window.decorView.systemUiVisibility =
            View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY

        surfaceView = SurfaceView(this)
        surfaceView.holder.addCallback(this)
        surfaceView.isClickable = true
        surfaceView.setOnTouchListener { view, event ->
            val frame = TouchInputMapper.fromMotionEvent(event, view.width, view.height)
            if (frame == null) {
                false
            } else {
                val sent = client?.sendTouchFrame(frame) == true
                if (!frame.isMoveOnly || !sent) {
                    FifLog.input(
                        "event" to "touch_event",
                        "action" to event.actionMasked,
                        "contacts" to frame.contacts.size,
                        "sent" to sent,
                        "view_width" to view.width,
                        "view_height" to view.height
                    )
                }
                true
            }
        }

        statusView = TextView(this).apply {
            setTextColor(Color.WHITE)
            textSize = 14f
            text = "Disconnected"
        }

        statsView = TextView(this).apply {
            setTextColor(Color.WHITE)
            textSize = 12f
            text = "1280x720  FPS --  bitrate --  decoder --  dropped 0"
        }

        toggleButton = Button(this).apply {
            text = "Start"
            setOnClickListener { toggleClient() }
        }

        val overlay = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(0x66000000)
            setPadding(16, 12, 16, 12)
            visibility = View.GONE
            addView(statusView)
            addView(statsView)
            addView(toggleButton)
        }

        val root = FrameLayout(this)
        root.addView(
            surfaceView,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
        )
        root.addView(
            overlay,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT
            )
        )
        settingsButton = ImageButton(this).apply {
            setImageResource(android.R.drawable.ic_menu_preferences)
            setColorFilter(Color.WHITE)
            contentDescription = "连接设置"
            setPadding(dp(10), dp(10), dp(10), dp(10))
            background = GradientDrawable().apply {
                setColor(0x88000000.toInt())
                cornerRadius = dp(6).toFloat()
            }
            setOnClickListener { showConnectionDialog() }
        }
        root.addView(
            settingsButton,
            FrameLayout.LayoutParams(dp(48), dp(48), Gravity.TOP or Gravity.END).apply {
                topMargin = dp(12)
                marginEnd = dp(12)
            }
        )
        setContentView(root)
        showSettingsButton()
    }

    override fun onNewIntent(intent: Intent?) {
        super.onNewIntent(intent)
        setIntent(intent)
        val requestedMode = requestedConnectionMode(intent) ?: return
        connectionDialog?.dismiss()
        if (requestedMode == ConnectionMode.USB) {
            applyConnectionSettings(
                mode = ConnectionMode.USB,
                pin = null,
                manualHostEnabled = false,
                host = null
            )
        } else {
            stopClient()
            connectionMode = ConnectionMode.LAN
            pairingPin = null
            desiredRunning = false
            getPreferences(Context.MODE_PRIVATE).edit()
                .putString(PREF_CONNECTION_MODE, ConnectionMode.LAN.preferenceValue)
                .apply()
            FifLog.network(
                "event" to "transport_requested",
                "transport" to ConnectionMode.LAN.preferenceValue,
                "source" to "launch_intent"
            )
            showSettingsButton()
            if (surfaceReady) {
                surfaceView.post { showConnectionDialog() }
            }
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        surfaceReady = true
        FifLog.surface("event" to "created", "valid" to holder.surface.isValid)
        maybeAutoStart()
        if (connectionMode == ConnectionMode.LAN && pairingPin == null) {
            surfaceView.post { showConnectionDialog() }
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        surfaceReady = true
        FifLog.surface(
            "event" to "changed",
            "format" to format,
            "width" to width,
            "height" to height,
            "valid" to holder.surface.isValid
        )
        maybeAutoStart()
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        FifLog.surface("event" to "destroyed")
        stopClient()
        surfaceReady = false
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != REQUEST_LOCAL_NETWORK_PERMISSION) return
        if (grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) {
            FifLog.network("event" to "local_network_permission_granted")
            maybeAutoStart()
        } else {
            desiredRunning = false
            FifLog.network("event" to "local_network_permission_denied")
            showSettingsButton()
            showConnectionError("无线连接需要“附近设备”权限")
        }
    }

    override fun onDestroy() {
        FifLog.device("event" to "activity_on_destroy")
        stopClient()
        super.onDestroy()
    }

    @Deprecated("Handled explicitly to keep the full-screen stream active")
    override fun onBackPressed() {
        showConnectionDialog()
    }

    private fun toggleClient() {
        if (client == null) {
            desiredRunning = true
            startClient()
        } else {
            desiredRunning = false
            stopClient()
        }
    }

    private fun maybeAutoStart() {
        if (desiredRunning && surfaceReady && client == null && executor == null) {
            surfaceView.post { startClient() }
        }
    }

    private fun startClient() {
        if (client != null || executor != null) {
            return
        }
        if (!surfaceReady) {
            statusView.text = "Surface not ready"
            FifLog.surface("event" to "start_rejected", "reason" to "surface_not_ready")
            return
        }
        val endpointProvider = createEndpointProvider() ?: run {
            desiredRunning = false
            showSettingsButton()
            showConnectionDialog()
            return
        }

        FifLog.network("event" to "client_start_requested")
        toggleButton.text = "Stop"
        statusView.text = "Connecting"
        val service = Executors.newSingleThreadExecutor()
        executor = service
        val generation = ++clientGeneration
        lateinit var streamClient: StreamClient
        streamClient = StreamClient(
            surface = surfaceView.holder.surface,
            endpointProvider = endpointProvider,
            listener = object : StreamClient.Listener {
                override fun onStatus(text: String) {
                    runOnUiThread {
                        if (generation != clientGeneration) return@runOnUiThread
                        statusView.text = text
                        when {
                            text.startsWith("Streaming") -> hideSettingsButtonDelayed()
                            text.startsWith("Error:") -> {
                                showSettingsButton()
                                showConnectionError(text.removePrefix("Error:").trim())
                            }
                            text.startsWith("Retry:") -> showSettingsButton()
                        }
                    }
                }

                override fun onStats(text: String) {
                    runOnUiThread {
                        if (generation == clientGeneration) {
                            statsView.text = text
                        }
                    }
                }

                override fun onStopped() {
                    runOnUiThread {
                        if (generation != clientGeneration || client !== streamClient) {
                            service.shutdown()
                            return@runOnUiThread
                        }
                        client = null
                        executor = null
                        toggleButton.text = "Start"
                        statusView.text = "Disconnected"
                        showSettingsButton()
                        if (desiredRunning && surfaceReady) {
                            statusView.text = "Reconnecting"
                            val delay = if (connectionMode == ConnectionMode.LAN) {
                                LAN_RECONNECT_DELAY_MS
                            } else {
                                USB_RECONNECT_DELAY_MS
                            }
                            mainHandler.postDelayed({ maybeAutoStart() }, delay)
                        }
                    }
                    service.shutdown()
                }
            }
        )
        client = streamClient
        service.execute { streamClient.run() }
    }

    private fun stopClient() {
        FifLog.network("event" to "client_stop_requested")
        mainHandler.removeCallbacksAndMessages(null)
        clientGeneration += 1
        client?.stop()
        client = null
        executor?.shutdownNow()
        executor = null
        if (::toggleButton.isInitialized) {
            toggleButton.text = "Start"
            statusView.text = "Disconnected"
        }
    }

    private fun createEndpointProvider(): EndpointProvider? = when (connectionMode) {
        ConnectionMode.USB -> UsbEndpointProvider()
        ConnectionMode.LAN -> pairingPin?.let { pin ->
            LanEndpointProvider(
                pin = pin,
                manualHost = manualLanHost.takeIf { useManualLanHost },
                discovery = LanDiscovery(applicationContext)
            )
        }
        ConnectionMode.RELAY -> null
    }

    private fun showConnectionDialog() {
        if (isFinishing || connectionDialog?.isShowing == true) return
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(24), dp(8), dp(24), 0)
        }
        val modes = RadioGroup(this).apply {
            orientation = RadioGroup.VERTICAL
        }
        val usb = RadioButton(this).apply {
            id = View.generateViewId()
            text = "USB 调试连接"
        }
        val lan = RadioButton(this).apply {
            id = View.generateViewId()
            text = "无线局域网连接"
        }
        modes.addView(usb)
        modes.addView(lan)
        modes.check(if (connectionMode == ConnectionMode.LAN) lan.id else usb.id)

        val pinInput = EditText(this).apply {
            hint = "四位 PIN"
            inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_VARIATION_PASSWORD
            filters = arrayOf(InputFilter.LengthFilter(4))
            setText(pairingPin.orEmpty())
            visibility = if (connectionMode == ConnectionMode.LAN) View.VISIBLE else View.GONE
        }
        val lanHostModes = RadioGroup(this).apply {
            orientation = RadioGroup.HORIZONTAL
        }
        val autoHostOption = RadioButton(this).apply {
            id = View.generateViewId()
            text = "自动发现"
        }
        val manualHostOption = RadioButton(this).apply {
            id = View.generateViewId()
            text = "手动电脑 IP"
        }
        lanHostModes.addView(autoHostOption)
        lanHostModes.addView(manualHostOption)
        lanHostModes.check(if (useManualLanHost) manualHostOption.id else autoHostOption.id)
        val hostInput = EditText(this).apply {
            hint = "电脑 IPv4，例如 192.168.1.10"
            inputType = InputType.TYPE_CLASS_PHONE
            filters = arrayOf(InputFilter.LengthFilter(15))
            setText(manualLanHost.orEmpty())
        }
        fun updateLanFieldVisibility() {
            val lanSelected = modes.checkedRadioButtonId == lan.id
            pinInput.visibility = if (lanSelected) View.VISIBLE else View.GONE
            lanHostModes.visibility = if (lanSelected) View.VISIBLE else View.GONE
            hostInput.visibility = if (
                lanSelected && lanHostModes.checkedRadioButtonId == manualHostOption.id
            ) {
                View.VISIBLE
            } else {
                View.GONE
            }
        }
        modes.setOnCheckedChangeListener { _, _ -> updateLanFieldVisibility() }
        lanHostModes.setOnCheckedChangeListener { _, _ -> updateLanFieldVisibility() }
        updateLanFieldVisibility()
        content.addView(modes)
        content.addView(pinInput)
        content.addView(lanHostModes)
        content.addView(hostInput)

        val dialog = AlertDialog.Builder(this)
            .setTitle("连接设置")
            .setView(content)
            .setNegativeButton("取消", null)
            .setPositiveButton("连接", null)
            .create()
        connectionDialog = dialog
        dialog.setOnDismissListener { connectionDialog = null }
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val selectedMode = if (modes.checkedRadioButtonId == lan.id) {
                    ConnectionMode.LAN
                } else {
                    ConnectionMode.USB
                }
                val pin = pinInput.text.toString()
                if (selectedMode == ConnectionMode.LAN && !com.fif.screen.net.PairingCrypto.isValidPin(pin)) {
                    pinInput.error = "请输入四位数字 PIN"
                    return@setOnClickListener
                }
                val manualSelected = selectedMode == ConnectionMode.LAN &&
                    lanHostModes.checkedRadioButtonId == manualHostOption.id
                val normalizedHost = if (manualSelected) {
                    LanHostAddress.normalize(hostInput.text.toString())
                } else {
                    null
                }
                if (manualSelected && normalizedHost == null) {
                    hostInput.error = "请输入有效的电脑 IPv4 地址"
                    return@setOnClickListener
                }
                applyConnectionSettings(
                    mode = selectedMode,
                    pin = pin.takeIf { selectedMode == ConnectionMode.LAN },
                    manualHostEnabled = manualSelected,
                    host = normalizedHost
                )
                dialog.dismiss()
            }
        }
        dialog.show()
    }

    private fun applyConnectionSettings(
        mode: ConnectionMode,
        pin: String?,
        manualHostEnabled: Boolean,
        host: String?
    ) {
        stopClient()
        connectionMode = mode
        pairingPin = pin
        useManualLanHost = manualHostEnabled
        manualLanHost = host ?: manualLanHost
        getPreferences(Context.MODE_PRIVATE).edit()
            .putString(PREF_CONNECTION_MODE, mode.preferenceValue)
            .putBoolean(PREF_LAN_MANUAL_HOST, useManualLanHost)
            .putString(PREF_LAN_HOST, manualLanHost)
            .apply()
        desiredRunning = true
        FifLog.network(
            "event" to "transport_selected",
            "transport" to mode.preferenceValue,
            "lan_host_mode" to if (useManualLanHost) "manual" else "automatic"
        )
        if (mode != ConnectionMode.LAN || ensureLocalNetworkPermission()) {
            maybeAutoStart()
        }
    }

    private fun showSettingsButton() {
        if (::settingsButton.isInitialized) {
            mainHandler.removeCallbacks(hideSettingsButtonAction)
            settingsButton.visibility = View.VISIBLE
        }
    }

    private fun ensureLocalNetworkPermission(): Boolean {
        if (Build.VERSION.SDK_INT != 36) return true
        if (checkSelfPermission(LOCAL_NETWORK_PERMISSION) == PackageManager.PERMISSION_GRANTED) {
            return true
        }
        statusView.text = "Waiting for local network permission"
        requestPermissions(
            arrayOf(LOCAL_NETWORK_PERMISSION),
            REQUEST_LOCAL_NETWORK_PERMISSION
        )
        return false
    }

    private fun hideSettingsButtonDelayed() {
        mainHandler.removeCallbacks(hideSettingsButtonAction)
        mainHandler.postDelayed(hideSettingsButtonAction, SETTINGS_BUTTON_HIDE_DELAY_MS)
    }

    private val hideSettingsButtonAction = Runnable {
        if (::settingsButton.isInitialized && client != null) {
            settingsButton.visibility = View.GONE
        }
    }

    private fun showConnectionError(message: String) {
        val now = System.nanoTime()
        if (now - lastErrorToastNs < ERROR_TOAST_INTERVAL_NS) return
        lastErrorToastNs = now
        Toast.makeText(this, message.ifBlank { "连接失败" }, Toast.LENGTH_LONG).show()
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    private fun displayRefreshRate(): Float =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            display?.refreshRate ?: 0f
        } else {
            @Suppress("DEPRECATION")
            windowManager.defaultDisplay.refreshRate
        }

    private fun requestedConnectionMode(intent: Intent?): ConnectionMode? =
        when (intent?.getStringExtra(EXTRA_CONNECTION_MODE)?.lowercase()) {
            ConnectionMode.USB.preferenceValue -> ConnectionMode.USB
            ConnectionMode.LAN.preferenceValue -> ConnectionMode.LAN
            else -> null
        }

    private companion object {
        const val EXTRA_CONNECTION_MODE = "fifscreen_connection_mode"
        const val PREF_CONNECTION_MODE = "connection_mode"
        const val PREF_LAN_MANUAL_HOST = "lan_manual_host"
        const val PREF_LAN_HOST = "lan_host"
        const val LOCAL_NETWORK_PERMISSION = "android.permission.NEARBY_WIFI_DEVICES"
        const val REQUEST_LOCAL_NETWORK_PERMISSION = 1001
        const val USB_RECONNECT_DELAY_MS = 500L
        const val LAN_RECONNECT_DELAY_MS = 1800L
        const val SETTINGS_BUTTON_HIDE_DELAY_MS = 2200L
        const val ERROR_TOAST_INTERVAL_NS = 5_000_000_000L
    }
}
