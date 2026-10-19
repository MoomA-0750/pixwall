package xyz.mooma0750.pixwall

import android.graphics.Bitmap
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLSurface
import android.opengl.GLES20
import android.opengl.GLUtils
import android.view.Surface
import xyz.mooma0750.pixwall.core.PixelRect
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * 壁紙の面に OpenGL ES で描く。フレームは最初に全部テクスチャにして GPU に置き、絵が変わるときは
 * テクスチャを1枚選んで四角を描き、差し出すだけにする（Canvas で描くと、毎回の記録と同期で CPU を使うため）。
 * 拡大は GL_NEAREST でぼかさない。GL の文脈は面と別に持ち、面が作り直されてもテクスチャを読み直さない
 * （面が無い間は 1×1 の pbuffer を current にする）。呼ぶのは主のスレッドだけ
 */
class GlRenderer {
    private val display: EGLDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
    private val config: EGLConfig
    private val context: EGLContext
    private val pbuffer: EGLSurface
    private var window: EGLSurface? = null
    private val program: Int
    private val vertexBuffer: Int
    private val quad = ByteBuffer.allocateDirect(4 * 2 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()
    private var textures = IntArray(0)

    init {
        val version = IntArray(2)
        check(EGL14.eglInitialize(display, version, 0, version, 1)) { "eglInitialize" }
        val configs = arrayOfNulls<EGLConfig>(1)
        val count = IntArray(1)
        val attributes = intArrayOf(
            EGL14.EGL_RED_SIZE, 8,
            EGL14.EGL_GREEN_SIZE, 8,
            EGL14.EGL_BLUE_SIZE, 8,
            EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
            EGL14.EGL_SURFACE_TYPE, EGL14.EGL_WINDOW_BIT or EGL14.EGL_PBUFFER_BIT,
            EGL14.EGL_NONE,
        )
        check(EGL14.eglChooseConfig(display, attributes, 0, configs, 0, 1, count, 0) && count[0] > 0) { "eglChooseConfig" }
        config = configs[0]!!
        context = EGL14.eglCreateContext(display, config, EGL14.EGL_NO_CONTEXT, intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE), 0)
        pbuffer = EGL14.eglCreatePbufferSurface(display, config, intArrayOf(EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE), 0)
        makeCurrent()

        program = link(
            """
            attribute vec2 position;
            varying vec2 uv;
            void main() {
                uv = vec2(position.x, 1.0 - position.y);
                gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
            }
            """,
            """
            precision mediump float;
            uniform sampler2D image;
            varying vec2 uv;
            void main() { gl_FragColor = texture2D(image, uv); }
            """,
        )
        // 描くための状態は、この文脈に一度だけ組む（毎フレームは、テクスチャを選んで描くだけ）。頂点は GPU のバッファに置く
        GLES20.glUseProgram(program)
        GLES20.glUniform1i(GLES20.glGetUniformLocation(program, "image"), 0)
        quad.put(floatArrayOf(0f, 0f, 1f, 0f, 0f, 1f, 1f, 1f)).position(0)
        val buffers = IntArray(1)
        GLES20.glGenBuffers(1, buffers, 0)
        vertexBuffer = buffers[0]
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vertexBuffer)
        GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, 4 * 2 * 4, quad, GLES20.GL_STATIC_DRAW)
        val positionLoc = GLES20.glGetAttribLocation(program, "position")
        GLES20.glEnableVertexAttribArray(positionLoc)
        GLES20.glVertexAttribPointer(positionLoc, 2, GLES20.GL_FLOAT, false, 0, 0)
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        // テクスチャは premultiplied（GLUtils が Bitmap をそのまま上げる）
        GLES20.glEnable(GLES20.GL_BLEND)
        GLES20.glBlendFunc(GLES20.GL_ONE, GLES20.GL_ONE_MINUS_SRC_ALPHA)
    }

    /** 壁紙の面につなぐ（面が作られたとき） */
    fun attach(surface: Surface) {
        detach()
        window = EGL14.eglCreateWindowSurface(display, config, surface, intArrayOf(EGL14.EGL_NONE), 0)
            .takeIf { it != EGL14.EGL_NO_SURFACE }
        makeCurrent()
    }

    /** 面から離す（面が消える前に） */
    fun detach() {
        val w = window ?: return
        window = null
        makeCurrent()
        EGL14.eglDestroySurface(display, w)
    }

    /** すでに current なら何もしない（毎フレームの JNI の呼び出しを減らす） */
    private fun makeCurrent() {
        val s = window ?: pbuffer
        if (current === this && currentSurface === s) return
        EGL14.eglMakeCurrent(display, s, s, context)
        current = this
        currentSurface = s
    }

    /** フレームをテクスチャにする。前のテクスチャは消す。Bitmap はこのあと手放してよい */
    fun upload(bitmaps: List<Bitmap>) {
        makeCurrent()
        deleteTextures()
        textures = IntArray(bitmaps.size)
        GLES20.glGenTextures(textures.size, textures, 0)
        bitmaps.forEachIndexed { i, bitmap ->
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, textures[i])
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_NEAREST)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_NEAREST)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
            GLUtils.texImage2D(GLES20.GL_TEXTURE_2D, 0, bitmap, 0)
        }
    }

    /**
     * 面いっぱいを background（0xRRGGBB）で塗り、frame 枚目を rect に描いて差し出す。
     * frame が -1 なら塗るだけ。rect は面の左上からの物理ピクセル
     */
    fun draw(width: Int, height: Int, background: Int, frame: Int, rect: PixelRect?) {
        val w = window ?: return
        makeCurrent()
        GLES20.glViewport(0, 0, width, height)
        GLES20.glClearColor((background shr 16 and 0xFF) / 255f, (background shr 8 and 0xFF) / 255f, (background and 0xFF) / 255f, 1f)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
        if (frame in textures.indices && rect != null) {
            // GL の原点は左下なので、上からの y を下からに直す
            GLES20.glViewport(rect.x, height - rect.y - rect.height, rect.width, rect.height)
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, textures[frame])
            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        }
        EGL14.eglSwapBuffers(display, w)
    }

    /** 片付ける。eglTerminate はしない（同じプロセスのほかのエンジン（プレビューなど）の EGL まで終えてしまうため） */
    fun release() {
        detach()
        makeCurrent()
        deleteTextures()
        GLES20.glDeleteBuffers(1, intArrayOf(vertexBuffer), 0)
        GLES20.glDeleteProgram(program)
        EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
        current = null
        currentSurface = null
        EGL14.eglDestroySurface(display, pbuffer)
        EGL14.eglDestroyContext(display, context)
    }

    private fun deleteTextures() {
        if (textures.isNotEmpty()) GLES20.glDeleteTextures(textures.size, textures, 0)
        textures = IntArray(0)
    }

    private fun link(vertex: String, fragment: String): Int {
        fun compile(type: Int, source: String): Int {
            val shader = GLES20.glCreateShader(type)
            GLES20.glShaderSource(shader, source)
            GLES20.glCompileShader(shader)
            return shader
        }
        val p = GLES20.glCreateProgram()
        GLES20.glAttachShader(p, compile(GLES20.GL_VERTEX_SHADER, vertex))
        GLES20.glAttachShader(p, compile(GLES20.GL_FRAGMENT_SHADER, fragment))
        GLES20.glLinkProgram(p)
        return p
    }

    private companion object {
        // 今 current の文脈と面（GL を呼ぶのは主のスレッドだけなので、1つ覚えておけば足りる）
        var current: GlRenderer? = null
        var currentSurface: EGLSurface? = null
    }
}
