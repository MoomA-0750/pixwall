package xyz.mooma0750.pixwall.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class SwitchingTest {
    private fun slot(image: String, fit: Fit = Fit.INTEGER) = Slot(image, Placement(fit))

    @Test
    fun clock() {
        assertEquals(360, parseClock("06:00"))
        assertEquals(365, parseClock("6:05"))
        assertEquals(1439, parseClock("23:59"))
        assertNull(parseClock("24:00"))
        assertNull(parseClock("6:5"))
        assertNull(parseClock("a:00"))
        assertEquals("06:05", formatClock(365))
    }

    @Test
    fun time() {
        val c = Config(
            image = "/main.gif",
            switchBy = SwitchBy.TIME,
            // 始まる時刻の順に書かなくてよい
            timeSlots = listOf(TimeSlot(18 * 60, slot("/night.gif", Fit.FILL)), TimeSlot(6 * 60, slot("/day.gif"))),
        )
        assertEquals("/day.gif", c.activeSlot(6 * 60, false).image)
        assertEquals("/day.gif", c.activeSlot(17 * 60 + 59, false).image)
        assertEquals(slot("/night.gif", Fit.FILL), c.activeSlot(18 * 60, false))
        // 0 時から 6 時までは、前の日の 18 時から続く
        assertEquals("/night.gif", c.activeSlot(3 * 60, false).image)
        assertEquals(60, c.minutesToNextSwitch(5 * 60))
        assertEquals(12 * 60, c.minutesToNextSwitch(6 * 60))
        assertEquals(10 * 60, c.minutesToNextSwitch(20 * 60))
        val r = c.resolved(19 * 60, false)
        assertEquals("/night.gif", r.image)
        assertEquals(Fit.FILL, r.fit)
        assertEquals(c.timeSlots, r.timeSlots)
        assertNull(c.copy(timeSlots = c.timeSlots.take(1)).minutesToNextSwitch(0))
        assertNull(c.copy(switchBy = SwitchBy.NONE).minutesToNextSwitch(0))
        assertEquals("/main.gif", c.copy(switchBy = SwitchBy.NONE).activeSlot(0, true).image)
    }

    @Test
    fun appearance() {
        val c = Config(image = "/main.gif", switchBy = SwitchBy.APPEARANCE, darkSlot = slot("/dark.gif", Fit.FREE))
        assertEquals(slot("/dark.gif", Fit.FREE), c.activeSlot(0, true))
        assertEquals("/main.gif", c.activeSlot(0, false).image)
    }

    @Test
    fun switchToSeedsFromMain() {
        var c = Config(image = "/main.gif", fit = Fit.FILL).switchTo(SwitchBy.TIME)
        assertEquals(listOf(360, 1080), c.timeSlots.map { it.start })
        assertEquals(listOf(c.mainSlot, c.mainSlot), c.timeSlots.map { it.slot })
        c = c.withSlot(SlotRef.Time(0), slot("/day.gif")).switchTo(SwitchBy.NONE).switchTo(SwitchBy.TIME)
        // 一度決めた時刻は消さない
        assertEquals("/day.gif", c.timeSlots[0].slot.image)
        c = c.switchTo(SwitchBy.APPEARANCE)
        assertEquals(c.mainSlot, c.lightSlot)
        assertEquals(c.mainSlot, c.darkSlot)
        assertEquals(listOf("/main.gif", "/day.gif"), c.allImages)
        assertEquals("/main.gif", c.slot(SlotRef.Time(5)).image)
        assertEquals(c, c.withSlot(SlotRef.Time(5), slot("/x.gif")))
    }

    @Test
    fun json() {
        val c = Config(
            image = "content://x/main.gif",
            switchBy = SwitchBy.TIME,
            timeSlots = listOf(TimeSlot(6 * 60 + 30, Slot("content://x/day.gif", Placement(Fit.FREE, freeSize = 2.0)))),
            darkSlot = slot("content://x/dark.gif"),
        )
        assertEquals(c, Config.parse(c.toJson()))
        // Mac・Windows が書いた形を読める
        val mac = Config.parse(
            """
            {"image": "/main.gif", "fit": "fill", "switchBy": "appearance",
             "darkSlot": {"fit": "free", "freeSize": 0.4, "freeX": 0, "freeY": 1, "image": "/dark.gif", "position": 0.5},
             "timeSlots": [{"start": "18:30", "image": "/t.gif", "fit": "fit"}]}
            """,
        )
        assertEquals(SwitchBy.APPEARANCE, mac.switchBy)
        assertEquals(Slot("/dark.gif", Placement(Fit.FREE, freeX = 0.0, freeY = 1.0, freeSize = 0.4)), mac.darkSlot)
        assertEquals(listOf(TimeSlot(18 * 60 + 30, slot("/t.gif", Fit.FIT))), mac.timeSlots)
        assertNull(mac.lightSlot)
    }

    @Test
    fun brokenSlotsAreSkippedOrClamped() {
        val c = Config.parse(
            """
            {"switchBy": "time", "timeSlots": [
              {"start": "7:00", "image": "/a.gif", "fit": "fill", "freeSize": 9, "position": -1},
              "broken",
              {"start": "25:00", "image": "/b.gif"}
            ], "darkSlot": {"fit": "nope"}, "lightSlot": 3}
            """,
        )
        assertEquals(2, c.timeSlots.size)
        assertEquals(420, c.timeSlots[0].start)
        assertEquals(3.0, c.timeSlots[0].slot.placement.freeSize, 0.0)
        assertEquals(0.0, c.timeSlots[0].slot.placement.position, 0.0)
        assertEquals(0, c.timeSlots[1].start)
        assertEquals(Slot(), c.darkSlot)
        assertNull(c.lightSlot)
        assertEquals(SwitchBy.NONE, Config.parse("""{"switchBy": "weather"}""").switchBy)
    }
}
