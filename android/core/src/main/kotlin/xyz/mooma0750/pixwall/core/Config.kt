package xyz.mooma0750.pixwall.core

import org.json.JSONArray
import org.json.JSONObject

/**
 * 設定。`config.json` にこの形で書く（Mac・Windows とそろえる）。
 * image は画像の場所（Android では content:// の URI）
 */
data class Config(
    val version: Int = 1,
    val image: String? = null,
    val fit: Fit = Fit.INTEGER,
    /** 画面に合わせる・埋めるときの位置（0 で上・左、0.5 で中央、1 で下・右） */
    val position: Double = 0.5,
    /** 自由に置くときの横・縦の位置（0〜1）と大きさ（画面に合わせたときを 1 とする。0.1〜3）。Placement を見る */
    val freeX: Double = 0.5,
    val freeY: Double = 0.5,
    val freeSize: Double = 1.0,
    /** 余白の色を自分で選ぶか（false は「自動」。画像の1枚目の左上の1ドットの色） */
    val customBackground: Boolean = false,
    /** 自分で選んだ余白の色（0xRRGGBB）。まだ選んでいなければ null。自動にしても覚えておく */
    val backgroundColor: Int? = null,
    /** 速さの倍率（0.25〜4） */
    val speed: Double = 1.0,
    /** 省電力モードのときに止める */
    val pauseOnLowPower: Boolean = true,
    /** 出す画像を何で切り替えるか（Switching.kt） */
    val switchBy: SwitchBy = SwitchBy.NONE,
    /** 時刻で切り替えるときの、時刻ごとの画像と並べ方（書いた順） */
    val timeSlots: List<TimeSlot> = emptyList(),
    /** ダーク・ライトで切り替えるときの画像と並べ方。null なら image と並べ方を使う */
    val lightSlot: Slot? = null,
    val darkSlot: Slot? = null,
) {
    /** 並べ方と位置・大きさ */
    val placement: Placement get() = Placement(fit, position, freeX, freeY, freeSize)

    /** 余白に使う、自分で選んだ色。自動のとき・色をまだ選んでいないときは null（画像の左上の色を使う） */
    val customColor: Int? get() = if (customBackground) backgroundColor else null

    fun toJson(): String {
        val o = JSONObject()
        o.put("version", version)
        image?.let { o.put("image", it) }
        o.put("fit", fit.json)
        o.put("position", position)
        o.put("freeX", freeX)
        o.put("freeY", freeY)
        o.put("freeSize", freeSize)
        o.put("background", if (customBackground) "custom" else "auto")
        backgroundColor?.let { o.put("backgroundColor", hexColor(it)) }
        o.put("speed", speed)
        o.put("pauseOnLowPower", pauseOnLowPower)
        o.put("switchBy", switchBy.json)
        o.put("timeSlots", JSONArray(timeSlots.map { it.toJson() }))
        lightSlot?.let { o.put("lightSlot", it.toJson()) }
        darkSlot?.let { o.put("darkSlot", it.toJson()) }
        return o.toString(2).replace("\\/", "/")
    }

    companion object {
        /** 足りない項目・読めない値は既定の値にする（壊れていても起動できる） */
        fun parse(text: String?): Config {
            val d = Config()
            val o = runCatching { JSONObject(text ?: return d) }.getOrNull() ?: return d
            fun num(key: String) = (o.opt(key) as? Number)?.toDouble()
            fun bool(key: String) = o.opt(key) as? Boolean
            // 画像と並べ方は、切り替える先の1つ（Slot）と同じ読み方をする
            return Config(
                version = num("version")?.toInt() ?: d.version,
                customBackground = (o.opt("background") as? String)?.let { it == "custom" } ?: d.customBackground,
                backgroundColor = (o.opt("backgroundColor") as? String)?.let(::parseHexColor),
                speed = (num("speed") ?: d.speed).coerceIn(0.25, 4.0),
                pauseOnLowPower = bool("pauseOnLowPower") ?: d.pauseOnLowPower,
                switchBy = SwitchBy.fromJson(o.opt("switchBy") as? String) ?: d.switchBy,
                // 読めない1つは飛ばす
                timeSlots = readTimeSlots(o.optJSONArray("timeSlots")),
                lightSlot = o.optJSONObject("lightSlot")?.let(Slot::read),
                darkSlot = o.optJSONObject("darkSlot")?.let(Slot::read),
            ).withMain(Slot.read(o))
        }
    }
}

/** "#RRGGBB"（大文字・小文字どちらでも）を 0xRRGGBB にする。形が違えば null */
fun parseHexColor(s: String): Int? {
    if (s.length != 7 || s[0] != '#') return null
    val digits = s.substring(1)
    if (!digits.all { it in '0'..'9' || it in 'a'..'f' || it in 'A'..'F' }) return null
    return digits.toInt(16)
}

/** 0xRRGGBB を "#RRGGBB"（大文字）にする */
fun hexColor(rgb: Int): String = "#%06X".format(rgb and 0xFFFFFF)
