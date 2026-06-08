package com.phonecam.nativeapp

import android.content.Context
import android.content.SharedPreferences

/**
 * SettingsStore —— phone_native/ 设置项统一读写入口
 *
 * 作用: 把 SharedPreferences 的 key/默认值集中到一处,
 *       避免 4 个 Activity 各自写 getSharedPreferences().getString() 散落各处.
 *
 * 范围 (Phase Y-1, 9 项 + 1 跳转):
 *   - 5 项真正生效: 默认摄像头/分辨率/目标帧率/PC 发现/显示调试信息
 *   - 3 项持久化但暂不生效 (推流待实现): 码率/编码/传输方式
 *   - 1 跳转: 关于 (无需持久化)
 *
 * 不做 (Phase Y 后续):
 *   - 设置变更广播 (暂时靠 onResume 重新读取)
 *   - 多 Profile 切换
 *   - 设置导入/导出
 */
class SettingsStore(context: Context) {

    companion object {
        private const val PREFS_NAME = "phonecam_settings"

        // Key 常量 (避免拼写错误)
        private const val KEY_LENS = "lens"                 // "back" / "front"
        private const val KEY_RESOLUTION = "resolution"     // "480p" / "720p" / "1080p"
        private const val KEY_FPS = "fps"                   // "15" / "30" / "60"
        private const val KEY_BITRATE = "bitrate"           // "1" / "2" / "4" / "8"
        private const val KEY_CODEC = "codec"               // "H.264" / "H.265" / "VP9"
        private const val KEY_TRANSPORT = "transport"       // "auto" / "wifi" / "usb"
        private const val KEY_PC_DISCOVERY = "pc_discovery"  // Boolean
        private const val KEY_SHOW_DEBUG = "show_debug"     // Boolean
        // Phase Y-5 加: 跨页表单状态持久化
        private const val KEY_LAST_IP = "last_ip"           // 用户上次输入的 IP
        private const val KEY_LAST_PORT = "last_port"       // 用户上次输入的端口

        // 默认值
        const val DEFAULT_LENS = "back"
        const val DEFAULT_RESOLUTION = "720p"
        const val DEFAULT_FPS = "30"
        const val DEFAULT_BITRATE = "2"
        const val DEFAULT_CODEC = "H.264"
        const val DEFAULT_TRANSPORT = "auto"
        const val DEFAULT_PC_DISCOVERY = true
        const val DEFAULT_SHOW_DEBUG = true
        const val DEFAULT_LAST_IP = ""      // 首次打开无 IP
        const val DEFAULT_LAST_PORT = "7878"
    }

    private val prefs: SharedPreferences =
        context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    // ============== 读 ==============

    var lens: String
        get() = prefs.getString(KEY_LENS, DEFAULT_LENS) ?: DEFAULT_LENS
        set(value) = prefs.edit().putString(KEY_LENS, value).apply()

    var resolution: String
        get() = prefs.getString(KEY_RESOLUTION, DEFAULT_RESOLUTION) ?: DEFAULT_RESOLUTION
        set(value) = prefs.edit().putString(KEY_RESOLUTION, value).apply()

    var fps: String
        get() = prefs.getString(KEY_FPS, DEFAULT_FPS) ?: DEFAULT_FPS
        set(value) = prefs.edit().putString(KEY_FPS, value).apply()

    var bitrate: String
        get() = prefs.getString(KEY_BITRATE, DEFAULT_BITRATE) ?: DEFAULT_BITRATE
        set(value) = prefs.edit().putString(KEY_BITRATE, value).apply()

    var codec: String
        get() = prefs.getString(KEY_CODEC, DEFAULT_CODEC) ?: DEFAULT_CODEC
        set(value) = prefs.edit().putString(KEY_CODEC, value).apply()

    var transport: String
        get() = prefs.getString(KEY_TRANSPORT, DEFAULT_TRANSPORT) ?: DEFAULT_TRANSPORT
        set(value) = prefs.edit().putString(KEY_TRANSPORT, value).apply()

    var pcDiscovery: Boolean
        get() = prefs.getBoolean(KEY_PC_DISCOVERY, DEFAULT_PC_DISCOVERY)
        set(value) = prefs.edit().putBoolean(KEY_PC_DISCOVERY, value).apply()

    var showDebug: Boolean
        get() = prefs.getBoolean(KEY_SHOW_DEBUG, DEFAULT_SHOW_DEBUG)
        set(value) = prefs.edit().putBoolean(KEY_SHOW_DEBUG, value).apply()

    // ============== 跨页表单状态 (Phase Y-5 加) ==============

    /** 上次输入的 PC IP (用于 Connect 页回填) */
    var lastIp: String
        get() = prefs.getString(KEY_LAST_IP, DEFAULT_LAST_IP) ?: DEFAULT_LAST_IP
        set(value) = prefs.edit().putString(KEY_LAST_IP, value).apply()

    /** 上次输入的 PC 端口 (用于 Connect 页回填) */
    var lastPort: String
        get() = prefs.getString(KEY_LAST_PORT, DEFAULT_LAST_PORT) ?: DEFAULT_LAST_PORT
        set(value) = prefs.edit().putString(KEY_LAST_PORT, value).apply()

    // ============== 辅助: 索引 ↔ 值映射 ==============
    // 用于 ArrayAdapter 选 index 持久化时存原始字符串

    /** 把 "480p" 这样的字符串在 [options] 里的 index 找出来, 找不到返回 0 */
    fun indexOf(options: List<String>, value: String): Int {
        val idx = options.indexOf(value)
        return if (idx >= 0) idx else 0
    }
}
