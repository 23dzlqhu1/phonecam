package com.phonecam.nativeapp

import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.util.AttributeSet
import android.util.TypedValue
import android.view.Gravity
import android.widget.LinearLayout
import android.widget.TextView

/**
 * DebugDashboardView — AI 可读的调试仪表盘
 *
 * 设计原则：
 * 1. 大字体 (18sp+) — Vision 模型识别率更高
 * 2. 高对比 — 黑底白/绿/红字，减少误读
 * 3. 结构化 — Key: Value 格式，AI 一眼定位
 * 4. 颜色编码 — 绿=正常，黄=警告，红=错误
 * 5. 信息分层 — 重要信息在上，细节在下
 */
class DebugDashboardView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : LinearLayout(context, attrs, defStyleAttr) {

    companion object {
        private const val TEXT_SIZE_TITLE = 22f
        private const val TEXT_SIZE_SECTION = 18f
        private const val TEXT_SIZE_VALUE = 20f
        private const val TEXT_SIZE_LABEL = 16f
        private const val PADDING_DP = 16
        private const val SECTION_SPACING_DP = 12
        private const val ROW_SPACING_DP = 4
    }

    // 状态行引用
    private val rows = mutableMapOf<String, StatusRow>()

    init {
        orientation = VERTICAL
        setBackgroundColor(Color.parseColor("#0A0A0F"))
        setPadding(dp(PADDING_DP), dp(PADDING_DP), dp(PADDING_DP), dp(PADDING_DP))

        // 标题
        addTitle("PHONECAM DEBUG DASHBOARD")
        addDivider()

        // CONNECTION 区块
        addSection("CONNECTION")
        addStatusRow("TCP Server", "IDLE")
        addStatusRow("PC Client", "DISCONNECTED")
        addStatusRow("ADB Forward", "UNKNOWN")

        addSpacer()

        // STREAMING 区块
        addSection("STREAMING")
        addStatusRow("Status", "IDLE")
        addStatusRow("Camera", "BACK")
        addStatusRow("Resolution", "—")
        addStatusRow("FPS", "—")
        addStatusRow("Bitrate", "—")
        addStatusRow("Frames", "0")

        addSpacer()

        // ENCODER 区块
        addSection("ENCODER")
        addStatusRow("Codec", "H.264")
        addStatusRow("Keyframes", "0")
        addStatusRow("NALUs", "0")

        addSpacer()

        // ORIENTATION 区块
        addSection("ORIENTATION")
        addStatusRow("Orient Lock", "OFF")

        addSpacer()

        // ERRORS 区块
        addSection("ERRORS")
        addStatusRow("Last Error", "(none)")
    }

    // ── 公开 API ──

    fun setTcpStatus(status: String) = setStatus("TCP Server", status)
    fun setPcStatus(status: String) = setStatus("PC Client", status)
    fun setAdbStatus(status: String) = setStatus("ADB Forward", status)
    fun setStreamingStatus(status: String) = setStatus("Status", status)
    fun setCamera(name: String) = setStatus("Camera", name)
    fun setResolution(w: Int, h: Int) = setStatus("Resolution", "${w}x${h}")
    fun setFps(actual: Int, target: Int) = setStatus("FPS", "$actual/$target")
    fun setBitrate(bps: Long) {
        val mbps = bps / 1_000_000.0
        setStatus("Bitrate", "%.1f Mbps".format(mbps))
    }
    fun setFrames(count: Int) = setStatus("Frames", count.toString())
    fun setKeyframes(count: Int) = setStatus("Keyframes", count.toString())
    fun setNalus(count: Int) = setStatus("NALUs", count.toString())
    fun setLastError(error: String?) = setStatus("Last Error", error ?: "(none)")

    fun setOrientLock(locked: Boolean, angle: Int) {
        setStatus("Orient Lock", if (locked) "ON ${angle}°" else "OFF")
    }

    fun setStatus(key: String, value: String) {
        rows[key]?.setValue(value)
    }

    // ── 内部构建方法 ──

    private fun addTitle(text: String) {
        val tv = TextView(context).apply {
            this.text = text
            setTextColor(Color.WHITE)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, TEXT_SIZE_TITLE)
            typeface = Typeface.MONOSPACE
            setTypeface(typeface, Typeface.BOLD)
            gravity = Gravity.CENTER
        }
        addView(tv)
    }

    private fun addSection(text: String) {
        val tv = TextView(context).apply {
            this.text = text
            setTextColor(Color.parseColor("#99FFFFFF"))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, TEXT_SIZE_SECTION)
            typeface = Typeface.MONOSPACE
            setTypeface(typeface, Typeface.BOLD)
            setPadding(0, dp(SECTION_SPACING_DP), 0, 0)
        }
        addView(tv)
    }

    private fun addStatusRow(label: String, initialValue: String): StatusRow {
        val row = StatusRow(context, label, initialValue)
        rows[label] = row
        addView(row)
        return row
    }

    private fun addDivider() {
        val divider = android.view.View(context).apply {
            layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, dp(1)).apply {
                topMargin = dp(8)
                bottomMargin = dp(8)
            }
            setBackgroundColor(Color.parseColor("#14FFFFFF"))
        }
        addView(divider)
    }

    private fun addSpacer() {
        val spacer = android.view.View(context).apply {
            layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, dp(SECTION_SPACING_DP))
        }
        addView(spacer)
    }

    private fun dp(value: Int): Int {
        return (value * context.resources.displayMetrics.density).toInt()
    }

    // ── 状态行内部类 ──

    inner class StatusRow(context: Context, label: String, initialValue: String) : LinearLayout(context) {
        private val labelView: TextView
        private val valueView: TextView

        init {
            orientation = HORIZONTAL
            setPadding(0, dp(ROW_SPACING_DP), 0, dp(ROW_SPACING_DP))

            labelView = TextView(context).apply {
                this.text = "├─ $label:"
                setTextColor(Color.parseColor("#99FFFFFF"))
                setTextSize(TypedValue.COMPLEX_UNIT_SP, TEXT_SIZE_LABEL)
                typeface = Typeface.MONOSPACE
                layoutParams = LayoutParams(0, LayoutParams.WRAP_CONTENT, 1f)
            }

            valueView = TextView(context).apply {
                this.text = initialValue
                setTextColor(Color.WHITE)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, TEXT_SIZE_VALUE)
                typeface = Typeface.MONOSPACE
                setTypeface(typeface, Typeface.BOLD)
                gravity = Gravity.END
            }

            addView(labelView)
            addView(valueView)
            setValue(initialValue)
        }

        fun setValue(value: String) {
            valueView.text = value
            valueView.setTextColor(getColorForValue(value))
        }

        private fun getColorForValue(value: String): Int {
            return when {
                value.contains("LISTENING") || value.contains("CONNECTED") || 
                value.contains("ACTIVE") || value.contains("OK") -> Color.parseColor("#00E676")  // 绿色
                value.contains("IDLE") || value.contains("UNKNOWN") || 
                value.contains("(none)") || value.contains("—") -> Color.parseColor("#99FFFFFF")  // 灰色
                value.contains("DISCONNECTED") || value.contains("ERROR") || 
                value.contains("FAILED") || value.contains("TIMEOUT") -> Color.parseColor("#FF3D5A")  // 红色
                value.contains("WAITING") || value.contains("CONNECTING") || 
                value.contains("RETRYING") -> Color.parseColor("#FFB300")  // 黄色
                else -> Color.WHITE  // 默认白色
            }
        }
    }
}
