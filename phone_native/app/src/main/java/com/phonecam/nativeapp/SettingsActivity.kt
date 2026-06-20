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
 * SettingsActivity — 设置页
 *
 * 存储 canonical value (back/front/30/auto/wifi/usb)，UI 显示中文 label。
 * 通过 settings_val_xxx 和 settings_opt_xxx 两个平行数组做映射。
 */
class SettingsActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "SettingsActivity"
    }

    private lateinit var settings: SettingsStore

    private data class RowDef(
        val rowId: Int,
        val titleResId: Int,
        val kind: Kind,
        val optionsResId: Int? = null,   // 显示值数组
        val valuesResId: Int? = null,    // 存储值数组（与 options 1:1 对应）
        val isPlaceholder: Boolean = false
    )

    private enum class Kind { DROPDOWN, SWITCH, NAVIGATE }

    private lateinit var rows: List<RowDef>

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)

        settings = SettingsStore(this)

        supportActionBar?.apply {
            setDisplayHomeAsUpEnabled(true)
            title = getString(R.string.title_settings)
        }

        rows = listOf(
            RowDef(R.id.rowLens,         R.string.settings_title_lens,        Kind.DROPDOWN, R.array.settings_opt_lens,       R.array.settings_val_lens),
            RowDef(R.id.rowResolution,   R.string.settings_title_resolution,  Kind.DROPDOWN, R.array.settings_opt_resolution, null),  // 显示值 = 存储值
            RowDef(R.id.rowFps,          R.string.settings_title_fps,         Kind.DROPDOWN, R.array.settings_opt_fps,        R.array.settings_val_fps),
            RowDef(R.id.rowBitrate,      R.string.settings_title_bitrate,     Kind.DROPDOWN, R.array.settings_opt_bitrate,    R.array.settings_val_bitrate, isPlaceholder = true),
            RowDef(R.id.rowCodec,        R.string.settings_title_codec,       Kind.DROPDOWN, R.array.settings_opt_codec,      null, isPlaceholder = true),
            RowDef(R.id.rowTransport,    R.string.settings_title_transport,   Kind.DROPDOWN, R.array.settings_opt_transport,  R.array.settings_val_transport, isPlaceholder = true),
            RowDef(R.id.rowPcDiscovery,  R.string.settings_title_pcdiscovery, Kind.SWITCH),
            RowDef(R.id.rowShowDebug,    R.string.settings_title_debug,       Kind.SWITCH),
            RowDef(R.id.rowConnect,      R.string.settings_title_connect,     Kind.NAVIGATE),
            RowDef(R.id.rowDebug,        R.string.settings_title_debugpage,   Kind.NAVIGATE),
            RowDef(R.id.rowAbout,        R.string.settings_title_about,       Kind.NAVIGATE)
        )

        bindRows()
        renderFooter()
    }

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
                        placeholder.text = getString(
                            if (def.titleResId == R.string.settings_title_transport)
                                R.string.settings_placeholder_transport
                            else
                                R.string.settings_placeholder_stream
                        )
                    }
                    val displayOptions = resources.getStringArray(def.optionsResId!!).toList()
                    val storedCanonical = storedValueFor(def.titleResId)
                    val displayLabel = canonicalToDisplay(storedCanonical, displayOptions, def.valuesResId)
                    value.text = displayLabel

                    row.setOnClickListener {
                        showPicker(def)
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

    /** 读取 SettingsStore 中的 canonical value */
    private fun storedValueFor(titleResId: Int): String = when (titleResId) {
        R.string.settings_title_lens       -> settings.lens
        R.string.settings_title_resolution -> settings.resolution
        R.string.settings_title_fps         -> settings.fps
        R.string.settings_title_bitrate     -> settings.bitrate
        R.string.settings_title_codec       -> settings.codec
        R.string.settings_title_transport   -> settings.transport
        else -> ""
    }

    /**
     * 把 canonical value 转为显示 label。
     * 如果有 valuesResId，通过 index 映射；否则 display = canonical。
     */
    private fun canonicalToDisplay(canonical: String, displayOptions: List<String>, valuesResId: Int?): String {
        if (valuesResId == null) {
            // 显示值 = 存储值（如 resolution: "720p"）
            return if (displayOptions.contains(canonical)) canonical else displayOptions[0]
        }
        val canonicalValues = resources.getStringArray(valuesResId).toList()
        val idx = canonicalValues.indexOf(canonical)
        return if (idx >= 0 && idx < displayOptions.size) displayOptions[idx] else displayOptions[0]
    }

    /**
     * 把显示 index 转为 canonical value 存储。
     * 如果有 valuesResId，取 values[index]；否则直接存 displayOptions[index]。
     */
    private fun displayIndexToCanonical(index: Int, def: RowDef): String {
        if (def.valuesResId == null) {
            val displayOptions = resources.getStringArray(def.optionsResId!!).toList()
            return displayOptions[index]
        }
        val canonicalValues = resources.getStringArray(def.valuesResId).toList()
        return if (index < canonicalValues.size) canonicalValues[index] else canonicalValues[0]
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

    private fun showPicker(def: RowDef) {
        val displayOptions = resources.getStringArray(def.optionsResId!!).toList()
        val title = getString(R.string.settings_dialog_title) + " — " + getString(def.titleResId)
        val storedCanonical = storedValueFor(def.titleResId)
        val currentDisplay = canonicalToDisplay(storedCanonical, displayOptions, def.valuesResId)
        val currentIndex = displayOptions.indexOf(currentDisplay).coerceAtLeast(0)

        if (def.isPlaceholder) {
            val placeholderMsg = if (def.titleResId == R.string.settings_title_transport) {
                R.string.settings_placeholder_transport
            } else {
                R.string.settings_placeholder_stream
            }
            AlertDialog.Builder(this)
                .setTitle(getString(def.titleResId))
                .setMessage(placeholderMsg)
                .setPositiveButton(android.R.string.ok, null)
                .show()
            return
        }

        AlertDialog.Builder(this)
            .setTitle(title)
            .setSingleChoiceItems(displayOptions.toTypedArray(), currentIndex) { dialog, which ->
                val canonicalValue = displayIndexToCanonical(which, def)
                applyChoice(def.titleResId, canonicalValue)
                dialog.dismiss()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun applyChoice(titleResId: Int, canonicalValue: String) {
        when (titleResId) {
            R.string.settings_title_lens       -> settings.lens = canonicalValue
            R.string.settings_title_resolution -> settings.resolution = canonicalValue
            R.string.settings_title_fps         -> settings.fps = canonicalValue
            R.string.settings_title_bitrate     -> settings.bitrate = canonicalValue
            R.string.settings_title_codec       -> settings.codec = canonicalValue
            R.string.settings_title_transport   -> settings.transport = canonicalValue
        }
        bindRows()
        Toast.makeText(this, R.string.toast_settings_saved, Toast.LENGTH_SHORT).show()
        InAppLogStore.d(TAG, "saved: $titleResId = $canonicalValue")
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
