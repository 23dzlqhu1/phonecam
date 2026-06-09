package com.phonecam.nativeapp

import android.util.Log
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket

/**
 * TcpStreamServer —— phone_native/ 批次 3.2.0.3b TCP 服务端
 *
 * 作用: 监听 0.0.0.0:9999, 接受电脑端连接, 提供 sendPacket(byteArray) 把 PCP 包发出去
 *       协议 = 裸 TCP, 不加密不压缩, pc 端用 socket.recv() 直接读字节流
 *
 * 工作模型 (类比餐厅):
 *   - start()  → 迎宾员站到门口 (ServerSocket 监听 9999)
 *   - accept() → 客人进门 (电脑端 phonecam.py 连上来)
 *   - sendPacket() → 服务员把菜 (PCP 包) 端到这位客人桌上 (getOutputStream().write)
 *   - stop()   → 餐厅打烊 (关 serverSocket + clientSocket)
 *
 * 范围 (批次 3.2.0.3b):
 *   - 单客户端 (MVP 阶段手机只连 1 台电脑)
 *   - 不接 Camera2/H264Encoder, 客户端连上后只发 1 个 "Hello PCP" 测试包
 *   - 阻塞式 sendPacket (等 3.2.0.3c 改成 Camera2 回调里异步发)
 *
 * 不做 (后续批次):
 *   - 3.2.0.3c: 接 H264Encoder 持续推流, sendPacket 改成无锁 + 背压控制
 *   - 3.2.0.3d: 电脑端 PcpReceiver 解码
 *   - MVP-4: 多客户端 / WiFi 自动发现
 *
 * 已知风险 (G-022 防御):
 *   - Android 9+ cleartext traffic 默认禁止 HTTP 但允许裸 TCP, 9999 端口不受影响
 *   - ServerSocket 占用 9999 失败时 (e.g. 上一轮没关) 抛 BindException → onEvent 提示
 *   - accept() 阻塞式, stop() 需要先 close() serverSocket 才能从阻塞中唤醒
 */
class TcpStreamServer(
    private val port: Int = 9999,
    private val onEvent: (String) -> Unit = {}
) {

    private val tag = "TcpStreamServer"

    @Volatile private var running = false
    @Volatile private var clientSocket: Socket? = null
    private var serverSocket: ServerSocket? = null
    private var acceptThread: Thread? = null

    /**
     * 启动 accept 循环 (非阻塞, 立刻返回)
     * 实际 accept 在独立线程跑 (G-022 防御: 不能在主线程 accept 会 ANR)
     */
    fun start() {
        if (running) {
            onEvent("已在运行中, 忽略重复 start()")
            return
        }
        running = true
        acceptThread = Thread({
            try {
                serverSocket = ServerSocket(port, 1, InetAddress.getByName("0.0.0.0"))
                onEvent("ServerSocket 监听 0.0.0.0:$port OK")
                while (running) {
                    val client = serverSocket!!.accept()  // 阻塞, 客人来之前一直等
                    if (!running) {
                        client.close()
                        break
                    }
                    onEvent("客户端连接: ${client.remoteSocketAddress}")
                    clientSocket = client
                }
            } catch (e: Exception) {
                if (running) {
                    onEvent("异常 (accept loop): ${e.javaClass.simpleName}: ${e.message}")
                    Log.e(tag, "accept loop 异常", e)
                }
            }
        }, "TcpStreamServer-Accept")
        acceptThread?.start()
    }

    /**
     * 检查客户端是否已连上
     */
    fun isClientConnected(): Boolean {
        val c = clientSocket ?: return false
        return c.isConnected && !c.isClosed
    }

    /**
     * 发送 1 个 PCP 包到当前客户端 (线程安全: 同步访问 clientSocket)
     *
     * @param packet 完整的 [24 字节头][payload] 字节数组 (PcpPacketWriter.buildPacket 输出)
     * @return true=成功, false=失败 (无客户端/已断)
     */
    fun sendPacket(packet: ByteArray): Boolean {
        val client = clientSocket ?: run {
            onEvent("sendPacket 失败: 无客户端连接")
            return false
        }
        return try {
            synchronized(client) {
                client.getOutputStream().apply {
                    write(packet)
                    flush()
                }
            }
            onEvent("已发送: ${packet.size} 字节")
            true
        } catch (e: Exception) {
            onEvent("sendPacket 异常: ${e.javaClass.simpleName}: ${e.message}")
            false
        }
    }

    /**
     * 关停服务 (关 serverSocket + clientSocket, 唤醒 accept 阻塞)
     */
    fun stop() {
        running = false
        try { serverSocket?.close() } catch (_: Exception) {}
        try { clientSocket?.close() } catch (_: Exception) {}
        serverSocket = null
        clientSocket = null
        acceptThread = null
        onEvent("已 stop()")
    }
}
