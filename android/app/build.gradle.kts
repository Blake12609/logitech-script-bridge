plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

// CI passes the release tag (e.g. v1.6.0) and its run number.
val appVersion = (System.getenv("VERSION") ?: "").removePrefix("v").ifEmpty { "1.6.0" }
val appVersionCode = System.getenv("GITHUB_RUN_NUMBER")?.toIntOrNull() ?: 1

android {
    namespace = "io.github.blake12609.scriptbridge"
    compileSdk = 35

    defaultConfig {
        applicationId = "io.github.blake12609.scriptbridge"
        minSdk = 26
        targetSdk = 35
        versionCode = appVersionCode
        versionName = appVersion
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64") }
        externalNativeBuild { cmake { arguments += "-DANDROID_STL=c++_static" } }
    }

    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt") } }
    // use the NDK that's already installed (GitHub's runners set this) instead of downloading one
    System.getenv("ANDROID_NDK_HOME")?.let { if (file(it).isDirectory) ndkPath = it }

    signingConfigs {
        create("release") {
            // A keystore from CI secrets if one is set up, otherwise the project's own
            // key (release.jks), so each release installs over the previous one.
            storeFile = file(System.getenv("ANDROID_KEYSTORE") ?: "release.jks")
            storePassword = System.getenv("ANDROID_KEYSTORE_PASSWORD") ?: "scriptbridge"
            keyAlias = System.getenv("ANDROID_KEY_ALIAS") ?: "scriptbridge"
            keyPassword = System.getenv("ANDROID_KEY_PASSWORD") ?: "scriptbridge"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.getByName("release")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    buildFeatures {
        compose = true
        buildConfig = true
    }
    // the example scripts ship inside the app
    sourceSets["main"].assets.srcDir("../../examples")
}

dependencies {
    implementation(platform("androidx.compose:compose-bom:2024.12.01"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("com.github.mik3y:usb-serial-for-android:3.8.1")
}
