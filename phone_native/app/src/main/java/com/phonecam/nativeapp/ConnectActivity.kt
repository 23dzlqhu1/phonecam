package com.phonecam.nativeapp

import android.os.Bundle
import android.text.TextUtils
import android.util.Log
import android.util.Patterns
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import java.net.Inet4Address
import java.net.NetworkInterface
import java.util.Collections

/**
 * ConnectActivity —— 连接 PC 页 (Phase Y-2 完整)
 *
 * 来源: specs/features/app-architecture-B-multiscreen.md §4.3
 *
 * 范围 (本批次):
 *   - 4 区块布局: 状态/扫码/手动/已连接
 *   - 状态指示: 未连接 (灰点) / 连接中 (琥珀) / 已连接 (绿)
 *   - 手动输入 IP + 端口 + 连接按钮
 *   - 真实连接: 暂不实现, 弹 Toast "⏳ 待 Phase Z"
 *   - 真实 QR: 暂不实现, 240x240 占位框
 *
 * 不做 (Phase Z):
 *   - QR 码生成 (需 zxing)
 *   - mDNS 发现 PC
 *   - TCP / PCP 实际连接
 *   - 已连接设备列表 (Phase Y 永远空)
 */
class ConnectActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "ConnectActivity"
        private const val DEFAULT_PORT = 7878
        // 模拟连接延迟 (Phase Y-2 演示用, 真实连接 Phase Z)
        private const val SIMULATED_CONNECT_DELAY_MS = 1500L
    }

    private enum class Status { DISCONNECTED, CONNECTING, CONNECTED }

    private var currentStatus: Status = Status.DISCONNECTED

    // 视图引用
    private lateinit var statusBigDot: View
    private lateinit var statusBigText: TextView
    private lateinit var statusBigHint: TextView
    private lateinit var qrIp: TextView
    private lateinit var inputIp: EditText
    private lateinit var inputPort: EditText
    private lateinit var btnConnect: Button

    // 持久化 (Phase Y-5 加)
    private lateinit var settings: SettingsStore

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_connect)

        // ActionBar
        supportActionBar?.apply {
            setDisplayHomeAsUpEnabled(true)
            title = getString(R.string.title_connect)
        }

        // 绑定视图
        statusBigDot = findViewById(R.id.statusBigDot)
        statusBigText = findViewById(R.id.statusBigText)
        statusBigHint = findViewById(R.id.statusBigHint)
        qrIp = findViewById(R.id.qrIp)
        inputIp = findViewById(R.id.inputIp)
        inputPort = findViewById(R.id.inputPort)
        btnConnect = findViewById(R.id.btnConnect)

        // 持久化 (Phase Y-5 加)
        settings = SettingsStore(this)

        // 回填上次输入 (Phase Y-5 加)
        if (settings.lastIp.isNotEmpty()) inputIp.setText(settings.lastIp)
        if (settings.lastPort.isNotEmpty()) inputPort.setText(settings.lastPort)

        // 尝试展示本机 WiFi IP (给 QR 码用)
        showLocalIp()

        // 连接按钮
        btnConnect.setOnClickListener { onConnectClicked() }

        // 初始状态
        renderStatus()
    }

    /**
     * 读取本机 WiFi IPv4 地址 (Phase Y-2 仅展示, 实际二维码内容 Phase Z 再加)
     * 没有 WiFi 时显示 "未连接 WiFi"
     */
    private fun showLocalIp() {
        val ip = getLocalIpv4()
        if (ip != null) {
            qrIp.text = "$ip:$DEFAULT_PORT"
            Log.d(TAG, "local IP: $ip:$DEFAULT_PORT")
        } else {
            qrIp.text = getString(R.string.connect_qr_ip_unknown)
            Log.d(TAG, "no WiFi IP found")
        }
    }

    /**
     * 遍历所有网络接口, 找第一个 IPv4 站点本地地址 (192.168.x.x / 10.x.x.x)
     */
    private fun getLocalIpv4(): String? {
        return try {
            Collections.list(NetworkInterface.getNetworkInterfaces())
                .flatMap { Collections.list(it.inetAddresses) }
                .filterIsInstance<Inet4Address>()
                .firstOrNull { !it.isLoopbackAddress }
                ?.hostAddress
        } catch (e: Exception) {
            Log.w(TAG, "getLocalIpv4 failed: ${e.message}")
            null
        }
    }

    /**
     * "连接" 按钮: 校验 → 模拟 1.5s 连接 → 弹 Toast
     * 真实逻辑 Phase Z 实现
     */
    private fun onConnectClicked() {
        if (currentStatus == Status.CONNECTING) {
            Log.d(TAG, "already connecting, ignore")
            return
        }

        val ip = inputIp.text.toString().trim()
        val portStr = inputPort.text.toString().trim()

        // 校验
        if (TextUtils.isEmpty(ip)) {
            inputIp.error = getString(R.string.connect_toast_no_ip)
            inputIp.requestFocus()
            return
        }
        if (!isValidIp(ip)) {
            inputIp.error = getString(R.string.connect_toast_invalid_ip)
            inputIp.requestFocus()
            return
        }
        if (TextUtils.isEmpty(portStr)) {
            inputPort.error = getString(R.string.connect_toast_no_port)
            inputPort.requestFocus()
            return
        }
        val port = portStr.toIntOrNull()
        if (port == null || port !in 1..65535) {
            inputPort.error = getString(R.string.connect_toast_no_port)
            inputPort.requestFocus()
            return
        }

        // 模拟连接
        setStatus(Status.CONNECTING)
        btnConnect.isEnabled = false
        btnConnect.text = getString(R.string.connect_btn_connecting)

        // 持久化输入 (Phase Y-5 加, 下次打开自动回填)
        settings.lastIp = ip
        settings.lastPort = port.toString()

        Toast.makeText(
            this,
            getString(R.string.connect_toast_connecting, ip, port.toString()),
            Toast.LENGTH_SHORT
        ).show()
        InAppLogStore.d(TAG, "connecting to $ip:$port (simulated)")

        // 1.5s 后: 弹 "⏳ 待 Phase Z", 状态回 DISCONNECTED
        btnConnect.postDelayed({
            setStatus(Status.DISCONNECTED)
            btnConnect.isEnabled = true
            btnConnect.text = getString(R.string.connect_btn)
            Toast.makeText(this, R.string.connect_toast_not_implemented, Toast.LENGTH_LONG).show()
        }, SIMULATED_CONNECT_DELAY_MS)
    }

    /**
     * 简单 IPv4 格式校验
     */
    private fun isValidIp(ip: String): Boolean {
        return Patterns.IP_ADDRESS.matcher(ip).matches() &&
            ip.split(".").size == 4
    }

    private fun setStatus(newStatus: Status) {
        currentStatus = newStatus
        renderStatus()
    }

    /**
     * 状态指示的视觉渲染
     */
    private fun renderStatus() {
        when (currentStatus) {
            Status.DISCONNECTED -> {
                statusBigDot.background = getDrawable(R.drawable.dot_inactive)
                statusBigText.text = getString(R.string.connect_status_disconnected)
                statusBigHint.text = getString(R.string.connect_status_disconnected_hint)
            }
            Status.CONNECTING -> {
                statusBigDot.background = getDrawable(R.drawable.dot_inactive)  // 没有黄色 dot, 复用灰 (Phase Z 加琥珀)
                statusBigText.text = getString(R.string.connect_status_connecting)
                statusBigHint.text = getString(R.string.connect_status_disconnected_hint)
            }
            Status.CONNECTED -> {
                statusBigDot.background = getDrawable(R.drawable.status_dot_connected)
                statusBigText.text = getString(R.string.connect_status_connected)
                statusBigHint.text = getString(R.string.connect_status_disconnected_hint)  // Phase Z 显示 PC 名
            }
        }
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }
}
