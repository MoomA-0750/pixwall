package xyz.mooma0750.pixwall.core

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.io.File

/** 4色の GIF を組み立てる。LZW は1画素ごとに clear を挟んで、符号の長さを3ビットのままにする */
private class GifBuilder(val width: Int, val height: Int, val colors: IntArray) {
    private val out = ByteArrayOutputStream()

    init {
        out.write("GIF89a".toByteArray())
        u16(width)
        u16(height)
        out.write(0x80 or 1) // 4色の表
        out.write(0)
        out.write(0)
        for (c in colors) {
            out.write(c shr 16 and 0xFF)
            out.write(c shr 8 and 0xFF)
            out.write(c and 0xFF)
        }
    }

    private fun u16(v: Int) {
        out.write(v and 0xFF)
        out.write(v shr 8 and 0xFF)
    }

    fun frame(
        pixels: IntArray,
        left: Int = 0,
        top: Int = 0,
        w: Int = width,
        h: Int = height,
        delayCs: Int = 10,
        disposal: Int = 1,
        transparent: Int = -1,
        interlaced: Boolean = false,
    ): GifBuilder {
        out.write(0x21)
        out.write(0xF9)
        out.write(4)
        out.write((disposal shl 2) or (if (transparent >= 0) 1 else 0))
        u16(delayCs)
        out.write(maxOf(transparent, 0))
        out.write(0)
        out.write(0x2C)
        u16(left)
        u16(top)
        u16(w)
        u16(h)
        out.write(if (interlaced) 0x40 else 0)
        out.write(2)
        // 符号を3ビットずつ詰める
        val codes = ArrayList<Int>()
        for (p in pixels) {
            codes += 4
            codes += p
        }
        codes += 5
        val data = ByteArrayOutputStream()
        var acc = 0
        var bits = 0
        for (c in codes) {
            acc = acc or (c shl bits)
            bits += 3
            while (bits >= 8) {
                data.write(acc and 0xFF)
                acc = acc ushr 8
                bits -= 8
            }
        }
        if (bits > 0) data.write(acc and 0xFF)
        val bytes = data.toByteArray()
        var i = 0
        while (i < bytes.size) {
            val n = minOf(255, bytes.size - i)
            out.write(n)
            out.write(bytes, i, n)
            i += n
        }
        out.write(0)
        return this
    }

    fun build(): ByteArray = out.toByteArray() + byteArrayOf(0x3B)
}

private const val R = 0xFFFF0000.toInt()
private const val G = 0xFF00FF00.toInt()
private const val B = 0xFF0000FF.toInt()
private const val W = 0xFFFFFFFF.toInt()

class GifTest {
    private val colors = intArrayOf(0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF)

    @Test
    fun readsSizeFramesAndDelays() {
        val gif = decodeGif(
            GifBuilder(2, 2, colors).frame(intArrayOf(0, 1, 2, 3), delayCs = 7).frame(intArrayOf(3, 2, 1, 0), delayCs = 0).build(),
        )
        assertEquals(2, gif.width)
        assertEquals(2, gif.height)
        assertEquals(2, gif.frames.size)
        assertArrayEquals(intArrayOf(R, G, B, W), gif.frames[0])
        assertArrayEquals(intArrayOf(W, B, G, R), gif.frames[1])
        assertEquals(listOf(70, 0), gif.delaysMillis)
    }

    // 透明な番号のところは前のフレームが残る（disposal 1 = そのまま）
    @Test
    fun transparentIndexKeepsPreviousFrame() {
        val gif = decodeGif(
            GifBuilder(2, 1, colors).frame(intArrayOf(0, 0)).frame(intArrayOf(1, 3), transparent = 3).build(),
        )
        assertArrayEquals(intArrayOf(G, R), gif.frames[1])
    }

    // disposal 2 は背景色ではなく透明に戻す（その範囲だけ）
    @Test
    fun backgroundDisposalClearsToTransparent() {
        val gif = decodeGif(
            GifBuilder(2, 1, colors)
                .frame(intArrayOf(0, 0))
                .frame(intArrayOf(1), left = 1, w = 1, h = 1, disposal = 2)
                .frame(intArrayOf(3), w = 1, h = 1, transparent = 3)
                .build(),
        )
        assertArrayEquals(intArrayOf(R, G), gif.frames[1])
        assertArrayEquals(intArrayOf(R, 0), gif.frames[2])
    }

    // disposal 3 は描く前の絵に戻す
    @Test
    fun previousDisposalRestores() {
        val gif = decodeGif(
            GifBuilder(2, 1, colors)
                .frame(intArrayOf(0, 0))
                .frame(intArrayOf(2, 2), disposal = 3)
                .frame(intArrayOf(3), w = 1, h = 1)
                .build(),
        )
        assertArrayEquals(intArrayOf(B, B), gif.frames[1])
        assertArrayEquals(intArrayOf(W, R), gif.frames[2])
    }

    @Test
    fun interlacedRowsArePlaced() {
        // 5行の飛び越し: 書かれる順は 0, 4, 2, 1, 3 行目
        val gif = decodeGif(GifBuilder(1, 5, colors).frame(intArrayOf(0, 1, 2, 3, 0), interlaced = true).build())
        assertArrayEquals(intArrayOf(R, W, B, R, G), gif.frames[0])
    }

    @Test
    fun truncatedFileKeepsFramesReadSoFar() {
        val full = GifBuilder(2, 1, colors).frame(intArrayOf(0, 1)).frame(intArrayOf(2, 3)).build()
        val gif = decodeGif(full.copyOf(full.size - 6))
        assertTrue(gif.frames.isNotEmpty())
        assertArrayEquals(intArrayOf(R, G), gif.frames[0])
    }

    @Test
    fun recognizesGifHeader() {
        assertTrue(isGif("GIF89a....".toByteArray()))
        assertFalse(isGif("\u0089PNG\r\n\u001a\n".toByteArray()))
    }

    // 試す・測るための画像（64×64、30フレーム）。LZW の符号がふつうに伸びる GIF も読めること
    @Test
    fun readsSampleGif() {
        val file = File("../../samples/dot-64x64-30f.gif")
        val gif = decodeGif(file.readBytes())
        assertEquals(64, gif.width)
        assertEquals(64, gif.height)
        assertEquals(30, gif.frames.size)
        // 1枚目は全部の画素が埋まっている
        assertTrue(gif.frames[0].all { it != 0 })
    }

    // 続く同じ絵は1枚にまとめ、待ち時間を足す。10ms 以下は 100ms にしてから足す
    @Test
    fun repeatedFramesAreMerged() {
        val gif = decodeGif(
            GifBuilder(1, 1, colors)
                .frame(intArrayOf(0), delayCs = 5)
                .frame(intArrayOf(0), delayCs = 0)
                .frame(intArrayOf(1), delayCs = 10)
                .frame(intArrayOf(2), delayCs = 3)
                .build(),
        ).mergingRepeats()
        assertEquals(3, gif.frames.size)
        assertEquals(listOf(150, 100, 30), gif.delaysMillis)
    }

    // 最後のフレームが1枚目と同じ絵なら、1枚目にまとめる（1周したときに同じ絵を描き直さない）
    @Test
    fun lastFrameSameAsFirstIsMerged() {
        val gif = decodeGif(
            GifBuilder(1, 1, colors)
                .frame(intArrayOf(0), delayCs = 5)
                .frame(intArrayOf(1), delayCs = 5)
                .frame(intArrayOf(2), delayCs = 5)
                .frame(intArrayOf(0), delayCs = 7)
                .build(),
        ).mergingRepeats()
        assertEquals(3, gif.frames.size)
        assertEquals(listOf(120, 50, 50), gif.delaysMillis)
        // 0, 1, 0 は 0 と 1 の行き来になる（2枚残るので止まらない）
        val two = decodeGif(GifBuilder(1, 1, colors).frame(intArrayOf(0)).frame(intArrayOf(1)).frame(intArrayOf(0)).build()).mergingRepeats()
        assertEquals(listOf(200, 100), two.delaysMillis)
        // 0, 1 だけのときは、そのまま
        val pair = decodeGif(GifBuilder(1, 1, colors).frame(intArrayOf(0)).frame(intArrayOf(1)).build()).mergingRepeats()
        assertEquals(listOf(100, 100), pair.delaysMillis)
    }
}
