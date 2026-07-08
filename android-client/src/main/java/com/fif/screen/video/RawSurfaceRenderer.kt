package com.fif.screen.video

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Rect
import android.view.Surface
import com.fif.screen.diagnostics.FifLog
import java.nio.ByteBuffer

class RawSurfaceRenderer(
    private val surface: Surface,
    private val width: Int,
    private val height: Int
) {
    data class Stats(
        val submittedFrames: Long,
        val renderedFrames: Long,
        val droppedFrames: Long
    )

    private val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
    private var submittedFrames = 0L
    private var renderedFrames = 0L
    private var droppedFrames = 0L

    fun render(payload: ByteArray) {
        submittedFrames += 1
        if (payload.size != width * height * 4 || !surface.isValid) {
            droppedFrames += 1
            return
        }

        bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(payload))
        var canvas: Canvas? = null
        try {
            canvas = surface.lockCanvas(null)
            val destination = Rect(0, 0, canvas.width, canvas.height)
            canvas.drawBitmap(bitmap, null, destination, null)
            renderedFrames += 1
        } catch (e: Exception) {
            droppedFrames += 1
            FifLog.decoder("event" to "raw_render_error", "message" to (e.message ?: "unknown"))
        } finally {
            if (canvas != null) {
                surface.unlockCanvasAndPost(canvas)
            }
        }
    }

    fun stats(): Stats = Stats(submittedFrames, renderedFrames, droppedFrames)
}
