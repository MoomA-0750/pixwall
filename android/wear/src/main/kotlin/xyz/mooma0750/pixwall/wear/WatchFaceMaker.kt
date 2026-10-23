package xyz.mooma0750.pixwall.wear

import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.os.ParcelFileDescriptor
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import androidx.wear.watchfacepush.WatchFacePushManagerFactory
import com.android.apksig.ApkSigner
import com.google.android.wearable.watchface.validator.client.DwfValidatorFactory
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import xyz.mooma0750.pixwall.core.Config
import xyz.mooma0750.pixwall.core.GifImage
import xyz.mooma0750.pixwall.core.WATCH_FACE_MEMORY_LIMIT
import xyz.mooma0750.pixwall.core.renderWatchFace
import xyz.mooma0750.pixwall.core.rewriteWatchFaceApk
import xyz.mooma0750.pixwall.core.watchFaceXml
import java.io.ByteArrayOutputStream
import java.io.File
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.PrivateKey
import java.security.cert.X509Certificate

/** 文字盤の名前。Watch Face Push の決まりで「このアプリの名前.watchfacepush.」で始める */
const val FACE_PACKAGE = "xyz.mooma0750.pixwall.watchfacepush.gif"

/** 入れた結果 */
sealed interface InstallResult {
    /** 入れて、今の文字盤になっている */
    data object Active : InstallResult

    /** 入れたが、今の文字盤ではない（本人が文字盤の一覧から選ぶ） */
    data object Installed : InstallResult

    /** 絵が大きすぎる（Play の審査と同じ数え方で 100MB を超える） */
    data class TooLarge(val megabytes: Long) : InstallResult

    /** 作った文字盤が検証を通らなかった・入れられなかった */
    data class Failed(val message: String) : InstallResult
}

/**
 * GIF と設定から文字盤の APK を作り、検証トークンを付けて Watch Face Push で入れる。
 * 土台（assets の watchface_template.apk。android/wearface の release のビルド）の GIF・見本・XML を差し替え、
 * 時計の中で作った鍵で署名する（Push で入れる文字盤は、アプリと別の鍵で署名する決まり）
 */
class WatchFaceMaker(private val context: Context) {
    suspend fun install(gif: GifImage, config: Config): InstallResult = withContext(Dispatchers.Default) {
        val art = renderWatchFace(gif, config)
        if (art.memoryBytes > WATCH_FACE_MEMORY_LIMIT) return@withContext InstallResult.TooLarge(art.memoryBytes / 1_000_000)

        val preview = ByteArrayOutputStream().also { out ->
            Bitmap.createBitmap(art.preview, art.size, art.size, Bitmap.Config.ARGB_8888).compress(Bitmap.CompressFormat.PNG, 100, out)
        }.toByteArray()
        val unsigned = File(context.cacheDir, "face-unsigned.apk")
        context.assets.open("watchface_template.apk").use { template ->
            unsigned.outputStream().use { out ->
                rewriteWatchFaceApk(template, out, watchFaceXml(art).toByteArray(), art.gif, preview)
            }
        }
        val signed = File(context.cacheDir, "face.apk")
        sign(unsigned, signed)
        unsigned.delete()

        val result = DwfValidatorFactory.create().validate(signed, context.packageName)
        if (result.failures().isNotEmpty()) {
            return@withContext InstallResult.Failed(result.failures().joinToString("\n") { "${it.name()}: ${it.failureMessage()}" })
        }
        try {
            push(signed, result.validationToken())
        } catch (e: Exception) {
            InstallResult.Failed(e.message ?: e.javaClass.simpleName)
        } finally {
            signed.delete()
        }
    }

    private suspend fun push(apk: File, token: String): InstallResult {
        val manager = WatchFacePushManagerFactory.createWatchFacePushManager(context)
        val list = manager.listWatchFaces()
        // 枠は Wear OS 6 では1つだけ。前に入れた pixwall の文字盤があれば、それを入れ替える
        val existing = list.installedWatchFaceDetails.firstOrNull { it.packageName == FACE_PACKAGE }
            ?: list.installedWatchFaceDetails.firstOrNull()
        // 入れ替えた直後に今の文字盤かを聞くと正しく答えないことがあるので、先に聞いておく
        val wasActive = existing != null && manager.isWatchFaceActive(existing.packageName)
        val details = ParcelFileDescriptor.open(apk, ParcelFileDescriptor.MODE_READ_ONLY).use { fd ->
            if (existing != null) manager.updateWatchFace(existing.slotId, fd, token) else manager.addWatchFace(fd, token)
        }
        if (wasActive) return InstallResult.Active
        // 今の文字盤にできるのは、本人が許したときの一度だけ。あとは本人が文字盤の一覧から選ぶ
        if (context.checkSelfPermission(SET_ACTIVE_PERMISSION) != PackageManager.PERMISSION_GRANTED) return InstallResult.Installed
        return runCatching { manager.setWatchFaceAsActive(details.slotId) }
            .fold(onSuccess = { InstallResult.Active }, onFailure = { InstallResult.Installed })
    }

    /** 時計の中の鍵（Android Keystore。取り出せない）で v2 の署名をする。鍵は初めて使うときに作る */
    private fun sign(input: File, output: File) {
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        if (!store.containsAlias(KEY_ALIAS)) {
            KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_RSA, "AndroidKeyStore").apply {
                initialize(
                    KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_SIGN)
                        .setKeySize(2048)
                        .setDigests(KeyProperties.DIGEST_SHA256)
                        .setSignaturePaddings(KeyProperties.SIGNATURE_PADDING_RSA_PKCS1)
                        .build(),
                )
            }.generateKeyPair()
        }
        val key = store.getKey(KEY_ALIAS, null) as PrivateKey
        val certificate = store.getCertificate(KEY_ALIAS) as X509Certificate
        ApkSigner.Builder(listOf(ApkSigner.SignerConfig.Builder("pixwall", key, listOf(certificate)).build()))
            .setInputApk(input)
            .setOutputApk(output)
            .setMinSdkVersion(33)
            .setV1SigningEnabled(false)
            .setV2SigningEnabled(true)
            .setV3SigningEnabled(false)
            .build()
            .sign()
    }

    companion object {
        const val SET_ACTIVE_PERMISSION = "com.google.wear.permission.SET_PUSHED_WATCH_FACE_AS_ACTIVE"
        private const val KEY_ALIAS = "watchface"
    }
}
