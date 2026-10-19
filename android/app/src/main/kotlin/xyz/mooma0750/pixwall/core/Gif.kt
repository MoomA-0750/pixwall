package xyz.mooma0750.pixwall.core

/**
 * 重ね方（disposal）まで当てた、元の大きさの GIF。
 * frames は1枚ずつ width×height の ARGB（透明なところは 0）、delaysMillis は GIF に書かれた待ち時間
 */
class GifImage(val width: Int, val height: Int, val frames: List<IntArray>, val delaysMillis: List<Int>)

class GifFormatException(message: String) : Exception(message)

/** GIF かどうかを先頭の6バイトで見る */
fun isGif(bytes: ByteArray): Boolean =
    bytes.size >= 6 && String(bytes, 0, 6, Charsets.US_ASCII).let { it == "GIF87a" || it == "GIF89a" }

/**
 * GIF を全フレーム読み、重ね方を当てる。Android には GIF のフレームを1枚ずつ取り出す公開の仕組みが無いので自分で読む
 * （`AnimatedImageDrawable` は描くのを任せる形で、待ち時間もぼかし方も選べない）。
 * `Background` は背景色ではなく透明に戻す（ブラウザと同じ。Windows の `GifCompositor` とも同じ）。
 * 途中で切れているファイルは、読めたところまでのフレームを返す
 */
fun decodeGif(bytes: ByteArray): GifImage {
    if (!isGif(bytes)) throw GifFormatException("not a GIF")
    val r = Reader(bytes)
    r.pos = 6
    val width = r.u16()
    val height = r.u16()
    val packed = r.u8()
    r.u8() // 背景色の番号（透明に戻すので使わない）
    r.u8() // 縦横比
    if (width <= 0 || height <= 0) throw GifFormatException("empty GIF")
    val globalTable = if (packed and 0x80 != 0) r.colorTable(2 shl (packed and 7)) else null

    val canvas = IntArray(width * height)
    val frames = ArrayList<IntArray>()
    val delays = ArrayList<Int>()
    var disposal = 0
    var transparent = -1
    var delay = 0

    try {
        loop@ while (r.pos < bytes.size) {
            when (r.u8()) {
                0x21 -> {
                    if (r.u8() == 0xF9) {
                        val size = r.u8()
                        val start = r.pos
                        val p = r.u8()
                        disposal = (p shr 2) and 7
                        delay = r.u16() * 10
                        val index = r.u8()
                        transparent = if (p and 1 != 0) index else -1
                        r.pos = start + size
                    }
                    r.skipSubBlocks()
                }
                0x2C -> {
                    val left = r.u16()
                    val top = r.u16()
                    val w = r.u16()
                    val h = r.u16()
                    val p = r.u8()
                    val table = (if (p and 0x80 != 0) r.colorTable(2 shl (p and 7)) else globalTable)
                        ?: throw GifFormatException("no color table")
                    val interlaced = p and 0x40 != 0
                    val minCodeSize = r.u8()
                    val indices = lzw(r.subBlocks(), minCodeSize, w * h)

                    val saved = if (disposal == 3) canvas.copyOf() else null
                    draw(canvas, width, height, indices, left, top, w, h, interlaced, table, transparent)
                    frames += canvas.copyOf()
                    delays += delay
                    when (disposal) {
                        2 -> clear(canvas, width, height, left, top, w, h)
                        3 -> saved!!.copyInto(canvas)
                    }
                    disposal = 0
                    transparent = -1
                    delay = 0
                }
                0x3B -> break@loop
                else -> break@loop
            }
        }
    } catch (e: IndexOutOfBoundsException) {
        // 切れている。読めたところまで
    }
    if (frames.isEmpty()) throw GifFormatException("no frames")
    return GifImage(width, height, frames, delays)
}

private class Reader(val b: ByteArray) {
    var pos = 0

    fun u8(): Int = b[pos++].toInt() and 0xFF

    fun u16(): Int = u8() or (u8() shl 8)

    fun colorTable(n: Int): IntArray = IntArray(n) { (0xFF shl 24) or (u8() shl 16) or (u8() shl 8) or u8() }

    fun skipSubBlocks() {
        while (true) {
            val n = u8()
            if (n == 0) return
            pos += n
        }
    }

    fun subBlocks(): ByteArray {
        val out = java.io.ByteArrayOutputStream()
        while (true) {
            val n = u8()
            if (n == 0) break
            val end = minOf(pos + n, b.size)
            out.write(b, pos, end - pos)
            pos += n
        }
        return out.toByteArray()
    }
}

/** LZW を解いて色の番号を並べる。足りなければ -1（描かない）で埋める */
private fun lzw(data: ByteArray, minCodeSize: Int, count: Int): IntArray {
    val out = IntArray(count) { -1 }
    if (minCodeSize !in 1..11) return out
    val clear = 1 shl minCodeSize
    val end = clear + 1
    val prefix = IntArray(4096)
    val suffix = IntArray(4096)
    val first = IntArray(4096)
    val stack = IntArray(4097)
    for (i in 0 until clear) {
        suffix[i] = i
        first[i] = i
    }
    var codeSize = minCodeSize + 1
    var next = clear + 2
    var prev = -1
    var bits = 0
    var acc = 0
    var n = 0
    var i = 0
    while (n < count) {
        while (bits < codeSize) {
            if (i >= data.size) return out
            acc = acc or ((data[i++].toInt() and 0xFF) shl bits)
            bits += 8
        }
        val code = acc and ((1 shl codeSize) - 1)
        acc = acc ushr codeSize
        bits -= codeSize
        if (code == clear) {
            codeSize = minCodeSize + 1
            next = clear + 2
            prev = -1
            continue
        }
        if (code == end) break
        if (prev == -1) {
            if (code >= clear) break
            out[n++] = code
            prev = code
            continue
        }
        val c = when {
            code < next -> code
            code == next -> prev
            else -> break
        }
        var sp = 0
        if (code == next) stack[sp++] = first[prev]
        var k = c
        while (k >= clear) {
            stack[sp++] = suffix[k]
            k = prefix[k]
        }
        stack[sp++] = k
        val head = k
        while (sp > 0 && n < count) out[n++] = stack[--sp]
        if (next < 4096) {
            prefix[next] = prev
            suffix[next] = head
            first[next] = first[prev]
            next++
            if (next == (1 shl codeSize) && codeSize < 12) codeSize++
        }
        prev = code
    }
    return out
}

private fun draw(
    canvas: IntArray,
    width: Int,
    height: Int,
    indices: IntArray,
    left: Int,
    top: Int,
    w: Int,
    h: Int,
    interlaced: Boolean,
    table: IntArray,
    transparent: Int,
) {
    val rows = if (interlaced) interlacedRows(h) else IntArray(h) { it }
    for (sourceRow in 0 until h) {
        val y = top + rows[sourceRow]
        if (y !in 0 until height) continue
        for (x in 0 until w) {
            val index = indices[sourceRow * w + x]
            if (index < 0 || index == transparent || index >= table.size) continue
            val cx = left + x
            if (cx !in 0 until width) continue
            canvas[y * width + cx] = table[index]
        }
    }
}

/** 飛び越しの GIF で、n 番目に書かれた行が何行目か */
private fun interlacedRows(h: Int): IntArray {
    val rows = IntArray(h)
    var n = 0
    for ((start, step) in listOf(0 to 8, 4 to 8, 2 to 4, 1 to 2)) {
        var y = start
        while (y < h) {
            rows[n++] = y
            y += step
        }
    }
    return rows
}

private fun clear(canvas: IntArray, width: Int, height: Int, left: Int, top: Int, w: Int, h: Int) {
    for (y in maxOf(top, 0) until minOf(top + h, height)) {
        for (x in maxOf(left, 0) until minOf(left + w, width)) canvas[y * width + x] = 0
    }
}
