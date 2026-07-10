plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val fifVersionName = rootProject.file("VERSION").readText().trim()
val fifVersionCode = rootProject.file("VERSION_CODE").readText().trim().toInt()

android {
    namespace = "com.fif.screen"
    compileSdk = 35

    buildFeatures {
        buildConfig = true
    }

    defaultConfig {
        applicationId = "com.fif.screen"
        minSdk = 26
        targetSdk = 35
        versionCode = fifVersionCode
        versionName = fifVersionName
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

kotlin {
    jvmToolchain(17)
}

dependencies {
    testImplementation("junit:junit:4.13.2")
}
