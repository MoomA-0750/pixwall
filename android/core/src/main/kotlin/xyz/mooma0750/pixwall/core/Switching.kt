package xyz.mooma0750.pixwall.core

import org.json.JSONArray
import org.json.JSONObject

/** 出す画像を何で切り替えるか。設定の JSON にもこの名前で書く（Mac・Windows とそろえる） */
enum class SwitchBy(val json: String) {
    /** 切り替えない（Config の image と並べ方を使う。既定） */
    NONE("none"),

    /** 決めた時刻で切り替える（Config.timeSlots） */
    TIME("time"),

    /** 端末のダークモード・ライトモードで切り替える（Config.lightSlot・darkSlot） */
    APPEARANCE("appearance"),
    ;

    companion object {
        fun fromJson(s: String?): SwitchBy? = entries.firstOrNull { it.json == s }
    }
}

/** 1つの画像と、その並べ方・位置・大きさ。JSON では Config の上の階層と同じ名前の項目に書く */
data class Slot(val image: String? = null, val placement: Placement = Placement()) {
    internal fun put(o: JSONObject) {
        image?.let { o.put("image", it) }
        o.put("fit", placement.fit.json)
        o.put("position", placement.position)
        o.put("freeX", placement.freeX)
        o.put("freeY", placement.freeY)
        o.put("freeSize", placement.freeSize)
    }

    internal fun toJson(): JSONObject = JSONObject().also(::put)

    companion object {
        /** 足りない項目・読めない値は既定の値にし、範囲の外の値は範囲に収める */
        internal fun read(o: JSONObject): Slot {
            val d = Placement()
            fun num(key: String) = (o.opt(key) as? Number)?.toDouble()
            return Slot(
                image = o.opt("image") as? String,
                placement = Placement(
                    fit = Fit.fromJson(o.opt("fit") as? String) ?: d.fit,
                    position = (num("position") ?: d.position).coerceIn(0.0, 1.0),
                    freeX = (num("freeX") ?: d.freeX).coerceIn(0.0, 1.0),
                    freeY = (num("freeY") ?: d.freeY).coerceIn(0.0, 1.0),
                    freeSize = (num("freeSize") ?: d.freeSize).coerceIn(FREE_SIZE_RANGE),
                ),
            )
        }
    }
}

const val MINUTES_PER_DAY = 24 * 60

/** 決めた時刻から出す画像。JSON では Slot の項目に start（"HH:MM"）を足して書く */
data class TimeSlot(
    /** 始まる時刻。0 時からの分（0〜1439） */
    val start: Int,
    val slot: Slot,
) {
    internal fun toJson(): JSONObject = slot.toJson().put("start", formatClock(start))

    companion object {
        /** 時刻が読めないものは 0:00 にする */
        internal fun read(o: JSONObject) = TimeSlot((o.opt("start") as? String)?.let(::parseClock) ?: 0, Slot.read(o))
    }
}

/** 設定画面で編集するもの（どの画像と並べ方か） */
sealed interface SlotRef {
    data object Main : SlotRef

    /** timeSlots の番号 */
    data class Time(val index: Int) : SlotRef

    data object Light : SlotRef

    data object Dark : SlotRef
}

/** "H:MM" か "HH:MM"（0:00〜23:59）を 0 時からの分にする。形が違えば null */
fun parseClock(s: String): Int? {
    val parts = s.split(":")
    if (parts.size != 2 || parts[0].length !in 1..2 || parts[1].length != 2) return null
    if (!parts.all { p -> p.all { it in '0'..'9' } }) return null
    val h = parts[0].toInt()
    val m = parts[1].toInt()
    return if (h < 24 && m < 60) h * 60 + m else null
}

/** 0 時からの分を "HH:MM" にする */
fun formatClock(minutes: Int): String {
    val m = Math.floorMod(minutes, MINUTES_PER_DAY)
    return "%02d:%02d".format(m / 60, m % 60)
}

/** 切り替えないときに出す画像と並べ方（上の階層の image・fit・position・freeX・freeY・freeSize） */
val Config.mainSlot: Slot get() = Slot(image, placement)

fun Config.withMain(s: Slot): Config = copy(
    image = s.image,
    fit = s.placement.fit,
    position = s.placement.position,
    freeX = s.placement.freeX,
    freeY = s.placement.freeY,
    freeSize = s.placement.freeSize,
)

/**
 * 時刻で切り替えるときに、今（0 時からの分）出す timeSlots の番号。無ければ null。
 * 始まる時刻が今以前のうち一番遅いもの。無ければ前の日から続く、一番遅く始まるもの。同じ時刻なら後に書いたもの
 */
fun Config.activeTimeIndex(minute: Int): Int? {
    if (timeSlots.isEmpty()) return null
    val order = timeSlots.indices.sortedWith(compareBy({ timeSlots[it].start }, { it }))
    return order.lastOrNull { timeSlots[it].start <= minute } ?: order.last()
}

/** 今（0 時からの分・ダークモードか）出す画像と並べ方。切り替える先が無ければ mainSlot */
fun Config.activeSlot(minute: Int, dark: Boolean): Slot = when (switchBy) {
    SwitchBy.NONE -> mainSlot
    SwitchBy.TIME -> activeTimeIndex(minute)?.let { timeSlots[it].slot } ?: mainSlot
    SwitchBy.APPEARANCE -> (if (dark) darkSlot else lightSlot) ?: mainSlot
}

/** 今出すものを mainSlot に入れた設定。壁紙はこれで描く */
fun Config.resolved(minute: Int, dark: Boolean): Config = withMain(activeSlot(minute, dark))

/** 時刻で切り替えるとき、今から次に切り替わる時刻までの分（1〜1440）。切り替えないなら null */
fun Config.minutesToNextSwitch(minute: Int): Int? {
    if (switchBy != SwitchBy.TIME || timeSlots.map { it.start }.toSet().size < 2) return null
    return timeSlots.minOf { Math.floorMod(it.start - minute - 1, MINUTES_PER_DAY) + 1 }
}

/** 切り替え方を変える。初めて切り替えるときは、今の画像と並べ方から始める（時刻は 6:00 と 18:00、ダーク・ライトは両方） */
fun Config.switchTo(mode: SwitchBy): Config = when (mode) {
    SwitchBy.NONE -> copy(switchBy = mode)
    SwitchBy.TIME -> copy(
        switchBy = mode,
        timeSlots = timeSlots.ifEmpty { listOf(TimeSlot(6 * 60, mainSlot), TimeSlot(18 * 60, mainSlot)) },
    )
    SwitchBy.APPEARANCE -> copy(switchBy = mode, lightSlot = lightSlot ?: mainSlot, darkSlot = darkSlot ?: mainSlot)
}

/** 無いもの（番号が範囲の外・まだ選んでいないダーク・ライト）は mainSlot */
fun Config.slot(ref: SlotRef): Slot = when (ref) {
    SlotRef.Main -> mainSlot
    is SlotRef.Time -> timeSlots.getOrNull(ref.index)?.slot ?: mainSlot
    SlotRef.Light -> lightSlot ?: mainSlot
    SlotRef.Dark -> darkSlot ?: mainSlot
}

/** 番号が範囲の外なら何も変えない */
fun Config.withSlot(ref: SlotRef, s: Slot): Config = when (ref) {
    SlotRef.Main -> withMain(s)
    is SlotRef.Time ->
        if (ref.index in timeSlots.indices) {
            copy(timeSlots = timeSlots.toMutableList().also { it[ref.index] = it[ref.index].copy(slot = s) })
        } else {
            this
        }
    SlotRef.Light -> copy(lightSlot = s)
    SlotRef.Dark -> copy(darkSlot = s)
}

/** 設定に出てくる画像の場所（重なりなし、出てくる順） */
val Config.allImages: List<String>
    get() = (listOf(image) + timeSlots.map { it.slot.image } + listOf(lightSlot?.image, darkSlot?.image))
        .filterNotNull().distinct()

internal fun readTimeSlots(a: JSONArray?): List<TimeSlot> =
    if (a == null) emptyList() else (0 until a.length()).mapNotNull { i -> a.optJSONObject(i)?.let(TimeSlot::read) }
