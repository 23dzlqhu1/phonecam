// build.gradle.kts — phone_native/ 顶层工程脚本
// 作用：声明所有子项目都用到的插件版本（apply false = 不在这里应用，只声明）

plugins {
    id("com.android.application") version "8.11.1" apply false
    id("org.jetbrains.kotlin.android") version "2.2.20" apply false
}
