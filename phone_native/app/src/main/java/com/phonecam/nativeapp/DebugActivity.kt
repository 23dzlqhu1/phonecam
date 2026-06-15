package com.phonecam.nativeapp

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.View
import android.widget.Button
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

/**
 * DebugActivity —— 调试 / 日志页 (Phase Y-4 完整)
 *
 * 来源: specs/features/app-architecture-B-multiscreen.md §4.5
 *
 * 范围 (本批次):
 *   - "日志" tab 完整实现: 实时显示 InAppLogStore 缓冲内容
 *   - 底部 3 按钮: 清空 / 复制 / 暂停自动滚动
 *   - "PCP" / "质量" tab: 点击 Toast "敬请期待"
 *   - 1Hz 自动刷新日志, 自动滚到底部 (暂停时停)
 *
 * 不做 (Phase Y+ 后续):
 *   - 按 TAG 过滤
 *   - 按等级过滤
 *   - 持久化日志
 *   - 真实 PCP/质量数据
 */
class DebugActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "DebugActivity"
        private const val REFRESH_INTERVAL_MS = 1000L
    }

    // 视图
    private lateinit var tabLogs: TextView
    private lateinit var tabPcp: TextView
    private lateinit var tabQuality: TextView
    private lateinit var logScroll: ScrollView
    private lateinit var logText: TextView
    private lateinit var btnClear: Button
    private lateinit var btnShare: Button
    private lateinit var btnPause: Button

    // 状态
    private var autoScroll: Boolean = true
    private var lastSize: Int = -1  // 用于判断是否需要重绘
    private val handler = Handler(Looper.getMainLooper())

    private val refreshRunnable = object : Runnable {
        override fun run() {
            refreshLogs()
            handler.postDelayed(this, REFRESH_INTERVAL_MS)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_debug)

        // ActionBar
        supportActionBar?.apply {
            setDisplayHomeAsUpEnabled(true)
            title = getString(R.string.title_debug)
        }

        bindViews()
        bindClickHandlers()
    }

    private fun bindViews() {
        tabLogs = findViewById(R.id.tabLogs)
        tabPcp = findViewById(R.id.tabPcp)
        tabQuality = findViewById(R.id.tabQuality)
        logScroll = findViewById(R.id.logScroll)
        logText = findViewById(R.id.logText)
        btnClear = findViewById(R.id.btnClear)
        btnShare = findViewById(R.id.btnShare)
        btnPause = findViewById(R.id.btnPause)
    }

    private fun bindClickHandlers() {
        tabPcp.setOnClickListener {
            Toast.makeText(this, "PCP 详情 — 敬请期待 (Phase Z)", Toast.LENGTH_SHORT).show()
        }
        tabQuality.setOnClickListener {
            Toast.makeText(this, "质量统计 — 敬请期待 (Phase Z)", Toast.LENGTH_SHORT).show()
        }
        tabLogs.setOnClickListener {
            // 当前就是日志 tab, 无操作
            Toast.makeText(this, "已是日志 tab", Toast.LENGTH_SHORT).show()
        }

        btnClear.setOnClickListener {
            InAppLogStore.clear()
            Toast.makeText(this, getString(R.string.debug_btn_clear) + " ✓", Toast.LENGTH_SHORT).show()
            refreshLogs()
        }

        btnShare.setOnClickListener { copyToClipboard() }

        btnPause.setOnClickListener { toggleAutoScroll() }
    }

    /**
     * 复制全部日志到剪贴板, 然后弹 Toast 提示
     */
    private fun copyToClipboard() {
        val snapshot = InAppLogStore.snapshot()
        if (snapshot.isEmpty()) {
            Toast.makeText(this, R.string.debug_log_empty, Toast.LENGTH_SHORT).show()
            return
        }
        val text = snapshot.joinToString("\n") { it.toString() }
        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        clipboard.setPrimaryClip(ClipData.newPlainText("PhoneCam Debug Log", text))
        Toast.makeText(this, "已复制 ${snapshot.size} 行", Toast.LENGTH_SHORT).show()
        Log.i(TAG, "copied ${snapshot.size} lines to clipboard")
    }

    /**
     * 切换自动滚动状态
     */
    private fun toggleAutoScroll() {
        autoScroll = !autoScroll
        btnPause.text = if (autoScroll) "暂停滚动" else "继续滚动"
        Log.d(TAG, "autoScroll=$autoScroll")
    }

    /**
     * 拉取最新日志并显示
     * 只在行数变化时重绘 (避免每 1s 滚动卡顿)
     */
    private fun refreshLogs() {
        val snapshot = InAppLogStore.snapshot()
        if (snapshot.size == lastSize) return
        lastSize = snapshot.size

        if (snapshot.isEmpty()) {
            logText.text = getString(R.string.debug_log_empty)
            return
        }
        logText.text = snapshot.joinToString("\n") { it.toString() }

        if (autoScroll) {
            logScroll.post {
                logScroll.fullScroll(View.FOCUS_DOWN)
            }
        }
    }

    override fun onResume() {
        super.onResume()
        Log.d(TAG, "onResume: start refresh timer")
        handler.post(refreshRunnable)
    }

    override fun onPause() {
        super.onPause()
        Log.d(TAG, "onPause: stop refresh timer")
        handler.removeCallbacks(refreshRunnable)
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }
}
