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
     * 实际 accept 在独立线程跑 (G-022 防御: 不能在主线程 accept 会 ANR)
     *
     * 批次 3.2.0.3h-A1 修复: accept 后不停在原 client 上, 而是用 InputStream.read()
     *  阻塞等客户端断开, read 返 -1/抛异常 → 关闭 + 清空 clientSocket → 回到 accept()
     *  接受下一个客户端 (PC 端 phonecam.py 退出重连场景必须支持)
     */
    fun start() {
        if (running) {
            onEvent("已在运行中, 忽略重复 start()")
            return
        }
        running = true
        acceptThread = Thread({
            try {
                // 1) 尝试作为 Client 连接 127.0.0.1:9999 (适用于 adb reverse 模式)
                var connectedAsClient = false
                try {
                    onEvent("[TCP] 正在尝试 Client 模式连接 127.0.0.1:$port ...")
                    val socket = Socket()
                    socket.connect(java.net.InetSocketAddress("127.0.0.1", port), 1000)
                    clientSocket = socket
                    connectedAsClient = true
                    onEvent("[TCP] Client 模式连接成功: ${socket.remoteSocketAddress}")
                } catch (e: Exception) {
                    onEvent("[TCP] Client 模式连接失败: ${e.message}，降级为 Server 模式")
                }

                if (connectedAsClient) {
                    // Client 模式下的监控与重连循环
                    var firstTime = true
                    while (running) {
                        val client = clientSocket
                        if (client == null || client.isClosed) {
                            // 自动重连
                            if (!firstTime) {
                                onEvent("[TCP] Client 模式连接断开，1s 后尝试重连...")
                                Thread.sleep(1000)
                            } else {
                                firstTime = false
                            }
                            if (!running) break
                            try {
                                val socket = Socket()
                                socket.connect(java.net.InetSocketAddress("127.0.0.1", port), 2000)
                                clientSocket = socket
                                onEvent("[TCP] Client 模式重连成功: ${socket.remoteSocketAddress}")
                                runClientMonitor(socket)
                            } catch (e: Exception) {
                                // 忽略单次重连失败，继续循环
                            }
                        } else {
                            if (firstTime) {
                                firstTime = false
                                runClientMonitor(client)
                            } else {
                                Thread.sleep(200)
                            }
                        }
                    }
                    onEvent("[TCP] Client 模式线程退出")
                    return@Thread
                }

                // 2) 降级为 Server 模式 (适用于 WiFi 模式)
                // 批次 3.2.0.3f: 用 bind() + setReuseAddress(true), 避免上次连接 TIME_WAIT (60s) 内 bind 同端口失败
                //  ServerSocket(port) 旧构造不暴露底层 SO_REUSEADDR
                serverSocket = java.net.ServerSocket()
                serverSocket!!.reuseAddress = true
                serverSocket!!.bind(java.net.InetSocketAddress("0.0.0.0", port), 1)
                onEvent("ServerSocket 监听 0.0.0.0:$port OK (SO_REUSEADDR)")
                // 批次 3.2.0.3h-A1: 接受循环持续, 一个 client 断开后立即接下一个
                while (running) {
                    val client: Socket = try {
                        serverSocket!!.accept()  // 阻塞, 等客人进门
                    } catch (e: Exception) {
                        if (running) {
                            onEvent("[3.2.0.3h] accept 异常: ${e.javaClass.simpleName}: ${e.message}")
                            Thread.sleep(500)  // 短暂退避, 避免死循环刷屏
                        }
                        continue
                    }
                    if (!running) {
                        try { client.close() } catch (_: Exception) {}
                        break
                    }
                    onEvent("[3.2.0.3h] 客户端连接: ${client.remoteSocketAddress}")
                    clientSocket = client

                    runClientMonitor(client)
                }
            } catch (e: Exception) {
                if (running) {
                    onEvent("[3.2.0.3h] accept loop 顶层异常: ${e.javaClass.simpleName}: ${e.message}")
                    Log.e(tag, "accept loop 顶层异常", e)
                }
            } finally {
                onEvent("[3.2.0.3h] accept 线程退出")
            }
        }, "TcpStreamServer-Accept")
        acceptThread?.start()
    }

    private fun runClientMonitor(client: Socket) {
        var clientAlive = true
        val reason: String = try {
            val monitor = client.getInputStream()
            val buf = ByteArray(1024)
            // read() 阻塞; 对端 close 时返 -1; 网络异常抛 IOException
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
                    // 阶段 1: 解析客户端发来的反向控制指令 (以 US_ASCII 解码)
                    val text = String(buf, 0, n, Charsets.US_ASCII)
                    if (text.contains("PLI")) {
                        onCommand("PLI")
                    }
                }
            }
            "client 监控循环结束 (alive=$clientAlive)"
        } catch (e: Exception) {
            "[3.2.0.3h] client 监控线程异常: ${e.javaClass.simpleName}: ${e.message}"
        }

        // 清理当前 client
        try { client.close() } catch (_: Exception) {}
        if (clientSocket === client) clientSocket = null
        onEvent("[3.2.0.3h] 客户端已断开: $reason")
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
     * 批次 3.2.0.3h-A1 修复: 发送失败时 (Broken pipe / Socket closed) 立即关 socket + 清字段,
     *  让 accept 循环在 client 监控 read() 返 -1 后重新接下一个客户端
     * 静默: 没有客户端时返 false 但不打印日志, 避免 30ms 一次的日志风暴
     *
     * @param packet 完整的 [32 字节头][payload] 字节数组 (PcpPacketWriter.buildPacket 输出)
     * @return true=成功, false=失败 (无客户端/已断)
     */
    fun sendPacket(packet: ByteArray): Boolean {
        val client = clientSocket ?: return false  // 批次 3.2.0.3h-A1: 静默不报错
        return try {
            synchronized(client) {
                client.getOutputStream().apply {
                    write(packet)
                    flush()
                }
            }
            true
        } catch (e: Exception) {
            // 批次 3.2.0.3h-A1: 客户端断, 关 socket + 清空 clientSocket
            //  accept 循环的 InputStream.read() 会同时返 -1, 触发重新 accept
            onEvent("[3.2.0.3h] sendPacket 客户端断开: ${e.javaClass.simpleName}: ${e.message}")
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
        try { serverSocket?.close() } catch (_: Exception) {}
        try { clientSocket?.close() } catch (_: Exception) {}
        serverSocket = null
        clientSocket = null
        acceptThread = null
        onEvent("已 stop()")
    }
}
