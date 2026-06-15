package com.phonecam.nativeapp

import android.content.Intent
import android.os.Bundle
import android.util.Log
import android.view.View
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity

/**
 * SettingsActivity —— phone_native/ Phase Y-1 设置页
 *
 * 来源: specs/features/app-architecture-B-multiscreen.md §4.2
 *
 * 范围 (9 项 + 1 跳转):
 *   - 5 项弹出选值 (默认摄像头/分辨率/帧率/码率/编码/传输方式 = 6 项, 但码率/编码/传输方式标 ⏳)
 *   - 2 项 Switch (PC 发现/显示调试信息)
 *   - 1 项跳转 (关于)
 *
 * 状态同步: 不在 onCreate 主动通知 MainActivity, 靠 MainActivity onResume 时
 *          重新读取 SettingsStore.
 */
class SettingsActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "SettingsActivity"
    }

    private lateinit var settings: SettingsStore

    // 9 行容器: (id, titleResId, kind, optionsResId)
    private data class RowDef(
        val rowId: Int,
        val titleResId: Int,
        val kind: Kind,
        val optionsResId: Int? = null,
        val isPlaceholder: Boolean = false
    )

    private enum class Kind { DROPDOWN, SWITCH, NAVIGATE }

    private lateinit var rows: List<RowDef>

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)

        settings = SettingsStore(this)

        // ActionBar: 标题 + 返回箭头
        supportActionBar?.apply {
            setDisplayHomeAsUpEnabled(true)
            title = getString(R.string.title_settings)
        }

        // 9 行定义 (按 activity_settings.xml 中的 include id 对应)
        rows = listOf(
            RowDef(R.id.rowLens,         R.string.settings_title_lens,        Kind.DROPDOWN, R.array.settings_opt_lens),
            RowDef(R.id.rowResolution,   R.string.settings_title_resolution,  Kind.DROPDOWN, R.array.settings_opt_resolution),
            RowDef(R.id.rowFps,          R.string.settings_title_fps,         Kind.DROPDOWN, R.array.settings_opt_fps),
            RowDef(R.id.rowBitrate,      R.string.settings_title_bitrate,     Kind.DROPDOWN, R.array.settings_opt_bitrate, isPlaceholder = true),
            RowDef(R.id.rowCodec,        R.string.settings_title_codec,       Kind.DROPDOWN, R.array.settings_opt_codec,   isPlaceholder = true),
            RowDef(R.id.rowTransport,    R.string.settings_title_transport,   Kind.DROPDOWN, R.array.settings_opt_transport, isPlaceholder = true),
            RowDef(R.id.rowPcDiscovery,  R.string.settings_title_pcdiscovery, Kind.SWITCH),
            RowDef(R.id.rowShowDebug,    R.string.settings_title_debug,       Kind.SWITCH),
            RowDef(R.id.rowConnect,      R.string.settings_title_connect,     Kind.NAVIGATE),
            RowDef(R.id.rowDebug,        R.string.settings_title_debugpage,   Kind.NAVIGATE),
            RowDef(R.id.rowAbout,        R.string.settings_title_about,       Kind.NAVIGATE)
        )

        bindRows()
        renderFooter()
    }

    /**
     * 绑定 9 行的 UI (title / value / placeholder / arrow / switch)
     * 并注册点击事件
     */
    private fun bindRows() {
        for (def in rows) {
            val row = findViewById<View>(def.rowId) ?: continue
            val title = row.findViewById<TextView>(R.id.rowTitle)
            val value = row.findViewById<TextView>(R.id.rowValue)
            val placeholder = row.findViewById<TextView>(R.id.rowPlaceholder)
            val arrow = row.findViewById<View>(R.id.rowArrow)
            val sw = row.findViewById<Switch>(R.id.rowSwitch)

            title.text = getString(def.titleResId)

            when (def.kind) {
                Kind.DROPDOWN -> {
                    arrow.visibility = View.VISIBLE
                    value.visibility = View.VISIBLE
                    placeholder.visibility = if (def.isPlaceholder) View.VISIBLE else View.GONE
                    if (def.isPlaceholder) {
                        placeholder.text = getString(R.string.settings_placeholder_stream)
                    }
                    val options = resources.getStringArray(def.optionsResId!!).toList()
                    value.text = currentValueFor(def.titleResId, options)

                    row.setOnClickListener {
                        showPicker(def.titleResId, def.optionsResId!!, def.isPlaceholder)
                    }
                }
                Kind.SWITCH -> {
                    arrow.visibility = View.GONE
                    value.visibility = View.GONE
                    placeholder.visibility = View.GONE
                    sw.visibility = View.VISIBLE
                    sw.isChecked = currentSwitchFor(def.titleResId)
                    sw.setOnCheckedChangeListener { _, checked ->
                        onSwitchChanged(def.titleResId, checked)
                    }
                }
                Kind.NAVIGATE -> {
                    arrow.visibility = View.VISIBLE
                    value.visibility = View.GONE
                    placeholder.visibility = View.GONE
                    row.setOnClickListener {
                        when (def.titleResId) {
                            R.string.settings_title_connect   -> startActivity(Intent(this, ConnectActivity::class.java))
                            R.string.settings_title_debugpage -> startActivity(Intent(this, DebugActivity::class.java))
                            R.string.settings_title_about     -> startActivity(Intent(this, AboutActivity::class.java))
                        }
                    }
                }
            }
        }
    }

    /**
     * 取当前行对应的存储值, 找不到时返回 options[0]
     */
    private fun currentValueFor(titleResId: Int, options: List<String>): String {
        val stored: String = when (titleResId) {
            R.string.settings_title_lens       -> settings.lens
            R.string.settings_title_resolution -> settings.resolution
            R.string.settings_title_fps         -> settings.fps
            R.string.settings_title_bitrate     -> settings.bitrate
            R.string.settings_title_codec       -> settings.codec
            R.string.settings_title_transport   -> settings.transport
            else -> options[0]
        }
        return if (options.contains(stored)) stored else options[0]
    }

    private fun currentSwitchFor(titleResId: Int): Boolean = when (titleResId) {
        R.string.settings_title_pcdiscovery -> settings.pcDiscovery
        R.string.settings_title_debug       -> settings.showDebug
        else -> false
    }

    private fun onSwitchChanged(titleResId: Int, checked: Boolean) {
        when (titleResId) {
            R.string.settings_title_pcdiscovery -> settings.pcDiscovery = checked
            R.string.settings_title_debug       -> settings.showDebug = checked
        }
        Toast.makeText(this, R.string.toast_settings_saved, Toast.LENGTH_SHORT).show()
    }

    /**
     * 弹单选对话框 (原生 AlertDialog, 不引 BottomSheet)
     */
    private fun showPicker(titleResId: Int, optionsResId: Int, isPlaceholder: Boolean) {
        val options = resources.getStringArray(optionsResId).toList()
        val title = getString(R.string.settings_dialog_title) + " — " + getString(titleResId)
        val currentValue = currentValueFor(titleResId, options)
        val currentIndex = options.indexOf(currentValue).coerceAtLeast(0)

        if (isPlaceholder) {
            // 待实现项: 不让改, 仅提示
            AlertDialog.Builder(this)
                .setTitle(getString(titleResId))
                .setMessage(R.string.settings_placeholder_stream)
                .setPositiveButton(android.R.string.ok, null)
                .show()
            return
        }

        AlertDialog.Builder(this)
            .setTitle(title)
            .setSingleChoiceItems(options.toTypedArray(), currentIndex) { dialog, which ->
                val chosen = options[which]
                applyChoice(titleResId, chosen)
                dialog.dismiss()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun applyChoice(titleResId: Int, value: String) {
        when (titleResId) {
            R.string.settings_title_lens       -> settings.lens = value
            R.string.settings_title_resolution -> settings.resolution = value
            R.string.settings_title_fps         -> settings.fps = value
            R.string.settings_title_bitrate     -> settings.bitrate = value
            R.string.settings_title_codec       -> settings.codec = value
            R.string.settings_title_transport   -> settings.transport = value
        }
        // 刷新当前行显示
        bindRows()
        Toast.makeText(this, R.string.toast_settings_saved, Toast.LENGTH_SHORT).show()
        InAppLogStore.d(TAG, "saved: $titleResId = $value")
    }

    private fun renderFooter() {
        val footer = findViewById<TextView>(R.id.footerVersion)
        val versionName = try {
            packageManager.getPackageInfo(packageName, 0).versionName ?: "0.0.0"
        } catch (e: Exception) {
            "0.0.0"
        }
        footer.text = "PhoneCam v$versionName"
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }
}
