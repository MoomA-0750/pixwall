package xyz.mooma0750.pixwall

import android.app.WallpaperColors
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Bitmap
import android.graphics.Canvas
import android.net.Uri
import android.os.FileObserver
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.os.SystemClock
import android.service.wallpaper.WallpaperService
import android.util.Log
import android.view.SurfaceHolder
import xyz.mooma0750.pixwall.core.Config
import xyz.mooma0750.pixwall.core.Placement
import xyz.mooma0750.pixwall.core.PixelRect
import xyz.mooma0750.pixwall.core.frameDelayMillis
import xyz.mooma0750.pixwall.core.layout
import java.util.concurrent.Executors

/**
 * ライブ壁紙。絵が変わる瞬間だけ OpenGL ES で描き（GlRenderer）、待っている間は Handler の予約で眠る。
 * 見えていない（アプリが前にある・画面が消えている）とき、省電力モード（設定で選べる）のときは止める
 */
class PixwallWallpaperService : WallpaperService() {
    override fun onCreateEngine(): Engine = PixwallEngine()

    private inner class PixwallEngine : Engine() {
        private val handler = Handler(Looper.getMainLooper())
        private val loader = Executors.newSingleThreadExecutor()
        private val power = getSystemService(PowerManager::class.java)

        private var renderer: GlRenderer? = null
        private var rect: PixelRect? = null
        private var rectFrames: Frames? = null
        private var rectWidth = 0
        private var rectHeight = 0
        private var rectPlacement: Placement? = null

        private var config = Config()
        private var frames: Frames? = null
        private var loadedImage: String? = null
        private var loadGeneration = 0
        private var index = 0
        private var nextAt = 0L
        private var surfaceWidth = 0
        private var surfaceHeight = 0
        private var visible = false
        private var powerSave = false

        private val tick = Runnable { advance() }

        private val powerReceiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                powerSave = power.isPowerSaveMode
                schedule()
            }
        }

        // 設定画面は別のプロセスなので、config.json の入れ替えで気づく
        private val configObserver = object : FileObserver(filesDir, MOVED_TO or CLOSE_WRITE) {
            override fun onEvent(event: Int, path: String?) {
                if (path == ConfigStore.FILE_NAME) handler.post { reloadConfig() }
            }
        }

        override fun onCreate(surfaceHolder: SurfaceHolder) {
            super.onCreate(surfaceHolder)
            renderer = runCatching { GlRenderer() }.onFailure { Log.e(TAG, "cannot start OpenGL ES", it) }.getOrNull()
            powerSave = power.isPowerSaveMode
            registerReceiver(powerReceiver, IntentFilter(PowerManager.ACTION_POWER_SAVE_MODE_CHANGED))
            configObserver.startWatching()
            reloadConfig()
        }

        override fun onDestroy() {
            handler.removeCallbacks(tick)
            configObserver.stopWatching()
            unregisterReceiver(powerReceiver)
            loadGeneration++
            loader.shutdown()
            frames?.recycle()
            frames = null
            renderer?.release()
            renderer = null
            super.onDestroy()
        }

        override fun onSurfaceCreated(holder: SurfaceHolder) {
            super.onSurfaceCreated(holder)
            renderer?.attach(holder.surface)
        }

        override fun onSurfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
            super.onSurfaceChanged(holder, format, width, height)
            surfaceWidth = width
            surfaceHeight = height
            draw()
        }

        override fun onSurfaceDestroyed(holder: SurfaceHolder) {
            surfaceWidth = 0
            surfaceHeight = 0
            handler.removeCallbacks(tick)
            renderer?.detach()
            super.onSurfaceDestroyed(holder)
        }

        override fun onSurfaceRedrawNeeded(holder: SurfaceHolder) {
            super.onSurfaceRedrawNeeded(holder)
            draw()
        }

        override fun onVisibilityChanged(visible: Boolean) {
            this.visible = visible
            if (visible) {
                draw()
                nextAt = 0
            }
            schedule()
        }

        override fun onComputeColors(): WallpaperColors? {
            val f = frames ?: return null
            val sample = Bitmap.createBitmap(f.colorSample.width, f.colorSample.height, Bitmap.Config.ARGB_8888)
            Canvas(sample).apply {
                drawColor(background(f) or 0xFF000000.toInt())
                drawBitmap(f.colorSample, 0f, 0f, null)
            }
            return WallpaperColors.fromBitmap(sample).also { sample.recycle() }
        }

        private fun background(f: Frames): Int = config.customColor ?: f.autoBackground

        private fun reloadConfig() {
            val old = config
            config = ConfigStore.load(this@PixwallWallpaperService)
            if (config.image != loadedImage) {
                loadImage(config.image)
            } else {
                if (config.customColor != old.customColor) notifyColorsChanged()
                draw()
                nextAt = 0
                schedule()
            }
        }

        private fun loadImage(image: String?) {
            loadedImage = image
            val generation = ++loadGeneration
            if (image == null) {
                replaceFrames(null)
                return
            }
            val isPreview = isPreview
            loader.execute {
                val loaded = runCatching { Frames.load(this@PixwallWallpaperService, Uri.parse(image)) }
                    .onFailure { Log.w(TAG, "cannot load $image (preview=$isPreview)", it) }
                    .getOrNull()
                handler.post {
                    if (generation == loadGeneration) replaceFrames(loaded) else loaded?.recycle()
                }
            }
        }

        private fun replaceFrames(f: Frames?) {
            frames?.recycle()
            frames = f
            // GPU に置いたら CPU 側の画素は手放す（大きさ・待ち時間・色の見本は残る）
            if (f != null) {
                renderer?.upload(f.bitmaps)
                f.bitmaps.forEach(Bitmap::recycle)
            }
            index = 0
            nextAt = 0
            notifyColorsChanged()
            draw()
            schedule()
        }

        private fun shouldRun(): Boolean {
            val f = frames ?: return false
            return visible && surfaceWidth > 0 && f.count > 1 && !(powerSave && config.pauseOnLowPower)
        }

        /** 次のフレームを予約する。動かさないときは予約を消して眠る */
        private fun schedule() {
            handler.removeCallbacks(tick)
            if (!shouldRun()) return
            val f = frames ?: return
            val now = SystemClock.uptimeMillis()
            // 止まっていた・遅れたときは今から数え直す（追いつこうとして続けて描かない）
            if (nextAt == 0L || nextAt < now - 1000) nextAt = now + frameDelayMillis(f.delaysMillis[index], config.speed)
            handler.postAtTime(tick, nextAt)
        }

        private fun advance() {
            val f = frames ?: return
            index = (index + 1) % f.count
            draw()
            nextAt += frameDelayMillis(f.delaysMillis[index], config.speed)
            schedule()
        }

        private fun draw() {
            if (surfaceWidth <= 0 || surfaceHeight <= 0) return
            val f = frames
            if (f == null) {
                renderer?.draw(surfaceWidth, surfaceHeight, 0, -1, null)
                return
            }
            // 置く位置は、画像・面の大きさ・並べ方・位置が変わったときだけ計算し直す（毎フレームの確保を減らす）
            if (f !== rectFrames || surfaceWidth != rectWidth || surfaceHeight != rectHeight || config.placement != rectPlacement) {
                rect = layout(f.width, f.height, surfaceWidth, surfaceHeight, config.placement)
                rectFrames = f
                rectWidth = surfaceWidth
                rectHeight = surfaceHeight
                rectPlacement = config.placement
            }
            renderer?.draw(surfaceWidth, surfaceHeight, background(f), index.coerceIn(0, f.count - 1), rect)
        }
    }

    companion object {
        private const val TAG = "pixwall"
    }
}
