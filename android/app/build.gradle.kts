plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "xyz.mooma0750.pixwall"
    compileSdk = libs.versions.compileSdk.get().toInt()

    defaultConfig {
        applicationId = "xyz.mooma0750.pixwall"
        minSdk = libs.versions.minSdk.get().toInt()
        targetSdk = libs.versions.targetSdk.get().toInt()
        versionCode = 1
        versionName = "0.1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            // 自分の端末に入れるだけなので、デバッグの鍵で署名する
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    buildFeatures {
        compose = true
    }
}

kotlin {
    jvmToolchain(21)
    compilerOptions {
        // 画面は Material 3 Expressive で作る（AGENTS.md）。Expressive の部品はまだ実験扱いなので、モジュール全体で opt-in する
        optIn.add("androidx.compose.material3.ExperimentalMaterial3ExpressiveApi")
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.material3)

    testImplementation(libs.junit4)
    testImplementation(libs.org.json)
}
