package com.phonecam.nativeapp

import android.util.Log
import java.text.SimpleDateFormat
import java.util.Collections
import java.util.Date
import java.util.Locale

/**
 * InAppLogStore —— phone_native/ 内存日志缓冲 (Phase Y-4 加)
 *
 * 作用: 让任何类 (Camera / Activity / Phase Z 的推流) 都能
 *       1) Log.d/i/w/e 到 logcat (不变)
 *       2) 同时也写到这块内存 (新), 供 DebugActivity 滚动展示
 *
 * 范围:
 *   - 内存环形缓冲, 最多 500 行 (FIFO)
 *   - 单例 (进程内全局), 不持久化
 *   - 时间戳格式 HH:mm:ss.SSS
 *   - 等级 I/D/W/E
 *
 * 不做 (Phase Y+ 后续):
 *   - 持久化到文件
 *   - 远程上报
 *   - 按 TAG 过滤 (DebugActivity 一次性全显示)
 */
object InAppLogStore {

    private const val TAG = "InAppLogStore"
    private const val MAX_LINES = 500

    /** 单行日志 */
    data class Entry(
        val timestamp: String,  // "HH:mm:ss.SSS"
        val level: Char,         // I/D/W/E
        val tag: String,
        val message: String
    ) {
        override fun toString(): String = "$timestamp  $level  $tag: $message"
    }

    private val lines: MutableList<Entry> =
        Collections.synchronizedList(ArrayList<Entry>())
    private val timeFormat = SimpleDateFormat("HH:mm:ss.SSS", Locale.US)

    /**
     * 写入一条日志 (供业务代码调)
     * level: I (info) / D (debug) / W (warn) / E (error)
     */
    fun append(level: Char, tag: String, message: String) {
        val entry = Entry(
            timestamp = timeFormat.format(Date()),
            level = level,
            tag = tag,
            message = message
        )
        synchronized(lines) {
            lines.add(entry)
            // FIFO: 超出上限就丢最早的
            while (lines.size > MAX_LINES) {
                lines.removeAt(0)
            }
        }
    }

    /** 取当前所有日志 (深拷贝) */
    fun snapshot(): List<Entry> = synchronized(lines) { lines.toList() }

    /** 清空所有日志 */
    fun clear() {
        synchronized(lines) { lines.clear() }
    }

    /** 当前行数 */
    fun size(): Int = synchronized(lines) { lines.size }

    // ============= 便利包装 (业务代码用起来更顺手) =============

    fun d(tag: String, msg: String) {
        Log.d(tag, msg)
        append('D', tag, msg)
    }

    fun i(tag: String, msg: String) {
        Log.i(tag, msg)
        append('I', tag, msg)
    }

    fun w(tag: String, msg: String) {
        Log.w(tag, msg)
        append('W', tag, msg)
    }

    fun e(tag: String, msg: String) {
        Log.e(tag, msg)
        append('E', tag, msg)
    }
}
