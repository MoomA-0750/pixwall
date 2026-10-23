// 時計（Wear OS 6 から）のアプリ。GIF と並べ方から文字盤を作り、Watch Face Push で入れる。
// Push で入れる文字盤は「このアプリの名前.watchfacepush.」で始める決まりなので、名前はスマホのアプリと同じにする
import com.android.build.api.variant.ApplicationAndroidComponentsExtension

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "xyz.mooma0750.pixwall.wear"
    compileSdk = libs.versions.compileSdk.get().toInt()

    defaultConfig {
        applicationId = "xyz.mooma0750.pixwall"
        // Watch Face Push は Wear OS 6（API 36）から
        minSdk = 36
        targetSdk = libs.versions.targetSdk.get().toInt()
        versionCode = 1
        versionName = "0.1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    buildFeatures {
        compose = true
    }

    packaging {
        resources.excludes += setOf("META-INF/DEPENDENCIES", "META-INF/LICENSE*", "META-INF/NOTICE*", "META-INF/*.kotlin_module")
    }
}

kotlin {
    jvmToolchain(21)
}

/** 文字盤の土台（android/wearface をビルドした APK）を assets に入れる。中の GIF と XML を時計の中で差し替える */
abstract class CopyWatchFaceTemplate : DefaultTask() {
    @get:InputFile
    abstract val template: RegularFileProperty

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun copy() {
        template.get().asFile.copyTo(outputDir.get().file("watchface_template.apk").asFile, overwrite = true)
    }
}

val copyWatchFaceTemplate = tasks.register<CopyWatchFaceTemplate>("copyWatchFaceTemplate") {
    dependsOn(":wearface:assembleRelease")
    template.set(project(":wearface").layout.buildDirectory.file("outputs/apk/release/wearface-release.apk"))
}

extensions.getByType<ApplicationAndroidComponentsExtension>().onVariants { variant ->
    variant.sources.assets?.addGeneratedSourceDirectory(copyWatchFaceTemplate, CopyWatchFaceTemplate::outputDir)
}

dependencies {
    implementation(project(":core"))
    implementation(libs.androidx.activity.compose)
    implementation(libs.wear.compose.material3)
    implementation(libs.wear.compose.foundation)
    implementation(libs.wear.watchfacepush)
    implementation(libs.wear.watchface.validator)
}
