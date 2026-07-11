package com.fif.screen

import android.app.Activity
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.Window
import android.view.WindowManager
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import com.fif.screen.diagnostics.FifLog
import com.fif.screen.diagnostics.H264CapabilityLogger
import com.fif.screen.input.TouchInputMapper
import com.fif.screen.net.StreamClient
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

class MainActivity : Activity(), SurfaceHolder.Callback {
    private lateinit var surfaceView: SurfaceView
    private lateinit var statusView: TextView
    private lateinit var statsView: TextView
    private lateinit var toggleButton: Button

    private var executor: ExecutorService? = null
    private var client: StreamClient? = null
    private var surfaceReady = false
    private var desiredRunning = true
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
            "control_host" to "127.0.0.1",
            "control_port" to 27183,
            "video_port" to 27184
        )
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
        setContentView(root)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        surfaceReady = true
        FifLog.surface("event" to "created", "valid" to holder.surface.isValid)
        maybeAutoStart()
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

    override fun onDestroy() {
        FifLog.device("event" to "activity_on_destroy")
        stopClient()
        super.onDestroy()
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

        FifLog.network("event" to "client_start_requested")
        toggleButton.text = "Stop"
        statusView.text = "Connecting"
        val service = Executors.newSingleThreadExecutor()
        executor = service
        client = StreamClient(
            surface = surfaceView.holder.surface,
            listener = object : StreamClient.Listener {
                override fun onStatus(text: String) {
                    runOnUiThread { statusView.text = text }
                }

                override fun onStats(text: String) {
                    runOnUiThread { statsView.text = text }
                }

                override fun onStopped() {
                    runOnUiThread {
                        client = null
                        executor = null
                        toggleButton.text = "Start"
                        statusView.text = "Disconnected"
                        if (desiredRunning && surfaceReady) {
                            statusView.text = "Reconnecting"
                            mainHandler.postDelayed({ maybeAutoStart() }, RECONNECT_DELAY_MS)
                        }
                    }
                    service.shutdown()
                }
            }
        )
        service.execute { client?.run() }
    }

    private fun stopClient() {
        FifLog.network("event" to "client_stop_requested")
        mainHandler.removeCallbacksAndMessages(null)
        client?.stop()
        client = null
        executor?.shutdownNow()
        executor = null
        if (::toggleButton.isInitialized) {
            toggleButton.text = "Start"
            statusView.text = "Disconnected"
        }
    }

    private fun displayRefreshRate(): Float =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            display?.refreshRate ?: 0f
        } else {
            @Suppress("DEPRECATION")
            windowManager.defaultDisplay.refreshRate
        }

    private companion object {
        const val RECONNECT_DELAY_MS = 500L
    }
}
