package com.phonecam.nativeapp

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/**
 * AboutActivity —— 关于页 (Phase X 占位)
 *
 * 范围: 仅壳, 显示标题 + 一行占位文字
 * 后续 (Phase Y): 版本号 / 介绍 / 版权 / 开源链接
 */
class AboutActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val textView = TextView(this).apply {
            text = getString(R.string.title_about) + " — Phase Y 填充\n" + getString(R.string.about_intro)
            textSize = 18f
            setTextColor(getColor(R.color.text_primary))
            setBackgroundColor(getColor(R.color.bg_oled))
            setPadding(48, 96, 48, 48)
        }
        setContentView(textView)

        supportActionBar?.apply {
            setDisplayHomeAsUpEnabled(true)
            title = getString(R.string.title_about)
        }
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }
}
