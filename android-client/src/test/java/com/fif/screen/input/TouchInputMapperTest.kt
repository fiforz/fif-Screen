package com.fif.screen.input

import org.junit.Assert.assertEquals
import org.junit.Test

class TouchInputMapperTest {
    @Test
    fun axisNormalizationClampsToViewBounds() {
        assertEquals(0, normalizeTouchAxis(-10f, 1920))
        assertEquals(0, normalizeTouchAxis(0f, 1920))
        assertEquals(65535, normalizeTouchAxis(1919f, 1920))
        assertEquals(65535, normalizeTouchAxis(5000f, 1920))
    }

    @Test
    fun contactSizeNormalizationUsesFullExtent() {
        assertEquals(0, normalizeTouchSize(0f, 1080))
        assertEquals(32768, normalizeTouchSize(540f, 1080))
        assertEquals(65535, normalizeTouchSize(1080f, 1080))
    }
}
