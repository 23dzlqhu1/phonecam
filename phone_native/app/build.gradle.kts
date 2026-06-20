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
        versionCode = 17                          // 批次 3.2.0.3h: +setTargetFps fix
        versionName = "0.2.8-mvp2-batch3.2.0.3h-portfix3"
    }

    signingConfigs {
        create("release") {
            storeFile = file("../../phonecam-release.jks")
            storePassword = "phonecam123"
            keyAlias = "phonecam"
            keyPassword = "phonecam123"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("release")
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
    // MVP-2 批次 3：Camera2 来自 Android Framework 自带（android.hardware.camera2.*），
    // 不引入 androidx.camera；权限申请用原生 checkSelfPermission / requestPermissions
    // 不引入 androidx.core（用 AppCompatActivity 自带的 Activity 基类即可）
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.cardview:cardview:1.0.0")
}
