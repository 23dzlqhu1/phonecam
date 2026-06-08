package com.phonecam.nativeapp

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/**
 * SettingsActivity —— 设置页 (Phase X 占位)
 *
 * 范围: 仅壳, 显示标题 + 一行占位文字
 * 后续 (Phase Y): 单层列表 - 分辨率 / 帧率 / 码率 / 编码器 / 协议格式
 */
class SettingsActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // 占位: 一行文字, Phase Y 会替换为单层列表
        val textView = TextView(this).apply {
            text = getString(R.string.title_settings) + " — Phase Y 填充"
            textSize = 18f
            setTextColor(getColor(R.color.text_primary))
            setBackgroundColor(getColor(R.color.bg_oled))
            setPadding(48, 96, 48, 48)
        }
        setContentView(textView)

        // 显示 ActionBar 标题 + 自动返回箭头
        supportActionBar?.apply {
            setDisplayHomeAsUpEnabled(true)
            title = getString(R.string.title_settings)
        }
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()  // 左上角返回箭头: 关闭当前 Activity 回到 Main
        return true
    }
}
