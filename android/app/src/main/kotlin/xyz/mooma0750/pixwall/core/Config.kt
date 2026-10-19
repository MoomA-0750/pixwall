package xyz.mooma0750.pixwall.core

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
    /** 余白の色を自分で選ぶか（false は「自動」。画像の1枚目の左上の1ドットの色） */
    val customBackground: Boolean = false,
    /** 自分で選んだ余白の色（0xRRGGBB）。まだ選んでいなければ null。自動にしても覚えておく */
    val backgroundColor: Int? = null,
    /** 速さの倍率（0.25〜4） */
    val speed: Double = 1.0,
    /** 省電力モードのときに止める */
    val pauseOnLowPower: Boolean = true,
) {
    /** 余白に使う、自分で選んだ色。自動のとき・色をまだ選んでいないときは null（画像の左上の色を使う） */
    val customColor: Int? get() = if (customBackground) backgroundColor else null

    fun toJson(): String {
        val o = JSONObject()
        o.put("version", version)
        image?.let { o.put("image", it) }
        o.put("fit", fit.json)
        o.put("position", position)
        o.put("background", if (customBackground) "custom" else "auto")
        backgroundColor?.let { o.put("backgroundColor", hexColor(it)) }
        o.put("speed", speed)
        o.put("pauseOnLowPower", pauseOnLowPower)
        return o.toString(2).replace("\\/", "/")
    }

    companion object {
        /** 足りない項目・読めない値は既定の値にする（壊れていても起動できる） */
        fun parse(text: String?): Config {
            val d = Config()
            val o = runCatching { JSONObject(text ?: return d) }.getOrNull() ?: return d
            fun num(key: String) = (o.opt(key) as? Number)?.toDouble()
            fun bool(key: String) = o.opt(key) as? Boolean
            return Config(
                version = num("version")?.toInt() ?: d.version,
                image = o.opt("image") as? String,
                fit = Fit.fromJson(o.opt("fit") as? String) ?: d.fit,
                position = (num("position") ?: d.position).coerceIn(0.0, 1.0),
                customBackground = (o.opt("background") as? String)?.let { it == "custom" } ?: d.customBackground,
                backgroundColor = (o.opt("backgroundColor") as? String)?.let(::parseHexColor),
                speed = (num("speed") ?: d.speed).coerceIn(0.25, 4.0),
                pauseOnLowPower = bool("pauseOnLowPower") ?: d.pauseOnLowPower,
            )
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
