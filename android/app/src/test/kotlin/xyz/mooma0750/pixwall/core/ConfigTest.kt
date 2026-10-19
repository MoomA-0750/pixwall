package xyz.mooma0750.pixwall.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ConfigTest {
    @Test
    fun missingOrBrokenFileGivesDefaults() {
        assertEquals(Config(), Config.parse(null))
        assertEquals(Config(), Config.parse("{"))
        assertEquals(Config(), Config.parse("""{"fit": "zoom", "speed": "fast", "position": null}"""))
    }

    @Test
    fun readsMacAndWindowsShape() {
        val c = Config.parse(
            """
            {"version": 1, "image": "content://x/y.gif", "fit": "fill", "position": 0.25,
             "background": "custom", "backgroundColor": "#12ab34", "speed": 2,
             "pauseOnLowPower": false, "launchAtLogin": true}
            """,
        )
        assertEquals("content://x/y.gif", c.image)
        assertEquals(Fit.FILL, c.fit)
        assertEquals(0.25, c.position, 0.0)
        assertEquals(0x12AB34, c.customColor)
        assertEquals(2.0, c.speed, 0.0)
        assertEquals(false, c.pauseOnLowPower)
    }

    @Test
    fun valuesAreClamped() {
        val c = Config.parse("""{"position": 3, "speed": 100}""")
        assertEquals(1.0, c.position, 0.0)
        assertEquals(4.0, c.speed, 0.0)
    }

    // 自動に戻しても選んだ色は覚えておく。読めない色は選んでいないのと同じ
    @Test
    fun backgroundColorIsKeptWhenAuto() {
        val auto = Config.parse("""{"background": "auto", "backgroundColor": "#FF0000"}""")
        assertNull(auto.customColor)
        assertEquals(0xFF0000, auto.backgroundColor)
        assertNull(Config.parse("""{"background": "custom", "backgroundColor": "red"}""").backgroundColor)
    }

    @Test
    fun roundTrips() {
        val c = Config(image = "content://a/b.gif", fit = Fit.FIT, position = 0.1, customBackground = true, backgroundColor = 0x0A0B0C, speed = 0.5, pauseOnLowPower = false)
        assertEquals(c, Config.parse(c.toJson()))
        // 色をまだ選んでいなければ書かない
        assertEquals(false, Config().toJson().contains("backgroundColor"))
        assertEquals(true, c.toJson().contains("\"#0A0B0C\""))
    }
}
