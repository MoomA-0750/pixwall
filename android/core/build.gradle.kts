// 並べ方・GIF の読み込み・設定の形。スマホの壁紙と時計の文字盤で共有する（Android の画面には触らない）
plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "xyz.mooma0750.pixwall.core"
    compileSdk = libs.versions.compileSdk.get().toInt()

    defaultConfig {
        // 時計（Wear OS 4 から）とスマホ（Android 12 から）の低いほう
        minSdk = libs.versions.minSdk.get().toInt()
    }
}

kotlin {
    jvmToolchain(21)
}

dependencies {
    testImplementation(libs.junit4)
    testImplementation(libs.org.json)
}
