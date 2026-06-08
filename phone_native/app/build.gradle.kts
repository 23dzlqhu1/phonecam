// app/build.gradle.kts — phone_native/ 的 :app 模块构建脚本
// 作用：声明 Android App 的所有编译参数

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.phonecam.nativeapp"          // R 类包名（与 applicationId 区分）
    compileSdk = 34                               // Android 14 API

    defaultConfig {
        applicationId = "com.phonecam.nativeapp"  // 装机后包名（与旧 phone/ 的 com.phonecam.phone 区分）
        minSdk = 24                               // Android 7.0+ 覆盖 99% 设备
        targetSdk = 34                            // Android 14
        versionCode = 1
        versionName = "0.1.0-mvp2-batch2"
    }

    buildTypes {
        release {
            isMinifyEnabled = false               // MVP 阶段不上 R8 压缩
        }
        debug {
            isDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    // MVP-2 批次 2 只需要 AndroidX core + AppCompat（让 themes.xml 不报错）
    // 不加 Material3 依赖（避免版本兼容问题）
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
}
