package com.phonecam.nativeapp

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/**
 * ConnectActivity —— 连接 PC 页 (Phase X 占位)
 *
 * 范围: 仅壳, 显示标题 + 一行占位文字
 * 后续 (Phase Y): QR 码占位区 + IP 输入框 + 连接按钮
 */
class ConnectActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val textView = TextView(this).apply {
            text = getString(R.string.title_connect) + " — Phase Y 填充\n" + getString(R.string.connect_hint)
            textSize = 18f
            setTextColor(getColor(R.color.text_primary))
            setBackgroundColor(getColor(R.color.bg_oled))
            setPadding(48, 96, 48, 48)
        }
        setContentView(textView)

        supportActionBar?.apply {
            setDisplayHomeAsUpEnabled(true)
            title = getString(R.string.title_connect)
        }
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }
}
