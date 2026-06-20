package com.phonecam.nativeapp

import android.util.Log
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket

/**
 * TcpStreamServer —— phone_native/ TCP 服务端
 *
 * 架构:
 *   手机端 = TCP Server (本类，监听 0.0.0.0:9999)
 *   PC 端 = TCP Client (PcpReceiver 连接 127.0.0.1:9999，经 ADB forward 隧道到手机)
 *
 * 工作流:
 *   start()      → 启动 accept 循环，监听 9999 端口
 *   sendPacket() → 通过已建立的连接发送 PCP 包
 *   stop()       → 关闭连接，停止服务
 *
 * 重连机制:
 *   - 一个客户端断开后立即接受下一个客户端
 *   - 收到 PC 发来的 "PLI" 指令时回调 onCommand
 */
class TcpStreamServer(
    private val port: Int = 9999,
    private val onEvent: (String) -> Unit = {},
    private val onCommand: (String) -> Unit = {}
) {

    private val tag = "TcpStreamServer"

    @Volatile private var running = false
    @Volatile private var clientSocket: Socket? = null
    private var serverSocket: ServerSocket? = null
    private var acceptThread: Thread? = null

    /**
     * 启动 accept 循环 (非阻塞, 立刻返回)
     * 实际 accept 在独立线程跑
     */
    fun start() {
        if (running) {
            onEvent("已在运行中, 忽略重复 start()")
            return
        }
        running = true
        acceptThread = Thread({
            try {
                // 用 bind() + setReuseAddress(true), 避免上次连接 TIME_WAIT (60s) 内 bind 同端口失败
                serverSocket = java.net.ServerSocket()
                serverSocket!!.reuseAddress = true
                serverSocket!!.bind(java.net.InetSocketAddress("0.0.0.0", port), 1)
                onEvent("ServerSocket 监听 0.0.0.0:$port OK (SO_REUSEADDR)")

                // 接受循环持续, 一个 client 断开后立即接下一个
                while (running) {
                    val client: Socket = try {
                        serverSocket!!.accept()  // 阻塞, 等客人进门
                    } catch (e: Exception) {
                        if (running) {
                            onEvent("[TCP] accept 异常: ${e.javaClass.simpleName}: ${e.message}")
                            Thread.sleep(500)
                        }
                        continue
                    }
                    if (!running) {
                        try { client.close() } catch (_: Exception) {}
                        break
                    }
                    onEvent("[TCP] 客户端连接: ${client.remoteSocketAddress}")
                    clientSocket = client

                    runClientMonitor(client)
                }
            } catch (e: Exception) {
                if (running) {
                    onEvent("[TCP] accept loop 顶层异常: ${e.javaClass.simpleName}: ${e.message}")
                    Log.e(tag, "accept loop 顶层异常", e)
                }
            } finally {
                onEvent("[TCP] accept 线程退出")
            }
        }, "TcpStreamServer-Accept")
        acceptThread?.start()
    }

    private fun runClientMonitor(client: Socket) {
        var clientAlive = true
        val reason: String = try {
            val monitor = client.getInputStream()
            val buf = ByteArray(1024)
            while (running && clientAlive && client.isConnected && !client.isClosed) {
                val n = try {
                    monitor.read(buf)
                } catch (ie: Exception) {
                    clientAlive = false
                    "对端 read 异常: ${ie.javaClass.simpleName}: ${ie.message}"
                    break
                }
                if (n == -1) {
                    clientAlive = false
                    "对端 close (read=-1)"
                    break
                }
                if (n > 0) {
                    // 解析客户端发来的反向控制指令
                    val text = String(buf, 0, n, Charsets.US_ASCII)
                    if (text.contains("PLI")) {
                        onCommand("PLI")
                    }
                }
            }
            "client 监控循环结束 (alive=$clientAlive)"
        } catch (e: Exception) {
            "[TCP] client 监控线程异常: ${e.javaClass.simpleName}: ${e.message}"
        }

        // 清理当前 client
        try { client.close() } catch (_: Exception) {}
        if (clientSocket === client) clientSocket = null
        onEvent("[TCP] 客户端已断开: $reason")
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
     */
    fun sendPacket(packet: ByteArray): Boolean {
        val client = clientSocket ?: return false
        return try {
            synchronized(client) {
                client.getOutputStream().apply {
                    write(packet)
                    flush()
                }
            }
            true
        } catch (e: Exception) {
            onEvent("[TCP] sendPacket 客户端断开: ${e.javaClass.simpleName}: ${e.message}")
            try { client.close() } catch (_: Exception) {}
            if (clientSocket === client) clientSocket = null
            false
        }
    }

    /**
     * 关停服务 (关 serverSocket + clientSocket, 唤醒 accept 阻塞)
     */
    fun stop() {
        running = false
        try { clientSocket?.close() } catch (_: Exception) {}
        try { serverSocket?.close() } catch (_: Exception) {}
        serverSocket = null
        clientSocket = null
        try { acceptThread?.join(3000) } catch (_: InterruptedException) {}
        acceptThread = null
        onEvent("已 stop()，后台接受线程退出")
    }
}
