package com.phonecam.nativeapp

import android.content.Context
import android.os.Build
import android.provider.Settings
import android.util.Log
import org.json.JSONObject
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.SocketException
import java.nio.charset.StandardCharsets

/**
 * DiscoveryResponder —— PhoneCam Discovery V1 的 Android 应答端
 *
 * 职责（严格限制）：
 *   收到 PC 端 UDP discovery request (type=PHONECAM_DISCOVER)
 *   → 单播回复 PHONECAM_HERE。
 *
 * 不包含任何视频编码 / TCP server / Camera2 逻辑，与 EGL/MediaCodec/H264 完全解耦。
 *
 * 生命周期由 StreamingService 的推流生命周期绑定：
 *   开始推流 → start()
 *   停止推流 / Service destroy / 启动失败 → stop()
 *
 * 协议细节：
 *   - UDP 固定端口 9997 (discoveryPort)
 *   - 视频 TCP 端口 9999 (tcpPort) 在响应中声明
 *   - 只使用 Android 自带 JSONObject + java.net，不引入第三方依赖
 *   - 无效 packet 静默忽略（最多 debug log）
 *
 * 线程模型：
 *   独立轻量线程 "PhoneCam-Discovery"，start() 幂等，
 *   stop() 关闭 socket 使阻塞的 receive() 抛 SocketException 退出，不残留线程。
 */
class DiscoveryResponder(
    private val context: Context,
    private val discoveryPort: Int = 9997,
    private val tcpPort: Int = 9999
) {
    private var socket: DatagramSocket? = null
    private var thread: Thread? = null
    private val lock = Any()

    /**
     * 启动监听。幂等：已在运行则直接返回。
     */
    fun start() {
        synchronized(lock) {
            val existing = thread
            if (existing != null && existing.isAlive) {
                Log.d(TAG, "start: 已在运行，幂等返回")
                return
            }
            try {
                val s = DatagramSocket(null)
                s.reuseAddress = true
                // bind 0.0.0.0:9997，接收任意接口的 discovery broadcast
                s.bind(InetSocketAddress(InetAddress.getByName("0.0.0.0"), discoveryPort))
                socket = s
                val t = Thread({ runLoop(s) }, THREAD_NAME)
                t.isDaemon = true
                thread = t
                t.start()
                Log.i(TAG, "DiscoveryResponder 已启动，监听 UDP :$discoveryPort")
            } catch (e: Exception) {
                Log.w(TAG, "start 失败: ${e.message}", e)
                try { socket?.close() } catch (_: Exception) {}
                socket = null
            }
        }
    }

    /**
     * 停止监听。关闭 socket → 阻塞中的 receive() 抛 SocketException → 线程退出。
     * 可安全重复调用。
     */
    fun stop() {
        synchronized(lock) {
            val s = socket
            socket = null
            try { s?.close() } catch (_: Exception) {}
            val t = thread
            thread = null
            t?.interrupt()
            Log.i(TAG, "DiscoveryResponder 已停止")
        }
    }

    private fun runLoop(s: DatagramSocket) {
        val buf = ByteArray(MAX_PACKET_SIZE)
        while (true) {
            try {
                val packet = DatagramPacket(buf, buf.size)
                s.receive(packet)  // 阻塞；stop() 关闭 socket 后抛 SocketException 退出
                handlePacket(s, packet)
            } catch (e: SocketException) {
                break  // socket 已关闭 → 正常退出线程
            } catch (e: InterruptedException) {
                break
            } catch (e: Exception) {
                Log.d(TAG, "receive 异常（忽略）: ${e.message}")
                // 防止异常导致的空转
                try { Thread.sleep(10) } catch (_: InterruptedException) { break }
            }
        }
        Log.d(TAG, "discovery thread 已退出")
    }

    private fun handlePacket(s: DatagramSocket, packet: DatagramPacket) {
        try {
            val text = String(packet.data, 0, packet.length, StandardCharsets.UTF_8)
            val obj = JSONObject(text)

            // 验证 request：type == PHONECAM_DISCOVER && version == 1 && nonce 非空
            if (obj.optString("type") != "PHONECAM_DISCOVER") return
            if (obj.optInt("version", -1) != 1) return
            val nonce = obj.optString("nonce")
            if (nonce.isEmpty()) return

            val response = JSONObject()
            response.put("type", "PHONECAM_HERE")
            response.put("version", 1)
            response.put("nonce", nonce)             // 原样返回 request nonce
            response.put("deviceId", getDeviceId())
            response.put("deviceName", getDeviceName())
            // 8月9日修复 A: 报告 App 版本 (BuildConfig 自动跟随 Gradle 版本, 禁止硬编码)
            response.put("appVersion", BuildConfig.VERSION_NAME)
            response.put("appVersionCode", BuildConfig.VERSION_CODE)
            response.put("tcpPort", tcpPort)
            response.put("pcpVersion", PCP_VERSION)

            val payload = response.toString().toByteArray(StandardCharsets.UTF_8)
            // 单播回复到 request 的 sender address + sender UDP port（不包含 IP 在 payload 中）
            val reply = DatagramPacket(payload, payload.size, packet.address, packet.port)
            s.send(reply)
            Log.d(TAG, "已回复 discovery 请求来自 ${packet.address}:${packet.port}")
        } catch (e: Exception) {
            // 无效 packet 静默忽略，最多 debug log
            Log.d(TAG, "忽略无效 discovery packet: ${e.message}")
        }
    }

    /**
     * deviceId 来源：优先 Settings.Secure.ANDROID_ID；
     * 异常或为空时使用稳定 fallback，不允许因此崩溃。
     */
    private fun getDeviceId(): String {
        return try {
            Settings.Secure.getString(context.contentResolver, Settings.Secure.ANDROID_ID)
                ?: ""
        } catch (e: Exception) {
            Log.w(TAG, "读取 ANDROID_ID 失败，使用 fallback: ${e.message}")
            ""
        }.takeIf { it.isNotBlank() } ?: fallbackDeviceId()
    }

    private fun fallbackDeviceId(): String {
        // 稳定 fallback：基于厂商+型号，只做去空/去重处理
        return "android-${Build.MANUFACTURER}-${Build.MODEL}".trim().replace(" ", "-")
    }

    /**
     * deviceName：MANUFACTURER + " " + MODEL，去重/trim。
     */
    private fun getDeviceName(): String {
        return "${Build.MANUFACTURER} ${Build.MODEL}".trim()
    }

    companion object {
        private const val TAG = "DiscoveryResponder"
        private const val THREAD_NAME = "PhoneCam-Discovery"
        private const val MAX_PACKET_SIZE = 2048
        private const val PCP_VERSION = 2
    }
}
