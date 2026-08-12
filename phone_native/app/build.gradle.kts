// app/build.gradle.kts — phone_native/ 的 :app 模块构建脚本
// 作用：声明 Android App 的所有编译参数

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.phonecam.nativeapp"          // R 类包名（与 applicationId 区分）
    compileSdk = 34                               // Android 14 API

    // 8月9日修复 A: 生成 BuildConfig (VERSION_NAME/VERSION_CODE), 供 DiscoveryResponder 报告 App 版本
    buildFeatures {
        buildConfig = true
    }

    defaultConfig {
        applicationId = "com.phonecam.nativeapp"  // 装机后包名（与旧 phone/ 的 com.phonecam.phone 区分）
        minSdk = 24                               // Android 7.0+ 覆盖 99% 设备
        targetSdk = 34                            // Android 14
        versionCode = 18                          // v2.0.2 release: 后台保活 + UDP discovery
        versionName = "0.2.9"
    }

    // 安全修复：签名信息不再硬编码，改从环境变量读取。
    // 发布 release 包前，请在终端/CI 中设置以下环境变量：
    //   PHONECAM_STORE_FILE      → keystore 文件绝对路径，例如 C:\Users\xxx\.android\phonecam-release.jks
    //   PHONECAM_STORE_PASSWORD  → keystore 密码
    //   PHONECAM_KEY_ALIAS       → key alias
    //   PHONECAM_KEY_PASSWORD    → key 密码
    val storeFileEnv = providers.environmentVariable("PHONECAM_STORE_FILE").orNull
    val storePasswordEnv = providers.environmentVariable("PHONECAM_STORE_PASSWORD").orNull
    val keyAliasEnv = providers.environmentVariable("PHONECAM_KEY_ALIAS").orNull
    val keyPasswordEnv = providers.environmentVariable("PHONECAM_KEY_PASSWORD").orNull

    val hasReleaseSigning = !storeFileEnv.isNullOrBlank() &&
            !storePasswordEnv.isNullOrBlank() &&
            !keyAliasEnv.isNullOrBlank() &&
            !keyPasswordEnv.isNullOrBlank()
    val releaseRequested = gradle.startParameter.taskNames.any {
        it.contains("release", ignoreCase = true)
    }
    if (releaseRequested && !hasReleaseSigning) {
        throw GradleException(
            "Release signing is required. Set PHONECAM_STORE_FILE, " +
                "PHONECAM_STORE_PASSWORD, PHONECAM_KEY_ALIAS and PHONECAM_KEY_PASSWORD."
        )
    }
    if (releaseRequested && !file(storeFileEnv!!).isFile) {
        throw GradleException("PHONECAM_STORE_FILE does not point to an existing keystore file.")
    }

    signingConfigs {
        if (hasReleaseSigning) {
            create("release") {
                storeFile = file(storeFileEnv!!)
                storePassword = storePasswordEnv!!
                keyAlias = keyAliasEnv!!
                keyPassword = keyPasswordEnv!!
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            if (hasReleaseSigning) signingConfig = signingConfigs.getByName("release")
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
