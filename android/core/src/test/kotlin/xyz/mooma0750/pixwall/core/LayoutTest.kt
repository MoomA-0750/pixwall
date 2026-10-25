package xyz.mooma0750.pixwall.core

import org.junit.Assert.assertEquals
import org.junit.Test

/** Mac の LayoutTests・TimingTests と同じ値。Pixel 3 の画面（1080×2160）も見る */
class LayoutTest {
    @Test
    fun integerPicksLargestWholeScale() {
        assertEquals(PixelRect(544, 4, 1792, 1792), layout(64, 64, 2880, 1800, Fit.INTEGER))
        // Pixel 3 の縦の画面に 64×64 は 16 倍
        assertEquals(PixelRect(28, 568, 1024, 1024), layout(64, 64, 1080, 2160, Fit.INTEGER))
    }

    @Test
    fun integerFallsBackToFitWhenImageIsLarger() {
        assertEquals(PixelRect(0, 500, 2000, 1000), layout(4000, 2000, 2000, 2000, Fit.INTEGER))
    }

    @Test
    fun fitKeepsWholeImageOnScreen() {
        assertEquals(PixelRect(0, 90, 2880, 1620), layout(320, 180, 2880, 1800, Fit.FIT))
    }

    @Test
    fun fillCoversScreen() {
        assertEquals(PixelRect(-160, 0, 3200, 1800), layout(320, 180, 2880, 1800, Fit.FILL))
    }

    @Test
    fun fitCanBeMovedInsideTheMargin() {
        assertEquals(PixelRect(0, 0, 2880, 1620), layout(320, 180, 2880, 1800, Fit.FIT, 0.0))
        assertEquals(PixelRect(0, 180, 2880, 1620), layout(320, 180, 2880, 1800, Fit.FIT, 1.0))
        assertEquals(PixelRect(500, 0, 500, 1000), layout(100, 200, 1000, 1000, Fit.FIT, 1.0))
        // 整数倍では中央のまま
        assertEquals(PixelRect(544, 4, 1792, 1792), layout(64, 64, 2880, 1800, Fit.INTEGER, 0.0))
    }

    @Test
    fun fillCanBeMovedInsideTheOverflow() {
        assertEquals(PixelRect(0, 0, 3200, 1800), layout(320, 180, 2880, 1800, Fit.FILL, 0.0))
        assertEquals(PixelRect(-320, 0, 3200, 1800), layout(320, 180, 2880, 1800, Fit.FILL, 1.0))
        assertEquals(PixelRect(0, -250, 1000, 2000), layout(100, 200, 1000, 1000, Fit.FILL, 0.25))
        assertEquals(PixelRect(-2, 0, 9, 3), layout(3, 1, 4, 3, Fit.FILL))
    }

    // 自由に置く: 画面に合わせた大きさ×freeSize。横・縦は別々に、余白（はみ出し）の中の割合で置く
    @Test
    fun freeSizeAndPosition() {
        fun free(x: Double, y: Double, size: Double) =
            layout(320, 180, 2880, 1800, Placement(Fit.FREE, freeX = x, freeY = y, freeSize = size))
        assertEquals(PixelRect(0, 90, 2880, 1620), free(0.5, 0.5, 1.0))
        assertEquals(PixelRect(0, 990, 1440, 810), free(0.0, 1.0, 0.5))
        assertEquals(PixelRect(1440, 0, 1440, 810), free(1.0, 0.0, 0.5))
        assertEquals(PixelRect(0, 0, 5760, 3240), free(0.0, 0.0, 2.0))
        assertEquals(PixelRect(-2880, -1440, 5760, 3240), free(1.0, 1.0, 2.0))
        assertEquals(free(0.0, 1.0, 3.0), free(-1.0, 2.0, 100.0))
        assertEquals(free(0.5, 0.5, 0.1), free(0.5, 0.5, 0.0))
        // position はほかの並べ方の値で、自由に置くときは使わない
        assertEquals(PixelRect(720, 495, 1440, 810), layout(320, 180, 2880, 1800, Placement(Fit.FREE, position = 0.0, freeSize = 0.5)))
    }

    @Test
    fun shortDelaysBecome100ms() {
        assertEquals(100L, frameDelayMillis(0))
        assertEquals(100L, frameDelayMillis(10))
        assertEquals(20L, frameDelayMillis(20))
    }

    @Test
    fun speedIsClamped() {
        assertEquals(50L, frameDelayMillis(100, 2.0))
        assertEquals(25L, frameDelayMillis(100, 100.0))
        assertEquals(400L, frameDelayMillis(100, 0.0))
    }
}
