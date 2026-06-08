package com.phonecam.nativeapp

import android.graphics.Color
import android.os.Bundle
import android.view.Gravity
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/**
 * MainActivity — phone_native/ 批次 2 最小 Activity
 *
 * 作用：真机跑通 "PhoneCam MVP-2 ready" 文本显示，验证 Kotlin 原生工程链路。
 *
 * 设计：使用 programmatic 方式创建 TextView（不用 XML 布局），最小化资源依赖：
 *   - 不依赖 activity_main.xml
 *   - 不依赖任何 Material3 主题
 *   - 只用 AppCompatActivity + setContentView(View)
 *
 * 后续批次会替换为本相机预览界面（批次 3）、编码界面（批次 4）等。
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // 创建根布局（垂直方向）
        val rootLayout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
            setBackgroundColor(Color.WHITE)
        }

        // 创建主文本
        val titleView = TextView(this).apply {
            text = "PhoneCam MVP-2 ready"
            textSize = 24f
            setTextColor(Color.BLACK)
            gravity = Gravity.CENTER
        }

        // 创建副文本（状态信息）
        val statusView = TextView(this).apply {
            text = buildString {
                append("package: com.phonecam.nativeapp\n")
                append("build: 0.1.0-mvp2-batch2\n")
                append("agp: 8.11.1 / kotlin: 2.2.20")
            }
            textSize = 12f
            setTextColor(Color.DKGRAY)
            gravity = Gravity.CENTER
        }

        rootLayout.addView(titleView)
        rootLayout.addView(statusView)
        setContentView(rootLayout)
    }
}
