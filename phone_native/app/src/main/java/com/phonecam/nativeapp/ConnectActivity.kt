package com.phonecam.nativeapp

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/**
 * ConnectActivity — 热点模式连接页面
 *
 * 手机是服务器（TcpStreamServer 监听 0.0.0.0:9999），
 * PC 连接手机热点后自动发现网关 IP 并连接。
 *
 * 本页面只显示状态，不做连接逻辑。
 */
class ConnectActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "ConnectActivity"
    }

    private lateinit var statusText: TextView
    private lateinit var hintText: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_connect)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        supportActionBar?.title = "连接状态"

        statusText = findViewById(R.id.statusBigText)
        hintText = findViewById(R.id.statusBigHint)

        updateStatus()
    }

    override fun onResume() {
        super.onResume()
        updateStatus()
    }

    private fun updateStatus() {
        val snap = StreamingService.getStateSnapshot()

        if (snap.isActive) {
            statusText.text = "推流中"
            hintText.text = buildString {
                append("PC 已连接，视频流传输中\n\n")
                append("分辨率: ${snap.cameraWidth}x${snap.cameraHeight}\n")
                append("帧率: ~${if (snap.frameSubmitCount > 0 && snap.startTimeMs > 0) {
                    val elapsed = (System.currentTimeMillis() - snap.startTimeMs) / 1000.0
                    if (elapsed > 0) String.format("%.0f", snap.frameSubmitCount / elapsed) else "..."
                } else "..."} fps")
            }
        } else if (snap.isStarting) {
            statusText.text = "等待 PC 连接..."
            hintText.text = "TcpStreamServer 已启动，等待电脑端连接\n\n" +
                "请确保:\n" +
                "1. 手机已开热点\n" +
                "2. 电脑已连接手机热点\n" +
                "3. 电脑端 PhoneCam 已启动"
        } else {
            statusText.text = "未推流"
            hintText.text = "请先在主页面点击「开始推流」"
        }

        Log.d(TAG, "status: active=${snap.isActive} starting=${snap.isStarting}")
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }
}
