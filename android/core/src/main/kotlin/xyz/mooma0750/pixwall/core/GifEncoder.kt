package xyz.mooma0750.pixwall.core

import java.io.ByteArrayOutputStream

/**
 * 不透明な RGB のフレームを、くり返し続ける GIF に書く。
 * 2枚目からは前のフレームと違うところを囲む四角だけを書き、同じところは透明にする（重ね方は「そのまま残す」）。
 * 色はフレームごとの表に入れる。1枚で 256 色（違うところだけを書くフレームは 255 色）を超えたら、多い色から残し、ほかは近い色にする
 */
class GifEncoder(private val width: Int, private val height: Int) {
    private val out = ByteArrayOutputStream()
    private var previous: IntArray? = null

    init {
        require(width in 1..0xFFFF && height in 1..0xFFFF)
        out.write("GIF89a".toByteArray(Charsets.US_ASCII))
        u16(width)
        u16(height)
        out.write(0) // 全体の色の表は持たない
        out.write(0)
        out.write(0)
        // くり返し続ける（NETSCAPE2.0）
        out.write(byteArrayOf(0x21, 0xFF.toByte(), 11))
        out.write("NETSCAPE2.0".toByteArray(Charsets.US_ASCII))
        out.write(byteArrayOf(3, 1, 0, 0, 0))
    }

    /** rgb は width×height の 0xRRGGBB（上の8ビットは見ない）。delayCentis は 1/100 秒 */
    fun addFrame(rgb: IntArray, delayCentis: Int) {
        require(rgb.size == width * height)
        val pixels = IntArray(rgb.size) { rgb[it] and 0xFFFFFF }
        val prev = previous
        var left = 0
        var top = 0
        var right = width
        var bottom = height
        if (prev != null) {
            left = width
            top = height
            right = 0
            bottom = 0
            for (y in 0 until height) {
                for (x in 0 until width) {
                    val i = y * width + x
                    if (pixels[i] != prev[i]) {
                        if (x < left) left = x
                        if (x >= right) right = x + 1
                        if (y < top) top = y
                        if (y >= bottom) bottom = y + 1
                    }
                }
            }
            // 何も変わらないフレームも、待ち時間のために1画素だけ書く
            if (right == 0) {
                left = 0
                top = 0
                right = 1
                bottom = 1
            }
        }
        val w = right - left
        val h = bottom - top

        // 書く四角の中の色を数える。前と同じところは透明（番号 0）にする
        val transparentUsed = prev != null
        val counts = HashMap<Int, Int>()
        for (y in top until bottom) {
            for (x in left until right) {
                val i = y * width + x
                if (prev != null && pixels[i] == prev[i]) continue
                counts.merge(pixels[i], 1, Int::plus)
            }
        }
        val reserved = if (transparentUsed) 1 else 0
        val palette = counts.entries.sortedByDescending { it.value }.take(256 - reserved).map { it.key }
        val indexOf = HashMap<Int, Int>()
        palette.forEachIndexed { i, c -> indexOf[c] = i + reserved }
        fun index(c: Int): Int = indexOf.getOrPut(c) { nearest(palette, c) + reserved }

        var bits = 1
        while ((1 shl bits) < palette.size + reserved) bits++
        val tableSize = 1 shl bits

        // 表で選んだ色を「前のフレーム」として覚える（近い色にしたところも、見えている色にそろえる）
        val shown = prev?.copyOf() ?: IntArray(pixels.size)
        val indices = IntArray(w * h)
        for (y in 0 until h) {
            for (x in 0 until w) {
                val i = (top + y) * width + left + x
                if (prev != null && pixels[i] == prev[i]) {
                    indices[y * w + x] = 0
                } else {
                    val k = index(pixels[i])
                    indices[y * w + x] = k
                    shown[i] = palette[k - reserved]
                }
            }
        }
        previous = shown

        // 描き方の決まり（重ね方 1 = そのまま残す、透明の番号 0）
        out.write(byteArrayOf(0x21, 0xF9.toByte(), 4))
        out.write((1 shl 2) or (if (transparentUsed) 1 else 0))
        u16(delayCentis.coerceIn(0, 0xFFFF))
        out.write(0)
        out.write(0)

        out.write(0x2C)
        u16(left)
        u16(top)
        u16(w)
        u16(h)
        out.write(0x80 or (bits - 1)) // フレームの色の表
        for (k in 0 until tableSize) {
            val c = if (k < reserved || k - reserved >= palette.size) 0 else palette[k - reserved]
            out.write(c shr 16 and 0xFF)
            out.write(c shr 8 and 0xFF)
            out.write(c and 0xFF)
        }
        val minCodeSize = maxOf(2, bits)
        out.write(minCodeSize)
        val data = lzwEncode(indices, minCodeSize)
        var p = 0
        while (p < data.size) {
            val n = minOf(255, data.size - p)
            out.write(n)
            out.write(data, p, n)
            p += n
        }
        out.write(0)
    }

    fun finish(): ByteArray {
        out.write(0x3B)
        return out.toByteArray()
    }

    private fun u16(v: Int) {
        out.write(v and 0xFF)
        out.write(v shr 8 and 0xFF)
    }
}

private fun nearest(palette: List<Int>, c: Int): Int {
    var best = 0
    var bestDistance = Int.MAX_VALUE
    palette.forEachIndexed { i, p ->
        val dr = (p shr 16 and 0xFF) - (c shr 16 and 0xFF)
        val dg = (p shr 8 and 0xFF) - (c shr 8 and 0xFF)
        val db = (p and 0xFF) - (c and 0xFF)
        val d = dr * dr + dg * dg + db * db
        if (d < bestDistance) {
            bestDistance = d
            best = i
        }
    }
    return best
}

/** 色の番号の並びを GIF の LZW で縮める。表が 4096 で埋まったら clear で作り直す */
private fun lzwEncode(indices: IntArray, minCodeSize: Int): ByteArray {
    val out = ByteArrayOutputStream()
    val clear = 1 shl minCodeSize
    val end = clear + 1
    var codeSize = minCodeSize + 1
    var next = clear + 2
    // (前の符号 << 8 | 次の番号) → 符号
    val table = HashMap<Int, Int>()
    var acc = 0
    var bits = 0

    fun emit(code: Int) {
        acc = acc or (code shl bits)
        bits += codeSize
        while (bits >= 8) {
            out.write(acc and 0xFF)
            acc = acc ushr 8
            bits -= 8
        }
    }

    emit(clear)
    if (indices.isEmpty()) {
        emit(end)
        if (bits > 0) out.write(acc and 0xFF)
        return out.toByteArray()
    }
    var prefix = indices[0]
    for (i in 1 until indices.size) {
        val k = indices[i]
        val key = (prefix shl 8) or k
        val found = table[key]
        if (found != null) {
            prefix = found
            continue
        }
        emit(prefix)
        if (next < 4096) {
            table[key] = next++
            // 読む側は、次の符号を足した時点で長さを増やす
            if (next > (1 shl codeSize) && codeSize < 12) codeSize++
        } else {
            emit(clear)
            table.clear()
            codeSize = minCodeSize + 1
            next = clear + 2
        }
        prefix = k
    }
    emit(prefix)
    emit(end)
    if (bits > 0) out.write(acc and 0xFF)
    return out.toByteArray()
}
