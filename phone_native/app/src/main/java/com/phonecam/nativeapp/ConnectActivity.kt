package com.phonecam.nativeapp

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.os.Bundle
import android.util.Log
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import java.net.Inet4Address
import java.net.NetworkInterface

/**
 * ConnectActivity — 连接方式页 (P2-1 enhanced)
 *
 * 显示手机 IP:端口、连接方式说明、复制按钮。
 * 手机是服务器（TcpStreamServer 监听 0.0.0.0:9999），
 * PC 通过 USB/WiFi/热点 连接手机。
 */
class ConnectActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "ConnectActivity"
        private const val PORT = 9999
    }

    private lateinit var statusText: TextView
    private lateinit var hintText: TextView
    private lateinit var ipText: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_connect)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        supportActionBar?.title = "连接方式"

        statusText = findViewById(R.id.statusBigText)
        hintText = findViewById(R.id.statusBigHint)
        ipText = findViewById(R.id.connectIpText)

        // P2-1: Copy button for IP:port
        val copyBtn = findViewById<Button>(R.id.btnCopyIp)
        copyBtn.setOnClickListener {
            val ip = getLocalIpAddress()
            if (ip != null) {
                val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                val clip = ClipData.newPlainText("PhoneCam Address", "$ip:$PORT")
                clipboard.setPrimaryClip(clip)
                Toast.makeText(this, "已复制 $ip:$PORT", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(this, "未检测到 IP 地址", Toast.LENGTH_SHORT).show()
            }
        }

        updateStatus()
    }

    override fun onResume() {
        super.onResume()
        updateStatus()
    }

    private fun updateStatus() {
        val snap = StreamingService.getStateSnapshot()
        val phoneIp = getLocalIpAddress()

        // P2-1: Enhanced IP display with connection instructions
        if (phoneIp != null) {
            ipText.text = "手机 IP: $phoneIp  端口: $PORT"
        } else {
            ipText.text = "未检测到 WiFi/热点 IP\n请开启热点或连接同一 WiFi"
        }

        when (snap.streamState) {
            StreamingService.Companion.StreamState.STREAMING -> {
                statusText.text = "推流中"
                hintText.text = buildString {
                    append("PC 已连接，视频流传输中\n\n")
                    append("分辨率: ${snap.cameraWidth}×${snap.cameraHeight}\n")
                    val elapsed = if (snap.startTimeMs > 0) {
                        (System.currentTimeMillis() - snap.startTimeMs) / 1000.0
                    } else 0.0
                    val fps = if (elapsed > 0 && snap.frameSubmitCount > 0) {
                        String.format("%.0f", snap.frameSubmitCount / elapsed)
                    } else "…"
                    append("帧率: ~$fps fps")
                }
            }
            StreamingService.Companion.StreamState.WAITING_PC,
            StreamingService.Companion.StreamState.PC_CONNECTED -> {
                statusText.text = "等待电脑连接"
                hintText.text = buildString {
                    append("手机已在端口 $PORT 等待连接\n\n")
                    if (phoneIp != null) {
                        append("电脑端手动输入: $phoneIp:$PORT\n\n")
                    }
                    append("—— 连接方式 ——\n\n")
                    append("方式一：USB 连接\n")
                    append("  USB 线连接手机和电脑，打开 PhoneCam 即可\n\n")
                    append("方式二：同一 WiFi\n")
                    append("  手机和电脑连接同一 WiFi，电脑端自动发现\n\n")
                    append("方式三：手机热点\n")
                    append("  1. 手机开启热点\n")
                    append("  2. 电脑连接手机热点\n")
                    append("  3. 电脑端 PhoneCam 启动后自动发现\n")
                    if (phoneIp != null) {
                        append("  或手动输入: $phoneIp:$PORT\n")
                    }
                }
            }
            StreamingService.Companion.StreamState.DISCONNECTED -> {
                statusText.text = "连接断开"
                hintText.text = "PC 端已断开，手机仍在等待重连\n请确认电脑端 PhoneCam 仍在运行"
            }
            StreamingService.Companion.StreamState.START_FAILED -> {
                statusText.text = "启动失败"
                hintText.text = snap.startFailedReason.ifEmpty { "未知错误，请返回重试" }
            }
            else -> {
                statusText.text = "未推流"
                hintText.text = "请先在主页面点击「开始推流」"
            }
        }

        Log.d(TAG, "status: state=${snap.streamState} ip=$phoneIp")
    }

    /**
     * 获取手机局域网 IP 地址（非 loopback、IPv4）
     */
    private fun getLocalIpAddress(): String? {
        try {
            val interfaces = NetworkInterface.getNetworkInterfaces() ?: return null
            for (intf in interfaces) {
                if (intf.isLoopback || !intf.isUp) continue
                for (addr in intf.inetAddresses) {
                    if (addr is Inet4Address && !addr.isLoopbackAddress) {
                        return addr.hostAddress
                    }
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Failed to get IP: ${e.message}")
        }
        return null
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }
}
