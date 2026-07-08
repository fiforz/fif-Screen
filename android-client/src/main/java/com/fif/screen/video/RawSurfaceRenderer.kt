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
    private val height: Int,
    private val pixelFormat: String
) {
    data class Stats(
        val submittedFrames: Long,
        val renderedFrames: Long,
        val droppedFrames: Long
    )

    private val bytesPerPixel = if (pixelFormat == "raw-rgb565") 2 else 4
    private val bitmap = Bitmap.createBitmap(
        width,
        height,
        if (pixelFormat == "raw-rgb565") Bitmap.Config.RGB_565 else Bitmap.Config.ARGB_8888
    )
    private var submittedFrames = 0L
    private var renderedFrames = 0L
    private var droppedFrames = 0L

    fun render(payload: ByteArray) {
        submittedFrames += 1
        if (payload.size != width * height * bytesPerPixel || !surface.isValid) {
            droppedFrames += 1
            return
        }

        bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(payload))
        var canvas: Canvas? = null
        try {
            canvas = surface.lockCanvas(null)
            val destination = fitRect(canvas.width, canvas.height)
            canvas.drawRGB(0, 0, 0)
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

    private fun fitRect(canvasWidth: Int, canvasHeight: Int): Rect {
        val sourceAspect = width.toFloat() / height.toFloat()
        val canvasAspect = canvasWidth.toFloat() / canvasHeight.toFloat()
        return if (canvasAspect > sourceAspect) {
            val drawWidth = (canvasHeight * sourceAspect).toInt()
            val left = (canvasWidth - drawWidth) / 2
            Rect(left, 0, left + drawWidth, canvasHeight)
        } else {
            val drawHeight = (canvasWidth / sourceAspect).toInt()
            val top = (canvasHeight - drawHeight) / 2
            Rect(0, top, canvasWidth, top + drawHeight)
        }
    }
}
