// Wear OS の文字盤（Watch Face Format）。WFF の文字盤はコードを持てず、Play にはアプリとは別の束で出す決まりなので、別のモジュールにする
plugins {
    alias(libs.plugins.android.application)
}

android {
    enableKotlin = false
    namespace = "xyz.mooma0750.pixwall.watchface"
    compileSdk = libs.versions.compileSdk.get().toInt()

    defaultConfig {
        // Watch Face Push で入れ替えるときは、文字盤の名前を「入れるアプリの名前.watchfacepush.」で始める決まりがある
        applicationId = "xyz.mooma0750.pixwall.watchfacepush.gif"
        // WFF の版 1 は Wear OS 4（API 33）から
        minSdk = 33
        targetSdk = libs.versions.targetSdk.get().toInt()
        versionCode = 1
        versionName = "0.1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            // 縮めるとき、XML からしか呼ばれない画像を消さないようにする
            isShrinkResources = false
            signingConfig = signingConfigs.getByName("debug")
        }
    }
}
