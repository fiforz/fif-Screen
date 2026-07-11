package com.fif.screen.input

import android.view.MotionEvent
import com.fif.screen.protocol.FifProtocol
import kotlin.math.roundToInt

object TouchInputMapper {
    fun fromMotionEvent(
        event: MotionEvent,
        viewWidth: Int,
        viewHeight: Int
    ): FifProtocol.TouchFrame? {
        if (viewWidth <= 1 || viewHeight <= 1 ||
            event.pointerCount <= 0 || event.pointerCount > FifProtocol.MAX_TOUCH_CONTACTS
        ) {
            return null
        }

        val action = event.actionMasked
        if (action !in SUPPORTED_ACTIONS) {
            return null
        }
        val changedIndex = event.actionIndex
        val contacts = (0 until event.pointerCount).map { index ->
            val phase = when (action) {
                MotionEvent.ACTION_DOWN -> FifProtocol.TouchPhase.DOWN
                MotionEvent.ACTION_POINTER_DOWN -> if (index == changedIndex) {
                    FifProtocol.TouchPhase.DOWN
                } else {
                    FifProtocol.TouchPhase.MOVE
                }
                MotionEvent.ACTION_MOVE -> FifProtocol.TouchPhase.MOVE
                MotionEvent.ACTION_UP -> FifProtocol.TouchPhase.UP
                MotionEvent.ACTION_POINTER_UP -> if (index == changedIndex) {
                    FifProtocol.TouchPhase.UP
                } else {
                    FifProtocol.TouchPhase.MOVE
                }
                else -> FifProtocol.TouchPhase.CANCEL
            }
            FifProtocol.TouchContact(
                pointerId = event.getPointerId(index) + 1,
                phase = phase,
                x = normalizeTouchAxis(event.getX(index), viewWidth),
                y = normalizeTouchAxis(event.getY(index), viewHeight),
                pressure = (event.getPressure(index).coerceIn(0f, 1f) *
                    FifProtocol.MAX_TOUCH_PRESSURE).roundToInt(),
                major = normalizeTouchSize(event.getTouchMajor(index), viewWidth),
                minor = normalizeTouchSize(event.getTouchMinor(index), viewHeight)
            )
        }
        return FifProtocol.TouchFrame(contacts)
    }

    private val SUPPORTED_ACTIONS = setOf(
        MotionEvent.ACTION_DOWN,
        MotionEvent.ACTION_POINTER_DOWN,
        MotionEvent.ACTION_MOVE,
        MotionEvent.ACTION_POINTER_UP,
        MotionEvent.ACTION_UP,
        MotionEvent.ACTION_CANCEL
    )
}

internal fun normalizeTouchAxis(value: Float, extent: Int): Int {
    if (extent <= 1) return 0
    val maximum = (extent - 1).toFloat()
    return (value.coerceIn(0f, maximum) * 65535f / maximum).roundToInt()
}

internal fun normalizeTouchSize(value: Float, extent: Int): Int {
    if (extent <= 0) return 0
    return (value.coerceIn(0f, extent.toFloat()) * 65535f / extent).roundToInt()
}
