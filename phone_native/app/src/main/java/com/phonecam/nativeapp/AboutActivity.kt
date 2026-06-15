package com.phonecam.nativeapp

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.util.Log
import android.view.View
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

/**
 * AboutActivity —— 关于页 (Phase Y-3 完整)
 *
 * 来源: specs/features/app-architecture-B-multiscreen.md §4.4
 *
 * 范围 (本批次):
 *   - 顶部: 大图标 + 应用名 + 版本号 + 一句话介绍
 *   - 中部: 作者 / 仓库 / 许可
 *   - 跳转行: 开源许可证 / 问题反馈 / 使用文档 (3 个)
 *   - 底部: 版权
 *
 * 不做 (Phase Y+ 后续):
 *   - 开源许可证子页 (点击先 Toast 占位)
 */
class AboutActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "AboutActivity"
        private const val GITHUB_REPO_URL = "https://github.com/23dzlqhu1/PhoneCam"
        private const val GITHUB_ISSUES_URL = "https://github.com/23dzlqhu1/PhoneCam/issues"
        private const val DOCS_URL = "https://github.com/23dzlqhu1/PhoneCam/tree/main/specs"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_about)

        // ActionBar
        supportActionBar?.apply {
            setDisplayHomeAsUpEnabled(true)
            title = getString(R.string.title_about)
        }

        renderVersion()
        bindClickHandlers()
    }

    /**
     * 显示真实 versionName (动态读取, 不写死)
     */
    private fun renderVersion() {
        val versionName = try {
            packageManager.getPackageInfo(packageName, 0).versionName ?: "0.0.0"
        } catch (e: Exception) {
            Log.w(TAG, "getPackageInfo failed: ${e.message}")
            "0.0.0"
        }
        findViewById<TextView>(R.id.aboutVersion).text = "v$versionName"
    }

    /**
     * 3 个跳转行: 全部打开浏览器 (Android 默认 Action_VIEW 行为)
     */
    private fun bindClickHandlers() {
        findViewById<View>(R.id.rowLicense).setOnClickListener {
            Toast.makeText(this, "开源许可证 — Phase Y+ 后续实现", Toast.LENGTH_SHORT).show()
        }
        findViewById<View>(R.id.rowIssues).setOnClickListener {
            openUrl(GITHUB_ISSUES_URL, "问题反馈")
        }
        findViewById<View>(R.id.rowDocs).setOnClickListener {
            openUrl(DOCS_URL, "使用文档")
        }
    }

    /**
     * 用 Intent.ACTION_VIEW 打开外部 URL
     * 失败时 (无浏览器) Toast 提示
     */
    private fun openUrl(url: String, label: String) {
        try {
            val intent = Intent(Intent.ACTION_VIEW, Uri.parse(url))
            startActivity(intent)
            Log.d(TAG, "openUrl: $label -> $url")
        } catch (e: Exception) {
            Log.w(TAG, "openUrl failed ($label): ${e.message}")
            Toast.makeText(this, "无法打开 $label", Toast.LENGTH_SHORT).show()
        }
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }
}
