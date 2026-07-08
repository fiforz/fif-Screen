package com.fif.screen.video

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Rect
import android.view.Surface
import com.fif.screen.diagnostics.FifLog
import java.nio.ByteBuffer
import java.nio.ByteOrder

class DirtyRawSurfaceRenderer(
    private val surface: Surface,
    private val width: Int,
    private val height: Int
) {
    data class Stats(
        val submittedFrames: Long,
        val renderedFrames: Long,
        val droppedFrames: Long,
        val dirtyRects: Long,
        val fullFrames: Long
    )

    private val bytesPerPixel = 2
    private val frameBuffer = ByteArray(width * height * bytesPerPixel)
    private val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.RGB_565)
    private var submittedFrames = 0L
    private var renderedFrames = 0L
    private var droppedFrames = 0L
    private var dirtyRects = 0L
    private var fullFrames = 0L

    fun render(payload: ByteArray) {
        submittedFrames += 1
        if (payload.size < HEADER_SIZE || !surface.isValid) {
            droppedFrames += 1
            return
        }

        try {
            val buffer = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
            val magic = ByteArray(4)
            buffer.get(magic)
            if (!magic.contentEquals(MAGIC)) {
                droppedFrames += 1
                return
            }

            buffer.short.toInt() and 0xffff // tile size
            val flags = buffer.short.toInt() and 0xffff
            val rectCount = buffer.int
            if (rectCount < 0) {
                droppedFrames += 1
                return
            }

            var changed = false
            repeat(rectCount) {
                if (buffer.remaining() < RECT_HEADER_SIZE) {
                    throw IllegalArgumentException("truncated dirty rect header")
                }
                val x = buffer.short.toInt() and 0xffff
                val y = buffer.short.toInt() and 0xffff
                val rectWidth = buffer.short.toInt() and 0xffff
                val rectHeight = buffer.short.toInt() and 0xffff
                if (x < 0 || y < 0 || rectWidth <= 0 || rectHeight <= 0 ||
                    x + rectWidth > width || y + rectHeight > height
                ) {
                    throw IllegalArgumentException("dirty rect out of bounds")
                }

                val rowBytes = rectWidth * bytesPerPixel
                val rectBytes = rowBytes * rectHeight
                if (buffer.remaining() < rectBytes) {
                    throw IllegalArgumentException("truncated dirty rect pixels")
                }

                for (row in 0 until rectHeight) {
                    val dst = ((y + row) * width + x) * bytesPerPixel
                    buffer.get(frameBuffer, dst, rowBytes)
                }
                dirtyRects += 1
                changed = true
            }

            if ((flags and FLAG_FULL_FRAME) != 0) {
                fullFrames += 1
            }
            if (changed) {
                drawFrame()
            }
        } catch (e: Exception) {
            droppedFrames += 1
            FifLog.decoder("event" to "dirty_raw_render_error", "message" to (e.message ?: "unknown"))
        }
    }

    fun stats(): Stats = Stats(submittedFrames, renderedFrames, droppedFrames, dirtyRects, fullFrames)

    private fun drawFrame() {
        bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(frameBuffer))
        var canvas: Canvas? = null
        try {
            canvas = surface.lockCanvas(null)
            val destination = fitRect(canvas.width, canvas.height)
            canvas.drawRGB(0, 0, 0)
            canvas.drawBitmap(bitmap, null, destination, null)
            renderedFrames += 1
        } finally {
            if (canvas != null) {
                surface.unlockCanvasAndPost(canvas)
            }
        }
    }

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

    private companion object {
        val MAGIC = byteArrayOf('F'.code.toByte(), 'D'.code.toByte(), 'R'.code.toByte(), '1'.code.toByte())
        const val HEADER_SIZE = 12
        const val RECT_HEADER_SIZE = 8
        const val FLAG_FULL_FRAME = 1
    }
}
